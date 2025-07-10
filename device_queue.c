/******************************************************************************
 * device_queue.c
 * 
 * Generic thread-safe command queue implementation for device control
 * with guaranteed sequential transaction execution
 ******************************************************************************/

#include "device_queue.h"
#include "logging.h"
#include "toolbox.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Internal Structures and Definitions
 ******************************************************************************/

// Command state enumeration
typedef enum {
    CMD_STATE_QUEUED,
    CMD_STATE_EXECUTING,
    CMD_STATE_COMPLETED
} CommandState;

// Base command structure - reference counted for safe multi-threaded access
typedef struct {
    // Command identification
    DeviceCommandID id;
    int commandType;
    DevicePriority priority;
    double timestamp;
    
    // Command state
    volatile CommandState state;
    
    // Reference counting for thread-safe lifetime management
    volatile int refCount;
    CmtThreadLockHandle refLock;
    
    // Command parameters (owned by this command)
    void *params;
    
    // For blocking commands - points to stack-allocated BlockingContext
    void *blockingContext;
    
    // For async commands
    DeviceCommandCallback callback;
    void *userData;
    
    // Transaction association
    DeviceTransactionHandle transactionId;
} QueuedCommand;

// Blocking command context - stack allocated by calling thread
typedef struct {
    CmtThreadLockHandle completionEvent;
    void *result;                // Pre-allocated result storage
    volatile int errorCode;
    volatile int completed;
} BlockingContext;

// Transaction structure
typedef struct {
    DeviceTransactionHandle id;
    QueuedCommand *commands[DEVICE_MAX_TRANSACTION_COMMANDS];
    int commandCount;
    DeviceTransactionCallback callback;
    void *userData;
    bool committed;
    bool executing;
    
    // Configuration
    DeviceTransactionFlags flags;
    DevicePriority priority;
    int timeoutMs;
    
    // Results tracking
    TransactionCommandResult *results;
    int successCount;
    int failureCount;
    double startTime;
} DeviceTransaction;

// Queue manager structure
struct DeviceQueueManager {
    // Device adapter and context
    const DeviceAdapter *adapter;
    void *deviceContext;
    void *connectionParams;
    
    // Thread-safe queues
    CmtTSQHandle highPriorityQueue;
    CmtTSQHandle normalPriorityQueue;
    CmtTSQHandle lowPriorityQueue;
    
    // Processing thread
    CmtThreadFunctionID processingThreadId;
    volatile int shutdownRequested;
    
    // Current command tracking
    volatile QueuedCommand *currentCommand;
    CmtThreadLockHandle currentCommandLock;
    
    // Connection state
    volatile int isConnected;
    int reconnectAttempts;
    double lastReconnectTime;
    
    // Command and transaction ID generation
    volatile DeviceCommandID nextCommandId;
    volatile DeviceTransactionHandle nextTransactionId;
    CmtThreadLockHandle commandLock;
    
    // Active transactions
    ListType activeTransactions;
    CmtThreadLockHandle transactionLock;
    volatile int activeTransactionHandle;  // Currently executing transaction
    volatile int inTransactionMode;        // Processing thread is in transaction mode
    
    // Statistics
    volatile int totalProcessed;
    volatile int totalErrors;
    CmtThreadLockHandle statsLock;
    
    // Logging
    LogDevice logDevice;
    
    // Queue manipulation lock for cancel operations
    CmtThreadLockHandle queueManipulationLock;
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

// Command lifecycle management
static QueuedCommand* Command_Create(DeviceQueueManager *mgr, int commandType, void *params);
static void Command_AddRef(QueuedCommand *cmd);
static void Command_Release(DeviceQueueManager *mgr, QueuedCommand *cmd);

// Processing thread
static int CVICALLBACK ProcessingThreadFunction(void *functionData);
static int ProcessCommand(DeviceQueueManager *mgr, QueuedCommand *cmd);
static int ExecuteDeviceCommand(DeviceQueueManager *mgr, QueuedCommand *cmd, void *result);

// Connection management
static int ConnectDevice(DeviceQueueManager *mgr);
static void DisconnectDevice(DeviceQueueManager *mgr);
static int AttemptReconnection(DeviceQueueManager *mgr);

// Transaction management
static DeviceTransaction* FindTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle id);
static DeviceTransaction* GetNextReadyTransaction(DeviceQueueManager *mgr);
static int ProcessTransaction(DeviceQueueManager *mgr, DeviceTransaction *txn);
static void CleanupTransactionResults(DeviceQueueManager *mgr, DeviceTransaction *txn);

// Cancellation helpers
static int CancelCommandInQueue(CmtTSQHandle queue, DeviceCommandID cmdId, DeviceQueueManager *mgr);
static int CancelByTypeInQueue(CmtTSQHandle queue, int commandType, DeviceQueueManager *mgr);
static int CancelByAgeInQueue(CmtTSQHandle queue, double currentTime, double maxAge, DeviceQueueManager *mgr);

// Validation
static bool ValidateAdapter(const DeviceAdapter *adapter);

/******************************************************************************
 * Design Notes:
 * 
 * This implementation uses reference counting for command lifetime management:
 * - Commands are created with refCount = 1
 * - When queued, refCount is incremented
 * - When dequeued/processed, refCount is decremented
 * - Command is freed when refCount reaches 0
 * 
 * Blocking commands use a stack-allocated BlockingContext that is owned by
 * the calling thread. This ensures clean ownership and prevents resource leaks.
 * 
 * The processing thread continues running during shutdown until all queues
 * are empty, ensuring no commands are leaked.
 ******************************************************************************/

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

