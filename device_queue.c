/******************************************************************************
 * device_queue.c
 * 
 * Generic thread-safe command queue implementation for device control
 ******************************************************************************/

#include "device_queue.h"
#include "logging.h"
#include "toolbox.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Internal Structures
 ******************************************************************************/

// Queued command structure
struct DeviceQueuedCommand {
    DeviceCommandID id;
    int commandType;
    DevicePriority priority;
    double timestamp;
    void *params;
    DeviceCommandCallback callback;
    void *userData;
    DeviceTransactionHandle transactionId;
    
    // For blocking calls
    CmtThreadLockHandle completionEvent;
    void *resultPtr;
    int *errorCodePtr;
    volatile int *completedPtr;
};

// Transaction structure
typedef struct {
    DeviceTransactionHandle id;
    DeviceQueuedCommand *commands[DEVICE_MAX_TRANSACTION_COMMANDS];
    int commandCount;
    DeviceTransactionCallback callback;
    void *userData;
    bool committed;
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
    
    // Connection state
    volatile int isConnected;
    int reconnectAttempts;
    double lastReconnectTime;
    
    // Command tracking
    volatile DeviceCommandID nextCommandId;
    volatile DeviceTransactionHandle nextTransactionId;
    CmtThreadLockHandle commandLock;
    
    // Active transactions
    ListType activeTransactions;
    CmtThreadLockHandle transactionLock;
    
    // Statistics
    volatile int totalProcessed;
    volatile int totalErrors;
    CmtThreadLockHandle statsLock;
    
    // Logging
    LogDevice logDevice;
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CVICALLBACK ProcessingThreadFunction(void *functionData);
static DeviceQueuedCommand* CreateCommand(DeviceQueueManager *mgr, int commandType, void *params);
static void FreeCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd);
static int ProcessCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd);
static int ExecuteDeviceCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd, void *result);
static void NotifyCommandComplete(DeviceQueuedCommand *cmd, void *result, int errorCode);
static int AttemptReconnection(DeviceQueueManager *mgr);
static DeviceTransaction* FindTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle id);
static void ProcessTransaction(DeviceQueueManager *mgr, DeviceTransaction *txn);
static int ConnectDevice(DeviceQueueManager *mgr);
static void DisconnectDevice(DeviceQueueManager *mgr);
static int FilterQueue(CmtTSQHandle queue, 
                      bool (*shouldCancel)(DeviceQueuedCommand*, void*),
                      void *filterData,
                      DeviceQueueManager *mgr);
static bool ShouldCancelById(DeviceQueuedCommand *cmd, void *data);
static bool ShouldCancelByType(DeviceQueuedCommand *cmd, void *data);
static bool ShouldCancelByAge(DeviceQueuedCommand *cmd, void *data);
static bool ShouldCancelByTransaction(DeviceQueuedCommand *cmd, void *data);

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
    
    // Create locks
    CmtNewLock(NULL, 0, &mgr->commandLock);
    CmtNewLock(NULL, 0, &mgr->transactionLock);
    CmtNewLock(NULL, 0, &mgr->statsLock);
    
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
    
    // Wait for processing thread
    if (mgr->processingThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, mgr->processingThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Disconnect device
    DisconnectDevice(mgr);
    
    // Cancel all pending commands
    DeviceQueue_CancelAll(mgr);
    
    // Dispose queues
    if (mgr->highPriorityQueue) CmtDiscardTSQ(mgr->highPriorityQueue);
    if (mgr->normalPriorityQueue) CmtDiscardTSQ(mgr->normalPriorityQueue);
    if (mgr->lowPriorityQueue) CmtDiscardTSQ(mgr->lowPriorityQueue);
    
    // Dispose locks
    if (mgr->commandLock) CmtDiscardLock(mgr->commandLock);
    if (mgr->transactionLock) CmtDiscardLock(mgr->transactionLock);
    if (mgr->statsLock) CmtDiscardLock(mgr->statsLock);
    
    // Clean up transactions
    if (mgr->activeTransactions) {
        int count = ListNumItems(mgr->activeTransactions);
        for (int i = 1; i <= count; i++) {
            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
            if (txnPtr && *txnPtr) {
                free(*txnPtr);
            }
        }
        ListDispose(mgr->activeTransactions);
    }
    
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
}

