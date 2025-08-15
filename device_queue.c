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
    
    // Configuration
    DeviceTransactionFlags flags;
    DevicePriority priority;
    int timeoutMs;
    
    // For tracking execution
    double startTime;
} DeviceTransaction;

// Queue manager structure
struct DeviceQueueManager {
    // Device adapter and context
    const DeviceAdapter *adapter;
    void *deviceContext;
    void *connectionParams;
    
    // Thread pool for processing
    CmtThreadPoolHandle threadPool;
    
    // Thread-safe queues
    CmtTSQHandle highPriorityQueue;
    CmtTSQHandle normalPriorityQueue;
    CmtTSQHandle lowPriorityQueue;
    CmtTSQHandle deferredCommandQueue;  // Commands deferred during transactions
    
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
    
    // Transaction tracking
    ListType uncommittedTransactions;      // List of uncommitted DeviceTransaction*
    CmtThreadLockHandle transactionLock;
    
    // Current transaction state (for processing thread)
    volatile DeviceTransactionHandle currentTransactionId;
    CmtTSQHandle currentTransactionQueue;
    
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

// Command enqueuing helper
static int EnqueueCommand(DeviceQueueManager *mgr, QueuedCommand *cmd, DevicePriority priority, int timeoutMs);

// Processing thread
static int CVICALLBACK ProcessingThreadFunction(void *functionData);
static int ExecuteDeviceCommand(DeviceQueueManager *mgr, QueuedCommand *cmd, void *result);

// Connection management
static int ConnectDevice(DeviceQueueManager *mgr);
static void DisconnectDevice(DeviceQueueManager *mgr);
static int AttemptReconnection(DeviceQueueManager *mgr);

// Transaction management
static DeviceTransaction* FindTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle id);

// Queue rebuilding for deferred commands
static void RebuildQueueWithDeferredCommands(DeviceQueueManager *mgr, CmtTSQHandle targetQueue);

// Cancellation helpers
static int CancelCommandInQueue(CmtTSQHandle queue, DeviceCommandID cmdId, DeviceQueueManager *mgr);
static int CancelByTypeInQueue(CmtTSQHandle queue, int commandType, DeviceQueueManager *mgr);
static int CancelByAgeInQueue(CmtTSQHandle queue, double currentTime, double maxAge, DeviceQueueManager *mgr);
static int CancelCommandsWithTransactionId(CmtTSQHandle queue, DeviceTransactionHandle txnId, 
                                         DeviceQueueManager *mgr);

// Validation
static bool ValidateAdapter(const DeviceAdapter *adapter);

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