DeviceQueueManager* DeviceQueue_Create(const DeviceAdapter *adapter, 
                                     void *deviceContext,
                                     void *connectionParams) {
    if (!adapter || !deviceContext) {
        LogError("DeviceQueue_Create: Invalid parameters");
        return NULL;
    }
    
    // Validate adapter has required functions
    if (!ValidateAdapter(adapter)) {
        LogError("DeviceQueue_Create: Invalid adapter - missing required functions");
        return NULL;
    }
    
    DeviceQueueManager *mgr = calloc(1, sizeof(DeviceQueueManager));
    if (!mgr) {
        LogError("DeviceQueue_Create: Failed to allocate manager");
        return NULL;
    }
    
    // Store device info
    mgr->adapter = adapter;
    mgr->deviceContext = deviceContext;
    mgr->connectionParams = connectionParams;
    mgr->nextCommandId = 1;
    mgr->nextTransactionId = 1;
    mgr->isConnected = 0;
    mgr->shutdownRequested = 0;
    mgr->logDevice = LOG_DEVICE_NONE;
    mgr->activeTransactionHandle = 0;
    mgr->inTransactionMode = 0;
    mgr->currentCommand = NULL;
    
    // Create queues
    int error = 0;
    error |= CmtNewTSQ(DEVICE_QUEUE_HIGH_PRIORITY_SIZE, sizeof(DeviceQueuedCommand*), 0, &mgr->highPriorityQueue);
    error |= CmtNewTSQ(DEVICE_QUEUE_NORMAL_PRIORITY_SIZE, sizeof(DeviceQueuedCommand*), 0, &mgr->normalPriorityQueue);
    error |= CmtNewTSQ(DEVICE_QUEUE_LOW_PRIORITY_SIZE, sizeof(DeviceQueuedCommand*), 0, &mgr->lowPriorityQueue);
    
    if (error < 0) {
        LogError("DeviceQueue_Create: Failed to create queues");
        DeviceQueue_Destroy(mgr);
        return NULL;
    }
    
    // Create locks and events
    CmtNewLock(NULL, 0, &mgr->commandLock);
    CmtNewLock(NULL, 0, &mgr->transactionLock);
    CmtNewLock(NULL, 0, &mgr->statsLock);
    CmtNewLock(NULL, 0, &mgr->currentCommandLock);
	CmtNewLock(NULL, 0, &mgr->queueManipulationLock);
    
    // Initialize transaction list
    mgr->activeTransactions = ListCreate(sizeof(DeviceTransaction*));
    
    // Attempt initial connection
    LogMessageEx(mgr->logDevice, "Attempting to connect to %s...", adapter->deviceName);
    int connectResult = ConnectDevice(mgr);
    
    if (connectResult == SUCCESS) {
        LogMessageEx(mgr->logDevice, "Successfully connected to %s", adapter->deviceName);
        mgr->isConnected = 1;
    } else {
        LogWarningEx(mgr->logDevice, "Failed initial connection to %s - will retry in background", 
                   adapter->deviceName);
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
    }
    
    // Start processing thread
    if (g_threadPool > 0) {
        error = CmtScheduleThreadPoolFunction(g_threadPool, ProcessingThreadFunction, 
                                            mgr, &mgr->processingThreadId);
        if (error != 0) {
            LogErrorEx(mgr->logDevice, "DeviceQueue_Create: Failed to start processing thread");
            DeviceQueue_Destroy(mgr);
            return NULL;
        }
    } else {
        LogErrorEx(mgr->logDevice, "DeviceQueue_Create: No thread pool available");
        DeviceQueue_Destroy(mgr);
        return NULL;
    }
    
    LogMessageEx(mgr->logDevice, "%s queue manager initialized", adapter->deviceName);
    return mgr;
}

void DeviceQueue_Destroy(DeviceQueueManager *mgr) {
    if (!mgr) return;
    
    LogMessageEx(mgr->logDevice, "Shutting down %s queue manager...", mgr->adapter->deviceName);
    
    // Signal shutdown
    InterlockedExchange(&mgr->shutdownRequested, 1);
    
    // Wait for processing thread to complete
    if (mgr->processingThreadId != 0) {
        LogMessageEx(mgr->logDevice, "Waiting for processing thread to complete...");
        
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, mgr->processingThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        
        LogMessageEx(mgr->logDevice, "%s queue processing thread stopped", mgr->adapter->deviceName);
    }
    
    // At this point, all queues should be empty
    // Verify and log if not
    int highCount = 0, normalCount = 0, lowCount = 0;
    CmtGetTSQAttribute(mgr->highPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &highCount);
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &normalCount);
    CmtGetTSQAttribute(mgr->lowPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &lowCount);
    
    if (highCount + normalCount + lowCount > 0) {
        LogWarningEx(mgr->logDevice, "Queues not empty after shutdown: high=%d, normal=%d, low=%d",
                   highCount, normalCount, lowCount);
    }
    
    // Disconnect device
    DisconnectDevice(mgr);
    
    // Clean up transactions
    if (mgr->activeTransactions) {
        CmtGetLock(mgr->transactionLock);
        int count = ListNumItems(mgr->activeTransactions);
        
        for (int i = count; i >= 1; i--) {
            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
            if (txnPtr && *txnPtr) {
                DeviceTransaction *txn = *txnPtr;
                
                // Free transaction commands
                for (int j = 0; j < txn->commandCount; j++) {
                    if (txn->commands[j]) {
                        Command_Release(mgr, txn->commands[j]);
                    }
                }
                
                CleanupTransactionResults(mgr, txn);
                free(txn);
            }
        }
        
        CmtReleaseLock(mgr->transactionLock);
        ListDispose(mgr->activeTransactions);
    }
    
    // Dispose queues
    if (mgr->highPriorityQueue) CmtDiscardTSQ(mgr->highPriorityQueue);
    if (mgr->normalPriorityQueue) CmtDiscardTSQ(mgr->normalPriorityQueue);
    if (mgr->lowPriorityQueue) CmtDiscardTSQ(mgr->lowPriorityQueue);
    
    // Dispose locks
    if (mgr->commandLock) CmtDiscardLock(mgr->commandLock);
    if (mgr->transactionLock) CmtDiscardLock(mgr->transactionLock);
    if (mgr->statsLock) CmtDiscardLock(mgr->statsLock);
    if (mgr->currentCommandLock) CmtDiscardLock(mgr->currentCommandLock);
    if (mgr->queueManipulationLock) CmtDiscardLock(mgr->queueManipulationLock);
    
    free(mgr);
    LogMessage("Device queue manager shut down");
}