void DeviceQueue_SetLogDevice(DeviceQueueManager *mgr, LogDevice device) {
    if (mgr) {
        mgr->logDevice = device;
    }
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
    if (mgr->adapter->disconnect && mgr->adapter->isConnected(mgr->deviceContext)) {
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
    
    // Create a synchronization structure
    typedef struct {
        CmtThreadLockHandle lock;
        void *result;
        int errorCode;
        volatile int completed;
    } SyncBlock;
    
    SyncBlock *sync = calloc(1, sizeof(SyncBlock));
    if (!sync) return ERR_OUT_OF_MEMORY;
    
    // Create result storage
    sync->result = mgr->adapter->createCommandResult(commandType);
    if (!sync->result) {
        free(sync);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Create a lock for synchronization
    int error = CmtNewLock(NULL, 0, &sync->lock);
    if (error < 0) {
        mgr->adapter->freeCommandResult(commandType, sync->result);
        free(sync);
        return ERR_BASE_THREAD;
    }
    
    // Create the command
    DeviceQueuedCommand *cmd = CreateCommand(mgr, commandType, params);
    if (!cmd) {
        CmtDiscardLock(sync->lock);
        mgr->adapter->freeCommandResult(commandType, sync->result);
        free(sync);
        return ERR_OUT_OF_MEMORY;
    }
    
    cmd->priority = priority;
    cmd->completionEvent = sync->lock;
    cmd->resultPtr = sync->result;
    cmd->errorCodePtr = &sync->errorCode;
    cmd->completedPtr = &sync->completed;
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case DEVICE_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case DEVICE_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case DEVICE_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(mgr->logDevice, "Failed to enqueue command type %s", 
                 mgr->adapter->getCommandTypeName(commandType));
        CmtDiscardLock(sync->lock);
        mgr->adapter->freeCommandResult(commandType, sync->result);
        free(sync);
        FreeCommand(mgr, cmd);
        return ERR_QUEUE_FULL;
    }
    
    // Wait for completion using polling
    double startTime = Timer();
    double timeout = (timeoutMs > 0 ? timeoutMs : 30000) / 1000.0;
    int errorCode = ERR_TIMEOUT;
    
    while ((Timer() - startTime) < timeout) {
        CmtGetLock(sync->lock);
        if (sync->completed) {
            // Copy result to caller's buffer
            mgr->adapter->copyCommandResult(commandType, result, sync->result);
            errorCode = sync->errorCode;
            CmtReleaseLock(sync->lock);
            break;
        }
        CmtReleaseLock(sync->lock);
        
        ProcessSystemEvents();
        Delay(0.001);
    }
    
    if (errorCode == ERR_TIMEOUT) {
        LogWarningEx(mgr->logDevice, "Command %s timed out after %dms", 
                   mgr->adapter->getCommandTypeName(commandType), timeoutMs);
    }
    
    // Clean up
    CmtDiscardLock(sync->lock);
    mgr->adapter->freeCommandResult(commandType, sync->result);
    free(sync);
    
    return errorCode;
}

DeviceCommandID DeviceQueue_CommandAsync(DeviceQueueManager *mgr, int commandType,
                                       void *params, DevicePriority priority,
                                       DeviceCommandCallback callback, void *userData) {
    if (!mgr) return 0;
    
    DeviceQueuedCommand *cmd = CreateCommand(mgr, commandType, params);
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
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(mgr->logDevice, "Failed to enqueue async command type %s", 
                 mgr->adapter->getCommandTypeName(commandType));
        FreeCommand(mgr, cmd);
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
    
    // Flush all queues
    CmtFlushTSQ(mgr->highPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->normalPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->lowPriorityQueue, TSQ_FLUSH_ALL, NULL);
    
    return SUCCESS;
}

int DeviceQueue_CancelCommand(DeviceQueueManager *mgr, DeviceCommandID cmdId) {
    if (!mgr || cmdId == 0) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    
    // Search and cancel in all queues
    totalCancelled += FilterQueue(mgr->highPriorityQueue, ShouldCancelById, &cmdId, mgr);
    totalCancelled += FilterQueue(mgr->normalPriorityQueue, ShouldCancelById, &cmdId, mgr);
    totalCancelled += FilterQueue(mgr->lowPriorityQueue, ShouldCancelById, &cmdId, mgr);
    
    if (totalCancelled > 0) {
        LogDebugEx(mgr->logDevice, "Cancelled command ID %u", cmdId);
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;  // Command not found
}

int DeviceQueue_CancelByType(DeviceQueueManager *mgr, int commandType) {
    if (!mgr) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    
    // Cancel in all queues
    totalCancelled += FilterQueue(mgr->highPriorityQueue, ShouldCancelByType, &commandType, mgr);
    totalCancelled += FilterQueue(mgr->normalPriorityQueue, ShouldCancelByType, &commandType, mgr);
    totalCancelled += FilterQueue(mgr->lowPriorityQueue, ShouldCancelByType, &commandType, mgr);
    
    if (totalCancelled > 0) {
        LogMessageEx(mgr->logDevice, "Cancelled %d commands of type %s", 
                   totalCancelled, mgr->adapter->getCommandTypeName(commandType));
    }
    
    return SUCCESS;
}

int DeviceQueue_CancelByAge(DeviceQueueManager *mgr, double ageSeconds) {
    if (!mgr || ageSeconds < 0) return ERR_INVALID_PARAMETER;
    
    int totalCancelled = 0;
    
    // Cancel old commands in all queues
    totalCancelled += FilterQueue(mgr->highPriorityQueue, ShouldCancelByAge, &ageSeconds, mgr);
    totalCancelled += FilterQueue(mgr->normalPriorityQueue, ShouldCancelByAge, &ageSeconds, mgr);
    totalCancelled += FilterQueue(mgr->lowPriorityQueue, ShouldCancelByAge, &ageSeconds, mgr);
    
    if (totalCancelled > 0) {
        LogMessageEx(mgr->logDevice, "Cancelled %d commands older than %.1f seconds", 
                   totalCancelled, ageSeconds);
    }
    
    return SUCCESS;
}

int DeviceQueue_CancelTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txnId) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    // If transaction is already committed, cancel queued commands
    if (txn->committed) {
        CmtReleaseLock(mgr->transactionLock);
        
        // Cancel all commands belonging to this transaction
        int totalCancelled = 0;
        totalCancelled += FilterQueue(mgr->highPriorityQueue, ShouldCancelByTransaction, &txnId, mgr);
        totalCancelled += FilterQueue(mgr->normalPriorityQueue, ShouldCancelByTransaction, &txnId, mgr);
        totalCancelled += FilterQueue(mgr->lowPriorityQueue, ShouldCancelByTransaction, &txnId, mgr);
        
        LogMessageEx(mgr->logDevice, "Cancelled %d commands from transaction %u", 
                   totalCancelled, txnId);
                   
        // Remove transaction from active list
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
        
        // Call transaction callback with cancellation status
        if (txn->callback) {
            txn->callback(txn->id, 0, totalCancelled, NULL, txn->userData);
        }
        
        // Free transaction commands
        for (int i = 0; i < txn->commandCount; i++) {
            if (txn->commands[i]) {
                FreeCommand(mgr, txn->commands[i]);
            }
        }
        
        free(txn);
    } else {
        // Transaction not yet committed - just remove it
        int count = ListNumItems(mgr->activeTransactions);
        for (int i = 1; i <= count; i++) {
            DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
            if (txnPtr && *txnPtr == txn) {
                ListRemoveItem(mgr->activeTransactions, 0, i);
                break;
            }
        }
        
        // Free any commands that were added
        for (int i = 0; i < txn->commandCount; i++) {
            if (txn->commands[i]) {
                FreeCommand(mgr, txn->commands[i]);
            }
        }
        
        free(txn);
        CmtReleaseLock(mgr->transactionLock);
        
        LogMessageEx(mgr->logDevice, "Cancelled uncommitted transaction %u", txnId);
    }
    
    return SUCCESS;
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

DeviceTransactionHandle DeviceQueue_BeginTransaction(DeviceQueueManager *mgr) {
    if (!mgr) return 0;
    
    DeviceTransaction *txn = calloc(1, sizeof(DeviceTransaction));
    if (!txn) return 0;
    
    CmtGetLock(mgr->transactionLock);
    txn->id = mgr->nextTransactionId++;
    ListInsertItem(mgr->activeTransactions, &txn, END_OF_LIST);
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(mgr->logDevice, "Started transaction %u", txn->id);
    return txn->id;
}

int DeviceQueue_AddToTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txnId,
                               int commandType, void *params) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    DeviceTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    if (txn->commandCount >= DEVICE_MAX_TRANSACTION_COMMANDS) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    DeviceQueuedCommand *cmd = CreateCommand(mgr, commandType, params);
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
        return ERR_INVALID_PARAMETER;
    }
    
    txn->callback = callback;
    txn->userData = userData;
    txn->committed = true;
    
    // Queue all commands as high priority
    for (int i = 0; i < txn->commandCount; i++) {
        DeviceQueuedCommand *cmd = txn->commands[i];
        cmd->priority = DEVICE_PRIORITY_HIGH;
        
        if (CmtWriteTSQData(mgr->highPriorityQueue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
            LogErrorEx(mgr->logDevice, "Failed to queue transaction command %d", i);
        }
    }
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessageEx(mgr->logDevice, "Committed transaction %u with %d commands", 
               txnId, txn->commandCount);
    return SUCCESS;
}