DeviceQueueManager* DeviceQueue_Create(const DeviceAdapter *adapter, 
                                     void *deviceContext,
                                     void *connectionParams,
                                     CmtThreadPoolHandle threadPool) {
    if (!adapter || !deviceContext) {
        LogError("DeviceQueue_Create: Invalid parameters");
        return NULL;
    }
    
    // Validate adapter has required functions
    if (!ValidateAdapter(adapter)) {
        LogError("DeviceQueue_Create: Invalid adapter - missing required functions");
        return NULL;
    }
    
    // If threadPool is unspecified use g_threadPool
    if (!threadPool) {
        threadPool = g_threadPool;
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
    mgr->currentCommand = NULL;
    
    // Create queues
    int error = 0;
    error |= CmtNewTSQ(DEVICE_QUEUE_HIGH_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->highPriorityQueue);
    error |= CmtNewTSQ(DEVICE_QUEUE_NORMAL_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->normalPriorityQueue);
    error |= CmtNewTSQ(DEVICE_QUEUE_LOW_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->lowPriorityQueue);
    error |= CmtNewTSQ(DEVICE_QUEUE_DEFERRED_SIZE, sizeof(QueuedCommand*), 0, &mgr->deferredCommandQueue);
    
    if (error < 0) {
        LogError("DeviceQueue_Create: Failed to create queues");
        DeviceQueue_Destroy(mgr);
        return NULL;
    }
    
    // Create locks
    CmtNewLock(NULL, 0, &mgr->commandLock);
    CmtNewLock(NULL, 0, &mgr->transactionLock);
    CmtNewLock(NULL, 0, &mgr->statsLock);
    CmtNewLock(NULL, 0, &mgr->currentCommandLock);
    CmtNewLock(NULL, 0, &mgr->queueManipulationLock);
    
    // Initialize transaction list (only for uncommitted transactions)
    mgr->uncommittedTransactions = ListCreate(sizeof(DeviceTransaction*));
    
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
    if (threadPool > 0) {
        mgr->threadPool = threadPool;
        error = CmtScheduleThreadPoolFunction(threadPool, ProcessingThreadFunction, 
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
        
        CmtWaitForThreadPoolFunctionCompletion(mgr->threadPool, mgr->processingThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // At this point, all queues should be empty
    // Verify and log if not
    int highCount = 0, normalCount = 0, lowCount = 0, deferredCount = 0;
    CmtGetTSQAttribute(mgr->highPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &highCount);
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &normalCount);
    CmtGetTSQAttribute(mgr->lowPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &lowCount);
    CmtGetTSQAttribute(mgr->deferredCommandQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &deferredCount);
    
    if (highCount + normalCount + lowCount + deferredCount > 0) {
        LogWarningEx(mgr->logDevice, "Queues not empty after shutdown: high=%d, normal=%d, low=%d, deferred=%d",
                   highCount, normalCount, lowCount, deferredCount);
    }
    
    // Disconnect device
    DisconnectDevice(mgr);
    
    // Clean up uncommitted transactions
    if (mgr->uncommittedTransactions) {
        CmtGetLock(mgr->transactionLock);
        int count = ListNumItems(mgr->uncommittedTransactions);
        
        for (int i = count; i >= 1; i--) {
            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->uncommittedTransactions, i);
            if (txnPtr && *txnPtr) {
                DeviceTransaction *txn = *txnPtr;
                
                // Free transaction commands
                for (int j = 0; j < txn->commandCount; j++) {
                    if (txn->commands[j]) {
                        Command_Release(mgr, txn->commands[j]);
                    }
                }
                
                free(txn);
            }
        }
        
        CmtReleaseLock(mgr->transactionLock);
        ListDispose(mgr->uncommittedTransactions);
    }
    
    // Dispose queues
    if (mgr->highPriorityQueue) CmtDiscardTSQ(mgr->highPriorityQueue);
    if (mgr->normalPriorityQueue) CmtDiscardTSQ(mgr->normalPriorityQueue);
    if (mgr->lowPriorityQueue) CmtDiscardTSQ(mgr->lowPriorityQueue);
    if (mgr->deferredCommandQueue) CmtDiscardTSQ(mgr->deferredCommandQueue);
    
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
    CmtGetTSQAttribute(mgr->deferredCommandQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &stats->deferredQueued);
    
    CmtGetLock(mgr->statsLock);
    stats->totalProcessed = mgr->totalProcessed;
    stats->totalErrors = mgr->totalErrors;
    stats->reconnectAttempts = mgr->reconnectAttempts;
    CmtReleaseLock(mgr->statsLock);
    
    stats->isConnected = mgr->isConnected;
    stats->isProcessing = (mgr->processingThreadId != 0);
    
    stats->activeTransactionId = mgr->currentTransactionId;
    stats->isInTransactionMode = (mgr->currentTransactionId != 0);
}

void DeviceQueue_SetLogDevice(DeviceQueueManager *mgr, LogDevice device) {
    if (mgr) {
        mgr->logDevice = device;
    }
}

bool DeviceQueue_IsInTransaction(DeviceQueueManager *mgr) {
    if (!mgr) return false;
    
    CmtGetLock(mgr->currentCommandLock);
    bool inTransaction = (mgr->currentCommand && mgr->currentCommand->transactionId != 0);
    CmtReleaseLock(mgr->currentCommandLock);
    
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
 * Command Enqueuing Helper
 ******************************************************************************/

static int EnqueueCommand(DeviceQueueManager *mgr, QueuedCommand *cmd, DevicePriority priority, int timeoutMs) {
    if (!mgr || !cmd || mgr->shutdownRequested) {
        return mgr && mgr->shutdownRequested ? ERR_CANCELLED : ERR_INVALID_PARAMETER;
    }
    
    // Select queue based on priority
    CmtTSQHandle queue;
    const char *queueName;
    switch (priority) {
        case DEVICE_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; queueName = "high"; break;
        case DEVICE_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; queueName = "normal"; break;
        case DEVICE_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; queueName = "low"; break;
        default:                     queue = mgr->normalPriorityQueue; queueName = "normal"; break;
    }
    
    double startTime = Timer();
    double totalTimeout = (timeoutMs > 0) ? (timeoutMs / 1000.0) : -1.0;
    
    for (int attempt = 0; attempt <= DEVICE_QUEUE_MAX_RETRIES; attempt++) {
        // Check timeout and shutdown
        if (mgr->shutdownRequested) return ERR_CANCELLED;
        if (totalTimeout > 0 && (Timer() - startTime) >= totalTimeout) return ERR_TIMEOUT;
        
        // Calculate attempt timeout
        int attemptTimeout = TSQ_INFINITE_TIMEOUT;
        if (timeoutMs >= 0) {
            if (totalTimeout > 0) {
                double remaining = totalTimeout - (Timer() - startTime);
                attemptTimeout = (remaining > 0) ? MIN((int)(remaining * 1000), DEVICE_QUEUE_MAX_ATTEMPT_TIMEOUT_MS) : 0;
                if (attemptTimeout <= 0) return ERR_TIMEOUT;
            } else {
                attemptTimeout = timeoutMs;
            }
        }
        
        // Attempt enqueue with lock coordination
        CmtGetLock(mgr->queueManipulationLock);
        if (mgr->shutdownRequested) {
            CmtReleaseLock(mgr->queueManipulationLock);
            return ERR_CANCELLED;
        }
        
        int itemsWritten = CmtWriteTSQData(queue, &cmd, 1, attemptTimeout, NULL);
        CmtReleaseLock(mgr->queueManipulationLock);
        
        if (itemsWritten == 1) {
            if (attempt > 0) {
                LogDebugEx(mgr->logDevice, "Enqueued command %u to %s queue after %d retries", 
                         cmd->id, queueName, attempt);
            }
            return SUCCESS;
        }
        
        // Handle failure
        int errorCode = (itemsWritten == 0) ? ERR_TIMEOUT : ERR_QUEUE_FULL;
        if (attempt >= DEVICE_QUEUE_MAX_RETRIES) {
            LogErrorEx(mgr->logDevice, "Failed to enqueue command type %s after %d attempts", 
                     mgr->adapter->getCommandTypeName(cmd->commandType), attempt + 1);
            return errorCode;
        }
        
        // Calculate backoff delay with jitter
        int delayMs = DEVICE_QUEUE_BASE_RETRY_DELAY_MS << attempt;
        delayMs = MIN(delayMs, DEVICE_QUEUE_MAX_RETRY_DELAY_MS);
        delayMs += (rand() % (delayMs / 2 + 1)) - (delayMs / 4);  // ±25% jitter
        delayMs = MAX(delayMs, 1);
        
        // Check if we have time for delay
        if (totalTimeout > 0 && (Timer() - startTime + delayMs/1000.0) >= totalTimeout) {
            return ERR_TIMEOUT;
        }
        
        LogDebugEx(mgr->logDevice, "Retrying command %u in %dms (attempt %d/%d)", 
                 cmd->id, delayMs, attempt + 1, DEVICE_QUEUE_MAX_RETRIES + 1);
        Delay(delayMs / 1000.0);
    }
    
    return ERR_OPERATION_FAILED;
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
    
    // Enqueue command with retry logic
    int enqueueResult = EnqueueCommand(mgr, cmd, priority, timeoutMs);
    if (enqueueResult != SUCCESS) {
        // Clean up
        Command_Release(mgr, cmd);  // Release queue's reference
        CmtDiscardLock(ctx.completionEvent);
        mgr->adapter->freeCommandResult(commandType, ctx.result);
        Command_Release(mgr, cmd);  // Release our reference
        
        return enqueueResult;
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
        
        if (mgr->shutdownRequested) {
            finalError = ERR_CANCELLED;
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
    
    // Enqueue command with retry logic
    int enqueueResult = EnqueueCommand(mgr, cmd, priority, -1);  // -1 = infinite timeout
    if (enqueueResult != SUCCESS) {
        Command_Release(mgr, cmd);
        return 0;
    }
    
    return cmd->id;
}

int DeviceQueue_CancelAll(DeviceQueueManager *mgr) {
    if (!mgr) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    QueuedCommand *cmd = NULL;
    
    CmtGetLock(mgr->queueManipulationLock);
    
    // Cancel all commands in all queues
    while (CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    while (CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    while (CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, 0) > 0) {
        if (cmd) {
            Command_Release(mgr, cmd);
            totalCancelled++;
        }
    }
    
    while (CmtReadTSQData(mgr->deferredCommandQueue, &cmd, 1, 0, 0) > 0) {
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
    totalCancelled += CancelCommandInQueue(mgr->deferredCommandQueue, cmdId, mgr);
    
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
    totalCancelled += CancelByTypeInQueue(mgr->deferredCommandQueue, commandType, mgr);
    
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
    totalCancelled += CancelByAgeInQueue(mgr->deferredCommandQueue, currentTime, ageSeconds, mgr);
    
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
    
    if (txn->committed) {
        // Transaction already committed - need to cancel queued commands
        CmtReleaseLock(mgr->transactionLock);
        
        // Cancel commands in all queues with this transaction ID
        int cancelled = 0;
        CmtGetLock(mgr->queueManipulationLock);
        
        cancelled += CancelCommandsWithTransactionId(mgr->highPriorityQueue, txnId, mgr);
        cancelled += CancelCommandsWithTransactionId(mgr->normalPriorityQueue, txnId, mgr);
        cancelled += CancelCommandsWithTransactionId(mgr->lowPriorityQueue, txnId, mgr);
        cancelled += CancelCommandsWithTransactionId(mgr->deferredCommandQueue, txnId, mgr);
        
        CmtReleaseLock(mgr->queueManipulationLock);
        
        LogMessageEx(mgr->logDevice, "Cancelled %d commands from transaction %u", 
                   cancelled, txnId);
        return cancelled > 0 ? SUCCESS : ERR_OPERATION_FAILED;
    } else {
        // Transaction not committed - just remove from list
        int count = ListNumItems(mgr->uncommittedTransactions);
        for (int i = 1; i <= count; i++) {
            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->uncommittedTransactions, i);
            if (txnPtr && *txnPtr == txn) {
                ListRemoveItem(mgr->uncommittedTransactions, 0, i);
                break;
            }
        }
        
        // Free commands
        for (int i = 0; i < txn->commandCount; i++) {
            if (txn->commands[i]) {
                Command_Release(mgr, txn->commands[i]);
            }
        }
        
        free(txn);
        CmtReleaseLock(mgr->transactionLock);
        
        LogMessageEx(mgr->logDevice, "Cancelled uncommitted transaction %u", txnId);
        return SUCCESS;
    }
}

static int CancelCommandsWithTransactionId(CmtTSQHandle queue, DeviceTransactionHandle txnId, 
                                         DeviceQueueManager *mgr) {
    int queueSize = 0;
    CmtGetTSQAttribute(queue, ATTR_TSQ_ITEMS_IN_QUEUE, &queueSize);
    if (queueSize == 0) return 0;
    
    QueuedCommand **commands = calloc(queueSize, sizeof(QueuedCommand*));
    if (!commands) return 0;
    
    int itemsRead = CmtReadTSQData(queue, commands, queueSize, 0, 0);
    int cancelled = 0;
    
    for (int i = 0; i < itemsRead; i++) {
        if (commands[i] && commands[i]->transactionId == txnId) {
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
    
    ListInsertItem(mgr->uncommittedTransactions, &txn, END_OF_LIST);
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
    if (!txn || txn->committed) {
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
    if (!txn || txn->committed) {
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
    if (!txn || txn->committed) {
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
    if (!txn || txn->committed) {
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
    if (!txn || txn->committed || txn->commandCount == 0) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_STATE;
    }
    
    // Select queue based on transaction priority
    CmtTSQHandle queue;
    switch (txn->priority) {
        case DEVICE_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case DEVICE_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case DEVICE_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Store transaction info for later use
    txn->callback = callback;
    txn->userData = userData;
    
    // Store transaction info in each command
    for (int i = 0; i < txn->commandCount; i++) {
        txn->commands[i]->transactionId = txnId;
        txn->commands[i]->priority = txn->priority;
        
        // Store transaction pointer in first command for later retrieval
        if (i == 0) {
            txn->commands[i]->blockingContext = (void*)txn;
        }
        
        // Add reference for the queue
        Command_AddRef(txn->commands[i]);
    }
    
    // Queue all commands at once
    int itemsWritten = CmtWriteTSQData(queue, txn->commands, txn->commandCount, 
                                       TSQ_INFINITE_TIMEOUT, NULL);
    
    if (itemsWritten != txn->commandCount) {
        // Failed to queue all commands - clean up
        for (int i = 0; i < txn->commandCount; i++) {
            Command_Release(mgr, txn->commands[i]);
        }
        CmtReleaseLock(mgr->transactionLock);
        LogError("Failed to queue transaction %u commands", txnId);
        return ERR_QUEUE_FULL;
    }
    
    txn->committed = true;
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessage("Committed transaction %u with %d commands to %s priority queue", 
               txnId, txn->commandCount,
               txn->priority == DEVICE_PRIORITY_HIGH ? "high" :
               txn->priority == DEVICE_PRIORITY_NORMAL ? "normal" : "low");
    
    return SUCCESS;
}

/******************************************************************************
 * Processing Thread Function
 ******************************************************************************/

static int CVICALLBACK ProcessingThreadFunction(void *functionData) {
    DeviceQueueManager *mgr = (DeviceQueueManager*)functionData;
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread started", mgr->adapter->deviceName);
    
    // Transaction execution state
    DeviceTransaction *currentTransaction = NULL;
    TransactionCommandResult *transactionResults = NULL;
    int transactionCommandIndex = 0;
    int expectedTransactionCommands = 0;
    
    while (1) {
        // Check if we should exit - but ONLY if all queues are empty
        if (mgr->shutdownRequested) {
            int highCount = 0, normalCount = 0, lowCount = 0, deferredCount = 0;
            
            CmtGetTSQAttribute(mgr->highPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &highCount);
            CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &normalCount);
            CmtGetTSQAttribute(mgr->lowPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &lowCount);
            CmtGetTSQAttribute(mgr->deferredCommandQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &deferredCount);
            
            if (highCount == 0 && normalCount == 0 && lowCount == 0 && deferredCount == 0) {
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
        
        QueuedCommand *cmd = NULL;
        CmtTSQHandle cmdQueue = 0;
        
        // Get next command based on current state
        if (mgr->currentTransactionId != 0 && mgr->currentTransactionQueue != 0) {
            // In transaction mode - only read from current transaction queue
            int itemsRead = CmtReadTSQData(mgr->currentTransactionQueue, &cmd, 1, 0, 0);
            if (itemsRead > 0) {
                cmdQueue = mgr->currentTransactionQueue;
                
                // Check if this command is part of current transaction
                if (cmd->transactionId != mgr->currentTransactionId) {
                    // Different transaction or non-transaction command - defer it
                    LogDebugEx(mgr->logDevice, "Deferring command %u (transaction %u) while processing transaction %u",
                             cmd->id, cmd->transactionId, mgr->currentTransactionId);
                    
                    // Add to deferred queue
                    if (CmtWriteTSQData(mgr->deferredCommandQueue, &cmd, 1, 0, NULL) <= 0) {
                        LogWarningEx(mgr->logDevice, "Deferred queue full - dropping command %u", cmd->id);
                        Command_Release(mgr, cmd);
                    }
                    cmd = NULL;
                }
            }
            
            // If no more transaction commands, complete the transaction
            if (!cmd) {
                mgr->currentTransactionId = 0;
                mgr->currentTransactionQueue = 0;
                
                // Rebuild the transaction source queue with deferred commands
                RebuildQueueWithDeferredCommands(mgr, cmdQueue);
            }
        } else {
            // Normal mode - check all queues in priority order
            CmtGetLock(mgr->queueManipulationLock);
            
            int itemsRead = CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, 0);
            if (itemsRead > 0) {
                cmdQueue = mgr->highPriorityQueue;
            } else {
                itemsRead = CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, 0);
                if (itemsRead > 0) {
                    cmdQueue = mgr->normalPriorityQueue;
                } else {
                    itemsRead = CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, 0);
                    if (itemsRead > 0) {
                        cmdQueue = mgr->lowPriorityQueue;
                    }
                }
            }
            
            CmtReleaseLock(mgr->queueManipulationLock);
        }
        
        // Process command if we have one
        if (cmd) {
            // During shutdown, just release the command
            if (mgr->shutdownRequested) {
                LogDebugEx(mgr->logDevice, "Releasing command %u during shutdown", cmd->id);
                
                // For blocking commands, signal cancellation
                BlockingContext *blockingCtx = (BlockingContext*)cmd->blockingContext;
                if (blockingCtx && cmd->transactionId == 0) {
                    blockingCtx->errorCode = ERR_CANCELLED;
                    blockingCtx->completed = 1;
                }
                
                Command_Release(mgr, cmd);
                continue;
            }
            
            // Check if this starts a new transaction
            if (cmd->transactionId != 0 && mgr->currentTransactionId == 0) {
                mgr->currentTransactionId = cmd->transactionId;
                mgr->currentTransactionQueue = cmdQueue;
                
                // Get transaction info from first command
                CmtGetLock(mgr->transactionLock);
                currentTransaction = (DeviceTransaction*)cmd->blockingContext;
                if (currentTransaction) {
                    // Allocate results array
                    transactionResults = calloc(currentTransaction->commandCount, 
                                              sizeof(TransactionCommandResult));
                    transactionCommandIndex = 0;
                    expectedTransactionCommands = currentTransaction->commandCount;
                    currentTransaction->startTime = Timer();
                    
                    LogMessageEx(mgr->logDevice, "Starting transaction %u with %d commands", 
                               mgr->currentTransactionId, expectedTransactionCommands);
                }
                CmtReleaseLock(mgr->transactionLock);
            }
            
            // Check transaction timeout BEFORE executing command
            bool skipDueToTimeout = false;
            if (cmd->transactionId != 0 && currentTransaction) {
                double elapsedMs = (Timer() - currentTransaction->startTime) * 1000.0;
                if (elapsedMs > currentTransaction->timeoutMs) {
                    skipDueToTimeout = true;
                    LogWarningEx(mgr->logDevice, "Transaction %u timed out after %.1f ms (limit: %d ms)", 
                               mgr->currentTransactionId, elapsedMs, currentTransaction->timeoutMs);
                }
            }
            
            // Store current command for status
            CmtGetLock(mgr->currentCommandLock);
            mgr->currentCommand = cmd;
            CmtReleaseLock(mgr->currentCommandLock);
            
            // Process the command
            void *result = NULL;
            BlockingContext *blockingCtx = NULL;
            int errorCode = SUCCESS;
            
            if (!skipDueToTimeout) {
                // For non-transaction blocking commands
                if (cmd->transactionId == 0 && cmd->blockingContext) {
                    blockingCtx = (BlockingContext*)cmd->blockingContext;
                    result = blockingCtx->result;
                } else {
                    result = mgr->adapter->createCommandResult(cmd->commandType);
                }
                
                errorCode = ExecuteDeviceCommand(mgr, cmd, result);
                
                // Update statistics
                CmtGetLock(mgr->statsLock);
                mgr->totalProcessed++;
                if (errorCode != SUCCESS) {
                    mgr->totalErrors++;
                }
                CmtReleaseLock(mgr->statsLock);
                
                // Handle connection loss
                if (errorCode == ERR_COMM_FAILED || errorCode == ERR_TIMEOUT || errorCode == ERR_NOT_CONNECTED) {
                    mgr->isConnected = 0;
                    mgr->lastReconnectTime = Timer();
                    LogWarningEx(mgr->logDevice, "Lost connection during command execution");
                }
            } else {
                // Command skipped due to timeout
                errorCode = ERR_TIMEOUT;
                result = mgr->adapter->createCommandResult(cmd->commandType);
                
                // Update statistics
                CmtGetLock(mgr->statsLock);
                mgr->totalProcessed++;
                mgr->totalErrors++;
                CmtReleaseLock(mgr->statsLock);
            }
            
            // Handle result based on command type
            if (cmd->transactionId != 0) {
                // Part of transaction
                if (transactionResults && transactionCommandIndex < expectedTransactionCommands) {
                    transactionResults[transactionCommandIndex].commandType = cmd->commandType;
                    transactionResults[transactionCommandIndex].errorCode = errorCode;
                    transactionResults[transactionCommandIndex].result = result;
                    transactionCommandIndex++;
                    
                    // Check for abort on error (but not for timeout - we handle that differently)
                    if (errorCode != SUCCESS && errorCode != ERR_TIMEOUT && currentTransaction &&
                        (currentTransaction->flags & DEVICE_TXN_ABORT_ON_ERROR)) {
                        
                        LogWarningEx(mgr->logDevice, "Transaction %u aborted due to error in command %d", 
                                   mgr->currentTransactionId, transactionCommandIndex);
                        
                        // Mark remaining commands as cancelled
                        for (int i = transactionCommandIndex; i < expectedTransactionCommands; i++) {
                            transactionResults[i].commandType = 0;
                            transactionResults[i].errorCode = ERR_CANCELLED;
                            transactionResults[i].result = NULL;
                        }
                        
                        // Force transaction completion
                        transactionCommandIndex = expectedTransactionCommands;
                    }
                    // If command was skipped due to timeout, mark remaining as timed out
                    else if (skipDueToTimeout) {
                        // Mark remaining commands as timed out
                        for (int i = transactionCommandIndex; i < expectedTransactionCommands; i++) {
                            transactionResults[i].commandType = 0;
                            transactionResults[i].errorCode = ERR_TIMEOUT;
                            transactionResults[i].result = NULL;
                        }
                        
                        // Force transaction completion
                        transactionCommandIndex = expectedTransactionCommands;
                    }
                    
                    // Check if transaction complete
                    if (transactionCommandIndex >= expectedTransactionCommands) {
                        // Transaction complete - call callback
                        if (currentTransaction && currentTransaction->callback) {
                            int successCount = 0, failureCount = 0;
                            for (int i = 0; i < expectedTransactionCommands; i++) {
                                if (transactionResults[i].errorCode == SUCCESS) {
                                    successCount++;
                                } else {
                                    failureCount++;
                                }
                            }
                            
                            currentTransaction->callback(mgr->currentTransactionId, 
                                                       successCount, failureCount,
                                                       transactionResults, expectedTransactionCommands,
                                                       currentTransaction->userData);
                        }
                        
                        // Clean up transaction
                        for (int i = 0; i < expectedTransactionCommands; i++) {
                            if (transactionResults[i].result) {
                                mgr->adapter->freeCommandResult(transactionResults[i].commandType,
                                                              transactionResults[i].result);
                            }
                        }
                        free(transactionResults);
                        transactionResults = NULL;
                        
                        // Remove transaction from uncommitted list
                        CmtGetLock(mgr->transactionLock);
                        int count = ListNumItems(mgr->uncommittedTransactions);
                        for (int i = 1; i <= count; i++) {
                            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->uncommittedTransactions, i);
                            if (txnPtr && *txnPtr && (*txnPtr)->id == mgr->currentTransactionId) {
                                free(*txnPtr);
                                ListRemoveItem(mgr->uncommittedTransactions, 0, i);
                                break;
                            }
                        }
                        CmtReleaseLock(mgr->transactionLock);
                        
                        LogMessageEx(mgr->logDevice, "Completed transaction %u", mgr->currentTransactionId);
                        
                        // Reset transaction state
                        mgr->currentTransactionId = 0;
                        mgr->currentTransactionQueue = 0;
                        currentTransaction = NULL;
                        transactionCommandIndex = 0;
                        expectedTransactionCommands = 0;
                    }
                }
            } else if (blockingCtx) {
                // Blocking non-transaction command
                blockingCtx->errorCode = errorCode;
                blockingCtx->completed = 1;
                // Don't free result - blocking thread owns it
            } else {
                // Async non-transaction command
                if (cmd->callback) {
                    cmd->callback(cmd->id, cmd->commandType, result, cmd->userData);
                }
                mgr->adapter->freeCommandResult(cmd->commandType, result);
            }
            
            // Clear current command
            CmtGetLock(mgr->currentCommandLock);
            mgr->currentCommand = NULL;
            CmtReleaseLock(mgr->currentCommandLock);
            
            // Apply command delay (skip if timed out)
            if (!skipDueToTimeout && mgr->adapter->getCommandDelay) {
                int delayMs = mgr->adapter->getCommandDelay(cmd->commandType);
                if (delayMs > 0) {
                    Delay(delayMs / 1000.0);
                }
            }
            
            // Release queue's reference
            Command_Release(mgr, cmd);
        } else {
            // No command available
            Delay(0.01);
        }
    }
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread stopped", mgr->adapter->deviceName);
    return 0;
}

/******************************************************************************
 * Queue Rebuilding for Deferred Commands
 ******************************************************************************/

static void RebuildQueueWithDeferredCommands(DeviceQueueManager *mgr, CmtTSQHandle targetQueue) {
    // Get count of deferred commands
    int deferredCount = 0;
    CmtGetTSQAttribute(mgr->deferredCommandQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &deferredCount);
    
    if (deferredCount == 0) {
        // No deferred commands, nothing to do
        return;
    }
    
    LogDebugEx(mgr->logDevice, "Rebuilding queue with %d deferred commands", deferredCount);
    
    // Get count of remaining commands in target queue
    int remainingCount = 0;
    CmtGetTSQAttribute(targetQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &remainingCount);
    
    // Allocate arrays for commands
    QueuedCommand **deferredCommands = calloc(deferredCount, sizeof(QueuedCommand*));
    QueuedCommand **remainingCommands = NULL;
    
    if (remainingCount > 0) {
        remainingCommands = calloc(remainingCount, sizeof(QueuedCommand*));
    }
    
    if (!deferredCommands || (remainingCount > 0 && !remainingCommands)) {
        LogError("Failed to allocate memory for queue rebuilding");
        free(deferredCommands);
        free(remainingCommands);
        return;
    }
    
    // Read all deferred commands
    int actualDeferred = CmtReadTSQData(mgr->deferredCommandQueue, deferredCommands, deferredCount, 0, 0);
    
    // Read all remaining commands from target queue
    int actualRemaining = 0;
    if (remainingCount > 0) {
        actualRemaining = CmtReadTSQData(targetQueue, remainingCommands, remainingCount, 0, 0);
    }
    
    // Write back: deferred commands first, then remaining commands
    if (actualDeferred > 0) {
        CmtWriteTSQData(targetQueue, deferredCommands, actualDeferred, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    if (actualRemaining > 0) {
        CmtWriteTSQData(targetQueue, remainingCommands, actualRemaining, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    LogDebugEx(mgr->logDevice, "Queue rebuilt: %d deferred + %d remaining commands", 
             actualDeferred, actualRemaining);
    
    // Clean up
    free(deferredCommands);
    free(remainingCommands);
}

/******************************************************************************
 * Internal Processing Functions
 ******************************************************************************/
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
    int count = ListNumItems(mgr->uncommittedTransactions);
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->uncommittedTransactions, i);
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