void* DeviceQueue_GetDeviceContext(DeviceQueueManager *mgr) {
    if (!mgr || !mgr->isConnected) {
        return NULL;
    }
    return mgr->deviceContext;
}

bool DeviceQueue_IsRunning(DeviceQueueManager *mgr) {
    return mgr && !mgr->shutdownRequested;
}

void DeviceQueue_GetStats(DeviceQueueManager *mgr, DeviceQueueStats *stats) {
    if (!mgr || !stats) return;
    
    memset(stats, 0, sizeof(DeviceQueueStats));
    
    // Get queue lengths
    CmtGetTSQAttribute(mgr->highPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &stats->highPriorityQueued);
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &stats->normalPriorityQueued);
    CmtGetTSQAttribute(mgr->lowPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &stats->lowPriorityQueued);
    
    CmtGetLock(mgr->statsLock);
    stats->totalProcessed = mgr->totalProcessed;
    stats->totalErrors = mgr->totalErrors;
    stats->reconnectAttempts = mgr->reconnectAttempts;
    CmtReleaseLock(mgr->statsLock);
    
    stats->isConnected = mgr->isConnected;
    stats->isProcessing = (mgr->processingThreadId != 0);
    
    CmtGetLock(mgr->transactionLock);
    stats->activeTransactionId = mgr->activeTransactionHandle;
    stats->isInTransactionMode = mgr->inTransactionMode;
    CmtReleaseLock(mgr->transactionLock);
}

void DeviceQueue_SetLogDevice(DeviceQueueManager *mgr, LogDevice device) {
    if (mgr) {
        mgr->logDevice = device;
    }
}

bool DeviceQueue_IsInTransaction(DeviceQueueManager *mgr) {
    if (!mgr) return false;
    
    CmtGetLock(mgr->transactionLock);
    bool inTransaction = mgr->inTransactionMode;
    CmtReleaseLock(mgr->transactionLock);
    
    return inTransaction;
}

/******************************************************************************
 * Internal Connection Functions
 ******************************************************************************/

static int ConnectDevice(DeviceQueueManager *mgr) {
    if (!mgr->adapter->connect) {
        return SUCCESS; // No connection needed
    }
    
    int result = mgr->adapter->connect(mgr->deviceContext, mgr->connectionParams);
    
    if (result == SUCCESS && mgr->adapter->testConnection) {
        // Test the connection
        result = mgr->adapter->testConnection(mgr->deviceContext);
    }
    
    return result;
}

static void DisconnectDevice(DeviceQueueManager *mgr) {
    if (mgr->adapter->disconnect && mgr->adapter->isConnected && 
        mgr->adapter->isConnected(mgr->deviceContext)) {
        mgr->adapter->disconnect(mgr->deviceContext);
        mgr->isConnected = 0;
        LogMessageEx(mgr->logDevice, "Disconnected from %s", mgr->adapter->deviceName);
    }
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

int DeviceQueue_CommandBlocking(DeviceQueueManager *mgr, int commandType,
                              void *params, DevicePriority priority,
                              void *result, int timeoutMs) {
    if (!mgr || !result) return ERR_INVALID_PARAMETER;
    
    // Create command
    QueuedCommand *cmd = Command_Create(mgr, commandType, params);
    if (!cmd) return ERR_OUT_OF_MEMORY;
    
    cmd->priority = priority;
    
    // Create blocking context (stack allocated - owned by this thread)
    BlockingContext ctx = {0};
    ctx.errorCode = ERR_TIMEOUT;
    ctx.completed = 0;
    
    // Create completion event
    int error = CmtNewLock(NULL, 0, &ctx.completionEvent);
    if (error < 0) {
        Command_Release(mgr, cmd);
        return ERR_BASE_THREAD;
    }
    
    // Create result storage
    ctx.result = mgr->adapter->createCommandResult(commandType);
    if (!ctx.result) {
        CmtDiscardLock(ctx.completionEvent);
        Command_Release(mgr, cmd);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Link command to blocking context
    cmd->blockingContext = &ctx;
    
    // Add reference for the queue
    Command_AddRef(cmd);
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case DEVICE_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case DEVICE_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case DEVICE_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    int itemsWritten = 0;
    if (timeoutMs >= 0) {
        itemsWritten = CmtWriteTSQData(queue, &cmd, 1, timeoutMs, NULL);
    } else {
        itemsWritten = CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    if (itemsWritten <= 0) {
        LogErrorEx(mgr->logDevice, "Failed to enqueue command type %s", 
                 mgr->adapter->getCommandTypeName(commandType));
        
        // Clean up
        Command_Release(mgr, cmd);  // Release queue's reference
        CmtDiscardLock(ctx.completionEvent);
        mgr->adapter->freeCommandResult(commandType, ctx.result);
        Command_Release(mgr, cmd);  // Release our reference
        
        return (itemsWritten == 0) ? ERR_TIMEOUT : ERR_QUEUE_FULL;
    }
    
    // Wait for completion
    double startTime = Timer();
    double timeout = (timeoutMs > 0 ? timeoutMs : 30000) / 1000.0;
    int finalError = ERR_TIMEOUT;
    
    while ((Timer() - startTime) < timeout) {
        if (ctx.completed) {
            finalError = ctx.errorCode;
            break;
        }
        
        if (timeoutMs == 0) {
            break;  // Immediate timeout
        }
        
        ProcessSystemEvents();
        Delay(0.001);
    }
    
    // Copy result if successful
    if (finalError == SUCCESS) {
        mgr->adapter->copyCommandResult(commandType, result, ctx.result);
    }
    
    // Clean up our resources
    CmtDiscardLock(ctx.completionEvent);
    mgr->adapter->freeCommandResult(commandType, ctx.result);
    
    // Release our reference
    Command_Release(mgr, cmd);
    
    return finalError;
}

DeviceCommandID DeviceQueue_CommandAsync(DeviceQueueManager *mgr, int commandType,
                                       void *params, DevicePriority priority,
                                       DeviceCommandCallback callback, void *userData) {
    if (!mgr) return 0;
    
    // Create command
    QueuedCommand *cmd = Command_Create(mgr, commandType, params);
    if (!cmd) return 0;
    
    cmd->priority = priority;
    cmd->callback = callback;
    cmd->userData = userData;
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case DEVICE_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case DEVICE_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case DEVICE_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) <= 0) {
        LogErrorEx(mgr->logDevice, "Failed to enqueue async command type %s", 
                 mgr->adapter->getCommandTypeName(commandType));
        Command_Release(mgr, cmd);
        return 0;
    }
    
    return cmd->id;
}

bool DeviceQueue_HasCommandType(DeviceQueueManager *mgr, int commandType) {
    if (!mgr) return false;
    
    // Simple implementation - just check if there are items in queues
    // TODO: Implement proper scanning of queue for command type
    int count = 0;
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &count);
    return count > 0;
}