/******************************************************************************
 * Internal Functions
 ******************************************************************************/

static DeviceQueuedCommand* CreateCommand(DeviceQueueManager *mgr, int commandType, void *params) {
    static volatile DeviceCommandID nextId = 1;
    
    DeviceQueuedCommand *cmd = calloc(1, sizeof(DeviceQueuedCommand));
    if (!cmd) return NULL;
    
    cmd->id = InterlockedIncrement(&nextId);
    cmd->commandType = commandType;
    cmd->timestamp = Timer();
    
    // Create device-specific parameter copy
    if (params && mgr->adapter->createCommandParams) {
        cmd->params = mgr->adapter->createCommandParams(commandType, params);
        if (!cmd->params) {
            free(cmd);
            return NULL;
        }
    }
    
    return cmd;
}

static void FreeCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd) {
    if (!cmd) return;
    
    if (cmd->params && mgr->adapter->freeCommandParams) {
        mgr->adapter->freeCommandParams(cmd->commandType, cmd->params);
    }
    
    free(cmd);
}

static int CVICALLBACK ProcessingThreadFunction(void *functionData) {
    DeviceQueueManager *mgr = (DeviceQueueManager*)functionData;
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread started", mgr->adapter->deviceName);
    
    while (!mgr->shutdownRequested) {
        DeviceQueuedCommand *cmd = NULL;
        unsigned int itemsRead = 0;
        
        // Check connection state
        if (!mgr->isConnected) {
            if (Timer() - mgr->lastReconnectTime > (DEVICE_QUEUE_RECONNECT_DELAY_MS / 1000.0)) {
                AttemptReconnection(mgr);
            }
            Delay(0.1);
            continue;
        }
        
        // Check queues in priority order
        if (CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, itemsRead) >= 0 && itemsRead > 0) {
            // Got high priority command
        } else if (CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, itemsRead) >= 0 && itemsRead > 0) {
            // Got normal priority command
        } else if (CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, itemsRead) >= 0 && itemsRead > 0) {
            // Got low priority command
        } else {
            // No commands available
            Delay(0.01);
            continue;
        }
        
        // Process the command
        if (cmd) {
            ProcessCommand(mgr, cmd);
            
            // Don't free transaction commands yet
            if (cmd->transactionId == 0) {
                FreeCommand(mgr, cmd);
            }
        }
    }
    
    LogMessageEx(mgr->logDevice, "%s queue processing thread stopped", mgr->adapter->deviceName);
    return 0;
}

static int ProcessCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd) {
    void *result = NULL;
    int errorCode = SUCCESS;
    
    LogDebugEx(mgr->logDevice, "Processing command: %s (ID: %u)", 
             mgr->adapter->getCommandTypeName(cmd->commandType), cmd->id);
    
    // Create result storage
    result = mgr->adapter->createCommandResult(cmd->commandType);
    if (!result) {
        errorCode = ERR_OUT_OF_MEMORY;
    } else {
        // Execute the command
        errorCode = ExecuteDeviceCommand(mgr, cmd, result);
    }
    
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
    
    // Notify completion
    NotifyCommandComplete(cmd, result, errorCode);
    
    // Free result
    if (result && mgr->adapter->freeCommandResult) {
        mgr->adapter->freeCommandResult(cmd->commandType, result);
    }
    
    // Check if this was part of a transaction
    if (cmd->transactionId != 0) {
        CmtGetLock(mgr->transactionLock);
        DeviceTransaction *txn = FindTransaction(mgr, cmd->transactionId);
        if (txn) {
            // Check if all commands in transaction are complete
            bool allComplete = true;
            for (int i = 0; i < txn->commandCount; i++) {
                if (txn->commands[i] && txn->commands[i]->completionEvent) {
                    allComplete = false;
                    break;
                }
            }
            
            if (allComplete) {
                ProcessTransaction(mgr, txn);
            }
        }
        CmtReleaseLock(mgr->transactionLock);
    }
    
    // Apply command-specific delay
    int delayMs = mgr->adapter->getCommandDelay(cmd->commandType);
    if (delayMs > 0) {
        Delay(delayMs / 1000.0);
    }
    
    return errorCode;
}