int DeviceQueue_CancelAll(DeviceQueueManager *mgr) {
    if (!mgr) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    QueuedCommand *cmd = NULL;
    
    CmtGetLock(mgr->queueManipulationLock);
    
    // Cancel all commands in high priority queue
    while (CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    // Cancel all commands in normal priority queue
    while (CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    // Cancel all commands in low priority queue
    while (CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    CmtReleaseLock(mgr->queueManipulationLock);
    
    if (totalCancelled > 0) {
        LogMessageEx(mgr->logDevice, "Cancelled %d pending commands", totalCancelled);
    }
    
    return SUCCESS;
}

int DeviceQueue_CancelCommand(DeviceQueueManager *mgr, DeviceCommandID cmdId) {
    if (!mgr || cmdId == 0) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    
    CmtGetLock(mgr->queueManipulationLock);
    
    // Search and cancel in all queues
    totalCancelled += CancelCommandInQueue(mgr->highPriorityQueue, cmdId, mgr);
    totalCancelled += CancelCommandInQueue(mgr->normalPriorityQueue, cmdId, mgr);
    totalCancelled += CancelCommandInQueue(mgr->lowPriorityQueue, cmdId, mgr);
    
    CmtReleaseLock(mgr->queueManipulationLock);
    
    if (totalCancelled > 0) {
        LogDebugEx(mgr->logDevice, "Cancelled command ID %u", cmdId);
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;
}

static int CancelCommandInQueue(CmtTSQHandle queue, DeviceCommandID cmdId, DeviceQueueManager *mgr) {
    int queueSize = 0;
    CmtGetTSQAttribute(queue, ATTR_TSQ_ITEMS_IN_QUEUE, &queueSize);
    if (queueSize == 0) return 0;
    
    QueuedCommand **commands = calloc(queueSize, sizeof(QueuedCommand*));
    if (!commands) return 0;
    
    int itemsRead = CmtReadTSQData(queue, commands, queueSize, 0, 0);
    int cancelled = 0;
    
    for (int i = 0; i < itemsRead; i++) {
        if (commands[i] && commands[i]->id == cmdId) {
            Command_Release(mgr, commands[i]);
            commands[i] = NULL;
            cancelled++;
        }
    }
    
    // Write back non-cancelled commands
    if (cancelled > 0) {
        int writeIdx = 0;
        for (int i = 0; i < itemsRead; i++) {
            if (commands[i] != NULL) {
                commands[writeIdx++] = commands[i];
            }
        }
        
        if (writeIdx > 0) {
            CmtWriteTSQData(queue, commands, writeIdx, TSQ_INFINITE_TIMEOUT, NULL);
        }
    } else {
        // Write all back
        CmtWriteTSQData(queue, commands, itemsRead, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    free(commands);
    return cancelled;
}

int DeviceQueue_CancelByType(DeviceQueueManager *mgr, int commandType) {
    if (!mgr) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    
    CmtGetLock(mgr->queueManipulationLock);
    
    totalCancelled += CancelByTypeInQueue(mgr->highPriorityQueue, commandType, mgr);
    totalCancelled += CancelByTypeInQueue(mgr->normalPriorityQueue, commandType, mgr);
    totalCancelled += CancelByTypeInQueue(mgr->lowPriorityQueue, commandType, mgr);
    
    CmtReleaseLock(mgr->queueManipulationLock);
    
    if (totalCancelled > 0) {
        LogMessageEx(mgr->logDevice, "Cancelled %d commands of type %s", 
                   totalCancelled, mgr->adapter->getCommandTypeName(commandType));
    }
    
    return SUCCESS;
}

static int CancelByTypeInQueue(CmtTSQHandle queue, int commandType, DeviceQueueManager *mgr) {
    int queueSize = 0;
    CmtGetTSQAttribute(queue, ATTR_TSQ_ITEMS_IN_QUEUE, &queueSize);
    if (queueSize == 0) return 0;
    
    QueuedCommand **commands = calloc(queueSize, sizeof(QueuedCommand*));
    if (!commands) return 0;
    
    int itemsRead = CmtReadTSQData(queue, commands, queueSize, 0, 0);
    int cancelled = 0;
    
    for (int i = 0; i < itemsRead; i++) {
        if (commands[i] && commands[i]->commandType == commandType) {
            Command_Release(mgr, commands[i]);
            commands[i] = NULL;
            cancelled++;
        }
    }
    
    // Write back non-cancelled commands
    if (cancelled > 0) {
        int writeIdx = 0;
        for (int i = 0; i < itemsRead; i++) {
            if (commands[i] != NULL) {
                commands[writeIdx++] = commands[i];
            }
        }
        
        if (writeIdx > 0) {
            CmtWriteTSQData(queue, commands, writeIdx, TSQ_INFINITE_TIMEOUT, NULL);
        }
    } else {
        CmtWriteTSQData(queue, commands, itemsRead, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    free(commands);
    return cancelled;
}

int DeviceQueue_CancelByAge(DeviceQueueManager *mgr, double ageSeconds) {
    if (!mgr || ageSeconds < 0) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    double currentTime = Timer();
    
    CmtGetLock(mgr->queueManipulationLock);
    
    totalCancelled += CancelByAgeInQueue(mgr->highPriorityQueue, currentTime, ageSeconds, mgr);
    totalCancelled += CancelByAgeInQueue(mgr->normalPriorityQueue, currentTime, ageSeconds, mgr);
    totalCancelled += CancelByAgeInQueue(mgr->lowPriorityQueue, currentTime, ageSeconds, mgr);
    
    CmtReleaseLock(mgr->queueManipulationLock);
    
    if (totalCancelled > 0) {
        LogMessageEx(mgr->logDevice, "Cancelled %d commands older than %.1f seconds", 
                   totalCancelled, ageSeconds);
    }
    
    return SUCCESS;
}

static int CancelByAgeInQueue(CmtTSQHandle queue, double currentTime, double maxAge, 
                             DeviceQueueManager *mgr) {
    int queueSize = 0;
    CmtGetTSQAttribute(queue, ATTR_TSQ_ITEMS_IN_QUEUE, &queueSize);
    if (queueSize == 0) return 0;
    
    QueuedCommand **commands = calloc(queueSize, sizeof(QueuedCommand*));
    if (!commands) return 0;
    
    int itemsRead = CmtReadTSQData(queue, commands, queueSize, 0, 0);
    int cancelled = 0;
    
    for (int i = 0; i < itemsRead; i++) {
        if (commands[i] && (currentTime - commands[i]->timestamp) > maxAge) {
            Command_Release(mgr, commands[i]);
            commands[i] = NULL;
            cancelled++;
        }
    }
    
    // Write back non-cancelled commands
    if (cancelled > 0) {
        int writeIdx = 0;
        for (int i = 0; i < itemsRead; i++) {
            if (commands[i] != NULL) {
                commands[writeIdx++] = commands[i];
            }
        }
        
        if (writeIdx > 0) {
            CmtWriteTSQData(queue, commands, writeIdx, TSQ_INFINITE_TIMEOUT, NULL);
        }
    } else {
        CmtWriteTSQData(queue, commands, itemsRead, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    free(commands);
    return cancelled;
}

int DeviceQueue_CancelTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txnId) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    // Cannot cancel if already executing
    if (txn->executing) {
        CmtReleaseLock(mgr->transactionLock);
        LogWarningEx(mgr->logDevice, "Cannot cancel executing transaction %u", txnId);
        return ERR_INVALID_STATE;
    }
    
    // Remove from active list
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr == txn) {
            ListRemoveItem(mgr->activeTransactions, 0, i);
            break;
        }
    }
    
    // Free commands using reference counting
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            Command_Release(mgr, txn->commands[i]);
        }
    }
    
    // Clean up results if allocated
    if (txn->results) {
        CleanupTransactionResults(mgr, txn);
    }
    
    free(txn);
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessageEx(mgr->logDevice, "Cancelled transaction %u", txnId);
    return SUCCESS;
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

DeviceTransactionHandle DeviceQueue_BeginTransaction(DeviceQueueManager *mgr) {
    if (!mgr) return 0;
    
    CmtGetLock(mgr->transactionLock);
    
    DeviceTransaction *txn = calloc(1, sizeof(DeviceTransaction));
    if (!txn) {
        CmtReleaseLock(mgr->transactionLock);
        return 0;
    }
    
    txn->id = mgr->nextTransactionId++;
    txn->flags = DEVICE_TXN_CONTINUE_ON_ERROR;  // Default behavior
    txn->priority = DEVICE_PRIORITY_HIGH;       // Default priority
    txn->timeoutMs = DEVICE_DEFAULT_TRANSACTION_TIMEOUT_MS;
    
    ListInsertItem(mgr->activeTransactions, &txn, END_OF_LIST);
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(mgr->logDevice, "Thread %d started transaction %u", 
              GetCurrentThreadId(), txn->id);
    return txn->id;
}

int DeviceQueue_SetTransactionFlags(DeviceQueueManager *mgr, 
                                   DeviceTransactionHandle txnId,
                                   DeviceTransactionFlags flags) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->executing) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    txn->flags = flags;
    CmtReleaseLock(mgr->transactionLock);
    
    return SUCCESS;
}

int DeviceQueue_SetTransactionPriority(DeviceQueueManager *mgr,
                                     DeviceTransactionHandle txnId,
                                     DevicePriority priority) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->executing) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    txn->priority = priority;
    CmtReleaseLock(mgr->transactionLock);
    
    return SUCCESS;
}

int DeviceQueue_SetTransactionTimeout(DeviceQueueManager *mgr,
                                    DeviceTransactionHandle txnId,
                                    int timeoutMs) {
    if (!mgr || txnId == 0 || timeoutMs <= 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->executing) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    txn->timeoutMs = timeoutMs;
    CmtReleaseLock(mgr->transactionLock);
    
    return SUCCESS;
}

int DeviceQueue_AddToTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txnId,
                               int commandType, void *params) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->executing) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    if (txn->commandCount >= DEVICE_MAX_TRANSACTION_COMMANDS) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    QueuedCommand *cmd = Command_Create(mgr, commandType, params);
    if (!cmd) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_OUT_OF_MEMORY;
    }
    
    cmd->transactionId = txnId;
    txn->commands[txn->commandCount++] = cmd;
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(mgr->logDevice, "Added %s to transaction %u", 
             mgr->adapter->getCommandTypeName(commandType), txnId);
    return SUCCESS;
}

int DeviceQueue_CommitTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txnId,
                                DeviceTransactionCallback callback, void *userData) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->executing || txn->commandCount == 0) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    // Allocate results array
    txn->results = calloc(txn->commandCount, sizeof(TransactionCommandResult));
    if (!txn->results) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Initialize results
    for (int i = 0; i < txn->commandCount; i++) {
        txn->results[i].commandType = txn->commands[i]->commandType;
        txn->results[i].errorCode = ERR_OPERATION_FAILED;
        txn->results[i].result = mgr->adapter->createCommandResult(txn->commands[i]->commandType);
    }
    
    txn->callback = callback;
    txn->userData = userData;
    txn->committed = true;
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessageEx(mgr->logDevice, "Committed transaction %u with %d commands", 
               txnId, txn->commandCount);
    return SUCCESS;
}

/******************************************************************************
 * Processing Thread Function
 ******************************************************************************/

static int CVICALLBACK ProcessingThreadFunction(void *functionData) {
    DeviceQueueManager *mgr = (DeviceQueueManager*)functionData;
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread started", mgr->adapter->deviceName);
    
    while (1) {
        // Check if we should exit - but ONLY if all queues are empty
        if (mgr->shutdownRequested) {
            int highCount = 0, normalCount = 0, lowCount = 0;
            
            CmtGetTSQAttribute(mgr->highPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &highCount);
            CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &normalCount);
            CmtGetTSQAttribute(mgr->lowPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &lowCount);
            
            if (highCount == 0 && normalCount == 0 && lowCount == 0) {
                LogMessageEx(mgr->logDevice, "All queues empty, processing thread exiting");
                break;
            }
        }
        
        // Check connection state (skip reconnection attempts during shutdown)
        if (!mgr->isConnected && !mgr->shutdownRequested) {
            if (Timer() - mgr->lastReconnectTime > (DEVICE_QUEUE_RECONNECT_DELAY_MS / 1000.0)) {
                AttemptReconnection(mgr);
            }
            Delay(0.1);
            continue;
        }
        
        // Check for ready transactions first (skip during shutdown)
        if (!mgr->shutdownRequested) {
            CmtGetLock(mgr->transactionLock);
            DeviceTransaction *readyTxn = GetNextReadyTransaction(mgr);
            if (readyTxn && !mgr->inTransactionMode) {
                mgr->inTransactionMode = 1;
                mgr->activeTransactionHandle = readyTxn->id;
                readyTxn->executing = true;
                CmtReleaseLock(mgr->transactionLock);
                
                LogMessageEx(mgr->logDevice, "Entering transaction mode for transaction %u", readyTxn->id);
                ProcessTransaction(mgr, readyTxn);
                
                CmtGetLock(mgr->transactionLock);
                mgr->inTransactionMode = 0;
                mgr->activeTransactionHandle = 0;
                CmtReleaseLock(mgr->transactionLock);
                
                LogMessageEx(mgr->logDevice, "Exited transaction mode");
                continue;
            }
            CmtReleaseLock(mgr->transactionLock);
        }
        
        // Process regular commands
        CmtGetLock(mgr->transactionLock);
        bool canProcessCommands = !mgr->inTransactionMode || mgr->shutdownRequested;
        CmtReleaseLock(mgr->transactionLock);
        
        if (canProcessCommands) {
            QueuedCommand *cmd = NULL;
            int itemsRead = 0;
            
            // Check queues in priority order
            itemsRead = CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, 0);
            if (itemsRead <= 0) {
                itemsRead = CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, 0);
                if (itemsRead <= 0) {
                    itemsRead = CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, 0);
                }
            }
            
            if (itemsRead > 0 && cmd) {
                // During shutdown, just release the command
                if (mgr->shutdownRequested) {
                    LogDebugEx(mgr->logDevice, "Releasing command %u during shutdown", cmd->id);
                    Command_Release(mgr, cmd);
                    continue;
                }
                
                // Process the command
                CmtGetLock(mgr->currentCommandLock);
                mgr->currentCommand = cmd;
                CmtReleaseLock(mgr->currentCommandLock);
                
                ProcessCommand(mgr, cmd);
                
                CmtGetLock(mgr->currentCommandLock);
                mgr->currentCommand = NULL;
                CmtReleaseLock(mgr->currentCommandLock);
                
                // Release queue's reference
                Command_Release(mgr, cmd);
            } else {
                Delay(0.01);
            }
        } else {
            Delay(0.01);
        }
    }
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread stopped", mgr->adapter->deviceName);
    return 0;
}