static int ExecuteDeviceCommand(DeviceQueueManager *mgr, DeviceQueuedCommand *cmd, void *result) {
    return mgr->adapter->executeCommand(mgr->deviceContext, cmd->commandType, cmd->params, result);
}

static void NotifyCommandComplete(DeviceQueuedCommand *cmd, void *result, int errorCode) {
    // For blocking commands
    if (cmd->completionEvent && cmd->completedPtr) {
        CmtGetLock(cmd->completionEvent);
        
        // Set error code
        if (cmd->errorCodePtr) {
            *cmd->errorCodePtr = errorCode;
        }
        
        // Results already copied to resultPtr during execution
        
        // Set completed flag
        *cmd->completedPtr = 1;
        
        CmtReleaseLock(cmd->completionEvent);
    }
    
    // For async commands
    if (cmd->callback) {
        cmd->callback(cmd->id, cmd->commandType, result, cmd->userData);
    }
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

static DeviceTransaction* FindTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle id) {
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        DeviceTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr && (*txnPtr)->id == id) {
            return *txnPtr;
        }
    }
    return NULL;
}

static void ProcessTransaction(DeviceQueueManager *mgr, DeviceTransaction *txn) {
    if (!txn->callback) return;
    
    // Collect results - simplified for now
    int successCount = 0;
    int failureCount = 0;
    
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            if (txn->commands[i]->errorCodePtr && *txn->commands[i]->errorCodePtr == SUCCESS) {
                successCount++;
            } else {
                failureCount++;
            }
        }
    }
    
    // Call transaction callback
    txn->callback(txn->id, successCount, failureCount, NULL, txn->userData);
    
    // Clean up
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            FreeCommand(mgr, txn->commands[i]);
        }
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
    
    free(txn);
}

/******************************************************************************
 * Helper function to filter commands from a queue
 ******************************************************************************/
static int FilterQueue(CmtTSQHandle queue, 
                      bool (*shouldCancel)(DeviceQueuedCommand*, void*),
                      void *filterData,
                      DeviceQueueManager *mgr) {
    if (!queue) return 0;
    
    int cancelledCount = 0;
    int queueSize = 0;
    
    // Get current queue size
    CmtGetTSQAttribute(queue, ATTR_TSQ_ITEMS_IN_QUEUE, &queueSize);
    if (queueSize == 0) return 0;
    
    // Allocate buffer for all commands
    DeviceQueuedCommand **commands = calloc(queueSize, sizeof(DeviceQueuedCommand*));
    if (!commands) return 0;
    
    // Read all commands from queue
    unsigned int itemsRead = 0;
    CmtReadTSQData(queue, commands, queueSize, 0, itemsRead);
    
    // Process each command
    for (unsigned int i = 0; i < itemsRead; i++) {
        DeviceQueuedCommand *cmd = commands[i];
        if (!cmd) continue;
        
        if (shouldCancel(cmd, filterData)) {
            // Notify cancellation
            NotifyCommandComplete(cmd, NULL, ERR_CANCELLED);
            
            // Free the command
            FreeCommand(mgr, cmd);
            commands[i] = NULL;  // Mark as removed
            cancelledCount++;
        }
    }
    
    // Write back non-cancelled commands
    if (cancelledCount > 0) {
        for (unsigned int i = 0; i < itemsRead; i++) {
            if (commands[i] != NULL) {
                CmtWriteTSQData(queue, &commands[i], 1, TSQ_INFINITE_TIMEOUT, NULL);
            }
        }
    } else {
        // No commands cancelled, write all back
        CmtWriteTSQData(queue, commands, itemsRead, TSQ_INFINITE_TIMEOUT, NULL);
    }
    
    free(commands);
    return cancelledCount;
}

/******************************************************************************
 * Filter predicates for different cancellation types
 ******************************************************************************/
static bool ShouldCancelById(DeviceQueuedCommand *cmd, void *data) {
    DeviceCommandID *targetId = (DeviceCommandID*)data;
    return cmd->id == *targetId;
}

static bool ShouldCancelByType(DeviceQueuedCommand *cmd, void *data) {
    int *targetType = (int*)data;
    return cmd->commandType == *targetType;
}

static bool ShouldCancelByAge(DeviceQueuedCommand *cmd, void *data) {
    double *maxAge = (double*)data;
    double age = Timer() - cmd->timestamp;
    return age > *maxAge;
}

static bool ShouldCancelByTransaction(DeviceQueuedCommand *cmd, void *data) {
    DeviceTransactionHandle *txnId = (DeviceTransactionHandle*)data;
    return cmd->transactionId == *txnId;
}