static int ProcessCommand(DeviceQueueManager *mgr, QueuedCommand *cmd) {
    cmd->state = CMD_STATE_EXECUTING;
    
    LogDebugEx(mgr->logDevice, "Processing command: %s (ID: %u)", 
             mgr->adapter->getCommandTypeName(cmd->commandType), cmd->id);
    
    // Prepare result storage
    void *result = NULL;
    BlockingContext *blockingCtx = (BlockingContext*)cmd->blockingContext;
    
    if (blockingCtx) {
        // Use pre-allocated result from blocking context
        result = blockingCtx->result;
    } else {
        // Create result for async command
        result = mgr->adapter->createCommandResult(cmd->commandType);
        if (!result) {
            // Notify async callback of failure
            if (cmd->callback) {
                cmd->callback(cmd->id, cmd->commandType, NULL, cmd->userData);
            }
            return ERR_OUT_OF_MEMORY;
        }
    }
    
    // Execute the command
    int errorCode = ExecuteDeviceCommand(mgr, cmd, result);
    
    // Update statistics
    CmtGetLock(mgr->statsLock);
    mgr->totalProcessed++;
    if (errorCode != SUCCESS) {
        mgr->totalErrors++;
    }
    CmtReleaseLock(mgr->statsLock);
    
    // Handle connection loss
    if (errorCode == ERR_COMM_FAILED || errorCode == ERR_TIMEOUT) {
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
        LogWarningEx(mgr->logDevice, "Lost connection during command execution");
    }
    
    // Mark as completed
    cmd->state = CMD_STATE_COMPLETED;
    
    // Notify completion
    if (blockingCtx) {
        // Signal blocking thread
        blockingCtx->errorCode = errorCode;
        blockingCtx->completed = 1;
        // The blocking thread will handle result cleanup
    } else {
        // Notify async callback
        if (cmd->callback) {
            cmd->callback(cmd->id, cmd->commandType, result, cmd->userData);
        }
        // Free async result
        mgr->adapter->freeCommandResult(cmd->commandType, result);
    }
    
    // Apply command-specific delay
    if (mgr->adapter->getCommandDelay) {
        int delayMs = mgr->adapter->getCommandDelay(cmd->commandType);
        if (delayMs > 0) {
            Delay(delayMs / 1000.0);
        }
    }
    
    return errorCode;
}

/******************************************************************************
 * Internal Processing Functions
 ******************************************************************************/

static DeviceTransaction* GetNextReadyTransaction(DeviceQueueManager *mgr) {
    // Must be called with transaction lock held
    int count = ListNumItems(mgr->activeTransactions);
    
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr) {
            DeviceTransaction *txn = *txnPtr;
            if (txn->committed && !txn->executing) {
                return txn;
            }
        }
    }
    
    return NULL;
}

static int ProcessTransaction(DeviceQueueManager *mgr, DeviceTransaction *txn) {
    LogMessageEx(mgr->logDevice, "Processing transaction %u with %d commands", 
               txn->id, txn->commandCount);
    
    txn->startTime = Timer();
    txn->successCount = 0;
    txn->failureCount = 0;
    
    // Process each command in sequence
    for (int i = 0; i < txn->commandCount; i++) {
        // Check for shutdown
        if (mgr->shutdownRequested) {
            LogWarningEx(mgr->logDevice, "Transaction %u interrupted by shutdown", txn->id);
            
            // Mark remaining commands as cancelled
            for (int j = i; j < txn->commandCount; j++) {
                txn->results[j].errorCode = ERR_CANCELLED;
                txn->failureCount++;
            }
            break;
        }
        
        // Check timeout
        if ((Timer() - txn->startTime) * 1000.0 > txn->timeoutMs) {
            LogWarningEx(mgr->logDevice, "Transaction %u timed out after %d ms", 
                       txn->id, (int)((Timer() - txn->startTime) * 1000.0));
            
            // Mark remaining commands as timed out
            for (int j = i; j < txn->commandCount; j++) {
                txn->results[j].errorCode = ERR_TIMEOUT;
                txn->failureCount++;
            }
            break;
        }
        
        QueuedCommand *cmd = txn->commands[i];
        void *result = txn->results[i].result;
        
        LogDebugEx(mgr->logDevice, "Transaction %u: Executing command %d/%d: %s", 
                 txn->id, i + 1, txn->commandCount,
                 mgr->adapter->getCommandTypeName(cmd->commandType));
        
        // Execute the command
        int errorCode = ExecuteDeviceCommand(mgr, cmd, result);
        
        // Store result
        txn->results[i].errorCode = errorCode;
        
        if (errorCode == SUCCESS) {
            txn->successCount++;
        } else {
            txn->failureCount++;
            
            // Check if we should abort on error
            if (txn->flags & DEVICE_TXN_ABORT_ON_ERROR) {
                LogWarningEx(mgr->logDevice, "Transaction %u aborted after command %d failed", 
                           txn->id, i + 1);
                
                // Mark remaining commands as not executed
                for (int j = i + 1; j < txn->commandCount; j++) {
                    txn->results[j].errorCode = ERR_CANCELLED;
                    txn->failureCount++;
                }
                break;
            }
        }
        
        // Update statistics
        CmtGetLock(mgr->statsLock);
        mgr->totalProcessed++;
        if (errorCode != SUCCESS) {
            mgr->totalErrors++;
        }
        CmtReleaseLock(mgr->statsLock);
        
        // Apply command-specific delay
        if (mgr->adapter->getCommandDelay) {
            int delayMs = mgr->adapter->getCommandDelay(cmd->commandType);
            if (delayMs > 0) {
                Delay(delayMs / 1000.0);
            }
        }
    }
    
    LogMessageEx(mgr->logDevice, "Transaction %u completed: %d success, %d failed", 
               txn->id, txn->successCount, txn->failureCount);
    
    // Call transaction callback
    if (txn->callback) {
        txn->callback(txn->id, txn->successCount, txn->failureCount, 
                     txn->results, txn->commandCount, txn->userData);
    }
    
    // Remove from active list and clean up
    CmtGetLock(mgr->transactionLock);
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr == txn) {
            ListRemoveItem(mgr->activeTransactions, 0, i);
            break;
        }
    }
    CmtReleaseLock(mgr->transactionLock);
    
    // Free commands and results
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            Command_Release(mgr, txn->commands[i]);
        }
    }
    
    CleanupTransactionResults(mgr, txn);
    free(txn);
    
    return SUCCESS;
}

static void CleanupTransactionResults(DeviceQueueManager *mgr, DeviceTransaction *txn) {
    if (!txn->results) return;
    
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->results[i].result && mgr->adapter->freeCommandResult) {
            // Use stored commandType from results instead of accessing commands
            mgr->adapter->freeCommandResult(txn->results[i].commandType, 
                                          txn->results[i].result);
        }
    }
    
    free(txn->results);
    txn->results = NULL;
}

static int ExecuteDeviceCommand(DeviceQueueManager *mgr, QueuedCommand *cmd, void *result) {
    if (!mgr->adapter->executeCommand) {
        return ERR_OPERATION_FAILED;
    }
    
    return mgr->adapter->executeCommand(mgr->deviceContext, 
                                      cmd->commandType, 
                                      cmd->params, 
                                      result);
}

static int AttemptReconnection(DeviceQueueManager *mgr) {
    LogMessageEx(mgr->logDevice, "Attempting to reconnect to %s...", mgr->adapter->deviceName);
    
    mgr->reconnectAttempts++;
    
    // Try to reconnect
    int result = ConnectDevice(mgr);
    
    if (result == SUCCESS) {
        mgr->isConnected = 1;
        mgr->reconnectAttempts = 0;
        LogMessageEx(mgr->logDevice, "Successfully reconnected to %s", mgr->adapter->deviceName);
        return SUCCESS;
    }
    
    // Calculate next retry delay with exponential backoff
    double delay = DEVICE_QUEUE_RECONNECT_DELAY_MS * pow(2, MIN(mgr->reconnectAttempts - 1, 5));
    delay = MIN(delay, DEVICE_QUEUE_MAX_RECONNECT_DELAY);
    mgr->lastReconnectTime = Timer() + (delay / 1000.0);
    
    LogWarningEx(mgr->logDevice, "Reconnection failed, next attempt in %.1f seconds", delay / 1000.0);
    return ERR_COMM_FAILED;
}

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/
static QueuedCommand* Command_Create(DeviceQueueManager *mgr, int commandType, void *params) {
    CmtGetLock(mgr->commandLock);
    DeviceCommandID cmdId = mgr->nextCommandId++;
    CmtReleaseLock(mgr->commandLock);
    
    QueuedCommand *cmd = calloc(1, sizeof(QueuedCommand));
    if (!cmd) return NULL;
    
    cmd->id = cmdId;
    cmd->commandType = commandType;
    cmd->timestamp = Timer();
    cmd->state = CMD_STATE_QUEUED;
    cmd->refCount = 1;  // Initial reference
    
    // Create reference lock
    if (CmtNewLock(NULL, 0, &cmd->refLock) < 0) {
        free(cmd);
        return NULL;
    }
    
    // Create device-specific parameter copy
    if (params && mgr->adapter->createCommandParams) {
        cmd->params = mgr->adapter->createCommandParams(commandType, params);
        if (!cmd->params) {
            CmtDiscardLock(cmd->refLock);
            free(cmd);
            return NULL;
        }
    }
    
    return cmd;
}

static void Command_AddRef(QueuedCommand *cmd) {
    if (!cmd) return;
    
    CmtGetLock(cmd->refLock);
    cmd->refCount++;
    CmtReleaseLock(cmd->refLock);
}

static void Command_Release(DeviceQueueManager *mgr, QueuedCommand *cmd) {
    if (!cmd) return;
    
    CmtGetLock(cmd->refLock);
    int newCount = --cmd->refCount;
    CmtReleaseLock(cmd->refLock);
    
    if (newCount == 0) {
        // Last reference - free the command
        CmtDiscardLock(cmd->refLock);
        
        // Free parameters
        if (cmd->params && mgr->adapter && mgr->adapter->freeCommandParams) {
            mgr->adapter->freeCommandParams(cmd->commandType, cmd->params);
        }
        
        free(cmd);
    }
}

static DeviceTransaction* FindTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle id) {
    // Must be called with transaction lock held
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr && (*txnPtr)->id == id) {
            return *txnPtr;
        }
    }
    return NULL;
}

static bool ValidateAdapter(const DeviceAdapter *adapter) {
    if (!adapter) return false;
    
    // Check required functions
    if (!adapter->deviceName) return false;
    if (!adapter->executeCommand) return false;
    if (!adapter->createCommandResult) return false;
    if (!adapter->freeCommandResult) return false;
    if (!adapter->copyCommandResult) return false;
    if (!adapter->getCommandTypeName) return false;
    
    // Connection functions are optional but must be consistent
    if (adapter->disconnect && !adapter->isConnected) return false;
    
    return true;
}