/******************************************************************************
 * psb10000_queue.c
 * 
 * Thread-safe command queue implementation for PSB 10000 Series Power Supply
 ******************************************************************************/

#include "psb10000_queue.h"
#include "logging.h"
#include "toolbox.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Internal Structures
 ******************************************************************************/

// Queued command structure
struct QueuedCommand {
    CommandID id;
    PSBCommandType type;
    PSBPriority priority;
    double timestamp;
    PSBCommandParams params;
    PSBCommandCallback callback;
    void *userData;
    TransactionHandle transactionId;
    
    // For blocking calls
    CmtThreadLockHandle completionEvent;
    PSBCommandResult *resultPtr;
    int *errorCodePtr;
	volatile int *completedPtr;  // Add this field
    
    // For raw Modbus - we need to copy buffers
    unsigned char *txBufferCopy;
    unsigned char *rxBufferCopy;
};

// Transaction structure
typedef struct {
    TransactionHandle id;
    QueuedCommand *commands[PSB_MAX_TRANSACTION_COMMANDS];
    int commandCount;
    PSBTransactionCallback callback;
    void *userData;
    bool committed;
} Transaction;

// Queue manager structure
struct PSBQueueManager {
    // PSB device handle and connection info
    PSB_Handle psbHandle;
    PSB_Handle *psbHandlePtr;  // Pointer to the handle
    char targetSerial[64];
    bool autoDiscovery;
    int specificPort;
    int specificBaudRate;
    int specificSlaveAddress;
    
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
    volatile CommandID nextCommandId;
    volatile TransactionHandle nextTransactionId;
    CmtThreadLockHandle commandLock;
    
    // Active transactions
    ListType activeTransactions;
    CmtThreadLockHandle transactionLock;
    
    // Statistics
    volatile int totalProcessed;
    volatile int totalErrors;
    CmtThreadLockHandle statsLock;
};

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* g_commandTypeNames[] = {
    "NONE",
    "SET_REMOTE_MODE",
    "SET_OUTPUT_ENABLE",
    "SET_VOLTAGE",
    "SET_CURRENT",
    "SET_POWER",
    "SET_VOLTAGE_LIMITS",
    "SET_CURRENT_LIMITS",
    "SET_POWER_LIMIT",
    "GET_STATUS",
    "GET_ACTUAL_VALUES",
    "RAW_MODBUS"
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CVICALLBACK ProcessingThreadFunction(void *functionData);
static QueuedCommand* CreateCommand(PSBCommandType type, PSBCommandParams *params);
static void FreeCommand(QueuedCommand *cmd);
static int ProcessCommand(PSBQueueManager *mgr, QueuedCommand *cmd);
static int ExecutePSBCommand(PSB_Handle *handle, QueuedCommand *cmd, PSBCommandResult *result);
static void NotifyCommandComplete(QueuedCommand *cmd, PSBCommandResult *result);
static int AttemptReconnection(PSBQueueManager *mgr);
static Transaction* FindTransaction(PSBQueueManager *mgr, TransactionHandle id);
static void ProcessTransaction(PSBQueueManager *mgr, Transaction *txn);
static int ConnectPSB(PSBQueueManager *mgr);
static void DisconnectPSB(PSBQueueManager *mgr);

void PSB_SetGlobalQueueManager(PSBQueueManager *mgr);
PSBQueueManager* PSB_GetGlobalQueueManager(void);

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

PSBQueueManager* PSB_QueueInit(const char *targetSerial) {
    if (!targetSerial || strlen(targetSerial) == 0) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: No target serial number provided");
        return NULL;
    }
    
    PSBQueueManager *mgr = calloc(1, sizeof(PSBQueueManager));
    if (!mgr) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: Failed to allocate manager");
        return NULL;
    }
    
    // Store connection parameters
    SAFE_STRCPY(mgr->targetSerial, targetSerial, sizeof(mgr->targetSerial));
    mgr->autoDiscovery = true;
    mgr->specificPort = 0;
    mgr->specificBaudRate = 0;
    mgr->specificSlaveAddress = DEFAULT_SLAVE_ADDRESS;
    mgr->psbHandlePtr = &mgr->psbHandle;
    mgr->nextCommandId = 1;
    mgr->nextTransactionId = 1;
    mgr->isConnected = 0;
    
    // Create queues
    int error = 0;
    error |= CmtNewTSQ(PSB_QUEUE_HIGH_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->highPriorityQueue);
    error |= CmtNewTSQ(PSB_QUEUE_NORMAL_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->normalPriorityQueue);
    error |= CmtNewTSQ(PSB_QUEUE_LOW_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->lowPriorityQueue);
    
    if (error < 0) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: Failed to create queues");
        PSB_QueueShutdown(mgr);
        return NULL;
    }
    
    // Create locks
    CmtNewLock(NULL, 0, &mgr->commandLock);
    CmtNewLock(NULL, 0, &mgr->transactionLock);
    CmtNewLock(NULL, 0, &mgr->statsLock);
    
    // Initialize transaction list
    mgr->activeTransactions = ListCreate(sizeof(Transaction*));
    
    // Attempt initial connection
    LogMessageEx(LOG_DEVICE_PSB, "Attempting to connect to PSB %s...", targetSerial);
    int connectResult = ConnectPSB(mgr);
    
    if (connectResult == PSB_SUCCESS) {
        LogMessageEx(LOG_DEVICE_PSB, "Successfully connected to PSB %s", targetSerial);
        mgr->isConnected = 1;
    } else {
        LogWarningEx(LOG_DEVICE_PSB, "Failed initial connection to PSB %s - will retry in background", targetSerial);
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
    }
    
    // Start processing thread
    if (g_threadPool > 0) {
        error = CmtScheduleThreadPoolFunction(g_threadPool, ProcessingThreadFunction, 
                                            mgr, &mgr->processingThreadId);
        if (error != 0) {
            LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: Failed to start processing thread");
            PSB_QueueShutdown(mgr);
            return NULL;
        }
    } else {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: No thread pool available");
        PSB_QueueShutdown(mgr);
        return NULL;
    }
    
    LogMessageEx(LOG_DEVICE_PSB, "PSB queue manager initialized for serial %s", targetSerial);
    return mgr;
}

PSBQueueManager* PSB_QueueInitSpecific(int comPort, int slaveAddress, int baudRate) {
    PSBQueueManager *mgr = calloc(1, sizeof(PSBQueueManager));
    if (!mgr) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: Failed to allocate manager");
        return NULL;
    }
    
    // Store specific connection parameters
    mgr->autoDiscovery = false;
    mgr->specificPort = comPort;
    mgr->specificBaudRate = baudRate;
    mgr->specificSlaveAddress = slaveAddress;
    mgr->psbHandlePtr = &mgr->psbHandle;
    mgr->nextCommandId = 1;
    mgr->nextTransactionId = 1;
    mgr->isConnected = 0;
    
    // Create queues
    int error = 0;
    error |= CmtNewTSQ(PSB_QUEUE_HIGH_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->highPriorityQueue);
    error |= CmtNewTSQ(PSB_QUEUE_NORMAL_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->normalPriorityQueue);
    error |= CmtNewTSQ(PSB_QUEUE_LOW_PRIORITY_SIZE, sizeof(QueuedCommand*), 0, &mgr->lowPriorityQueue);
    
    if (error < 0) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: Failed to create queues");
        PSB_QueueShutdown(mgr);
        return NULL;
    }
    
    // Create locks
    CmtNewLock(NULL, 0, &mgr->commandLock);
    CmtNewLock(NULL, 0, &mgr->transactionLock);
    CmtNewLock(NULL, 0, &mgr->statsLock);
    
    // Initialize transaction list
    mgr->activeTransactions = ListCreate(sizeof(Transaction*));
    
    // Attempt initial connection
    LogMessageEx(LOG_DEVICE_PSB, "Attempting to connect to PSB on COM%d...", comPort);
    int connectResult = ConnectPSB(mgr);
    
    if (connectResult == PSB_SUCCESS) {
        LogMessageEx(LOG_DEVICE_PSB, "Successfully connected to PSB on COM%d", comPort);
        mgr->isConnected = 1;
    } else {
        LogWarningEx(LOG_DEVICE_PSB, "Failed initial connection to PSB on COM%d", comPort);
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
    }
    
    // Start processing thread
    if (g_threadPool > 0) {
        error = CmtScheduleThreadPoolFunction(g_threadPool, ProcessingThreadFunction, 
                                            mgr, &mgr->processingThreadId);
        if (error != 0) {
            LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: Failed to start processing thread");
            PSB_QueueShutdown(mgr);
            return NULL;
        }
    } else {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: No thread pool available");
        PSB_QueueShutdown(mgr);
        return NULL;
    }
    
    LogMessageEx(LOG_DEVICE_PSB, "PSB queue manager initialized for COM%d", comPort);
    return mgr;
}

void PSB_QueueShutdown(PSBQueueManager *mgr) {
    if (!mgr) return;
    
    LogMessageEx(LOG_DEVICE_PSB, "Shutting down PSB queue manager...");
    
    // Signal shutdown
    InterlockedExchange(&mgr->shutdownRequested, 1);
    
    // Wait for processing thread
    if (mgr->processingThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, mgr->processingThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Disconnect PSB
    DisconnectPSB(mgr);
    
    // Cancel all pending commands
    PSB_QueueCancelAll(mgr);
    
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
            Transaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
            if (txnPtr && *txnPtr) {
                free(*txnPtr);
            }
        }
        ListDispose(mgr->activeTransactions);
    }
    
    free(mgr);
    LogMessageEx(LOG_DEVICE_PSB, "PSB queue manager shut down");
}

PSB_Handle* PSB_QueueGetHandle(PSBQueueManager *mgr) {
    if (!mgr || !mgr->isConnected) {
        return NULL;
    }
    return mgr->psbHandlePtr;
}

bool PSB_QueueIsRunning(PSBQueueManager *mgr) {
    return mgr && !mgr->shutdownRequested;
}

void PSB_QueueGetStats(PSBQueueManager *mgr, PSBQueueStats *stats) {
    if (!mgr || !stats) return;
    
    memset(stats, 0, sizeof(PSBQueueStats));
    
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

/******************************************************************************
 * Internal Connection Functions
 ******************************************************************************/

static int ConnectPSB(PSBQueueManager *mgr) {
    int result;
    
    if (mgr->autoDiscovery) {
        // Use auto-discovery
        LogMessageEx(LOG_DEVICE_PSB, "Auto-discovering PSB with serial %s...", mgr->targetSerial);
        result = PSB_AutoDiscover(mgr->targetSerial, mgr->psbHandlePtr);
        
        if (result == PSB_SUCCESS) {
            // Set initial state
            PSB_SetRemoteMode(mgr->psbHandlePtr, 1);
            PSB_SetOutputEnable(mgr->psbHandlePtr, 0);  // Start with output disabled
            
            // Get initial status
            PSB_Status status;
            if (PSB_GetStatus(mgr->psbHandlePtr, &status) == PSB_SUCCESS) {
                LogMessageEx(LOG_DEVICE_PSB, "PSB Status: Output=%s, Remote=%s", 
                           status.outputEnabled ? "ON" : "OFF",
                           status.remoteMode ? "YES" : "NO");
            }
        }
    } else {
        // Use specific connection parameters
        LogMessageEx(LOG_DEVICE_PSB, "Connecting to PSB on COM%d...", mgr->specificPort);
        result = PSB_InitializeSpecific(mgr->psbHandlePtr, mgr->specificPort, 
                                      mgr->specificSlaveAddress, mgr->specificBaudRate);
        
        if (result == PSB_SUCCESS) {
            // Get serial number
            // Note: This would require implementing a serial number read function
            SAFE_STRCPY(mgr->psbHandlePtr->serialNumber, "UNKNOWN", sizeof(mgr->psbHandlePtr->serialNumber));
            
            // Set initial state
            PSB_SetRemoteMode(mgr->psbHandlePtr, 1);
            PSB_SetOutputEnable(mgr->psbHandlePtr, 0);
        }
    }
    
    return result;
}

static void DisconnectPSB(PSBQueueManager *mgr) {
    if (mgr->psbHandlePtr && mgr->psbHandlePtr->isConnected) {
        // Disable output and remote mode before disconnecting
        PSB_SetOutputEnable(mgr->psbHandlePtr, 0);
        PSB_SetRemoteMode(mgr->psbHandlePtr, 0);
        PSB_Close(mgr->psbHandlePtr);
        
        mgr->isConnected = 0;
        LogMessageEx(LOG_DEVICE_PSB, "Disconnected from PSB");
    }
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

int PSB_QueueCommandBlocking(PSBQueueManager *mgr, PSBCommandType type,
                           PSBCommandParams *params, PSBPriority priority,
                           PSBCommandResult *result, int timeoutMs) {
    if (!mgr || !result) return PSB_ERROR_INVALID_PARAM;
    
    // Create a synchronization structure that we control
    typedef struct {
        CmtThreadLockHandle lock;
        PSBCommandResult result;
        int errorCode;
        volatile int completed;
    } SyncBlock;
    
    SyncBlock *sync = calloc(1, sizeof(SyncBlock));
    if (!sync) return PSB_ERROR_COMM;
    
    // Create a regular lock for synchronization (NOT an event lock)
    int error = CmtNewLock(NULL, 0, &sync->lock);
    if (error < 0) {
        free(sync);
        return PSB_ERROR_COMM;
    }
    
    // Create the command
    QueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) {
        CmtDiscardLock(sync->lock);
        free(sync);
        return PSB_ERROR_COMM;
    }
    
    cmd->priority = priority;
    cmd->completionEvent = sync->lock;  // Store the lock handle
    cmd->resultPtr = &sync->result;
    cmd->errorCodePtr = &sync->errorCode;
    cmd->completedPtr = &sync->completed;  // Add this field to QueuedCommand
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case PSB_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case PSB_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case PSB_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to enqueue command type %s", 
                 PSB_QueueGetCommandTypeName(type));
        CmtDiscardLock(sync->lock);
        free(sync);
        FreeCommand(cmd);
        return PSB_ERROR_COMM;
    }
    
    // Wait for completion using polling
    double startTime = Timer();
    double timeout = (timeoutMs > 0 ? timeoutMs : 30000) / 1000.0; // Convert to seconds
    int errorCode = PSB_ERROR_TIMEOUT;
    
    while ((Timer() - startTime) < timeout) {
        // Check if command completed
        CmtGetLock(sync->lock);
        if (sync->completed) {
            *result = sync->result;
            errorCode = sync->errorCode;
            CmtReleaseLock(sync->lock);
            break;
        }
        CmtReleaseLock(sync->lock);
        
        // Process events and yield CPU
        ProcessSystemEvents();
        Delay(0.001); // 1ms delay
    }
    
    if (errorCode == PSB_ERROR_TIMEOUT) {
        LogWarningEx(LOG_DEVICE_PSB, "Command %s timed out after %dms", 
                   PSB_QueueGetCommandTypeName(type), timeoutMs);
    }
    
    // Clean up
    CmtDiscardLock(sync->lock);
    free(sync);
    
    return errorCode;
}

CommandID PSB_QueueCommandAsync(PSBQueueManager *mgr, PSBCommandType type,
                              PSBCommandParams *params, PSBPriority priority,
                              PSBCommandCallback callback, void *userData) {
    if (!mgr) return 0;
    
    QueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) return 0;
    
    cmd->priority = priority;
    cmd->callback = callback;
    cmd->userData = userData;
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case PSB_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case PSB_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case PSB_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to enqueue async command type %s", 
                 PSB_QueueGetCommandTypeName(type));
        FreeCommand(cmd);
        return 0;
    }
    
    return cmd->id;
}

bool PSB_QueueHasCommandType(PSBQueueManager *mgr, PSBCommandType type) {
    if (!mgr) return false;
    
    // For now, we'll just check if status commands are queued
    // This is specifically for preventing status command buildup
    int count = 0;
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &count);
    
    // TODO: Implement proper scanning of queue for command type
    // For now, assume if there are items in normal queue, there might be a status command
    return (type == PSB_CMD_GET_STATUS && count > 0);
}

int PSB_QueueCancelCommand(PSBQueueManager *mgr, CommandID cmdId) {
    // TODO: Implement command cancellation by ID
    return PSB_ERROR_NOT_SUPPORTED;
}

int PSB_QueueCancelByType(PSBQueueManager *mgr, PSBCommandType type) {
    // TODO: Implement command cancellation by type
    return PSB_ERROR_NOT_SUPPORTED;
}

int PSB_QueueCancelByAge(PSBQueueManager *mgr, double ageSeconds) {
    // TODO: Implement command cancellation by age
    return PSB_ERROR_NOT_SUPPORTED;
}

int PSB_QueueCancelAll(PSBQueueManager *mgr) {
    if (!mgr) return PSB_ERROR_INVALID_PARAM;
    
    // Flush all queues
    CmtFlushTSQ(mgr->highPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->normalPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->lowPriorityQueue, TSQ_FLUSH_ALL, NULL);
    
    return PSB_SUCCESS;
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle PSB_QueueBeginTransaction(PSBQueueManager *mgr) {
    if (!mgr) return 0;
    
    Transaction *txn = calloc(1, sizeof(Transaction));
    if (!txn) return 0;
    
    CmtGetLock(mgr->transactionLock);
    txn->id = mgr->nextTransactionId++;
    ListInsertItem(mgr->activeTransactions, &txn, END_OF_LIST);
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(LOG_DEVICE_PSB, "Started transaction %u", txn->id);
    return txn->id;
}

int PSB_QueueAddToTransaction(PSBQueueManager *mgr, TransactionHandle txnId,
                            PSBCommandType type, PSBCommandParams *params) {
    if (!mgr || txnId == 0) return PSB_ERROR_INVALID_PARAM;
    
    CmtGetLock(mgr->transactionLock);
    Transaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed) {
        CmtReleaseLock(mgr->transactionLock);
        return PSB_ERROR_INVALID_PARAM;
    }
    
    if (txn->commandCount >= PSB_MAX_TRANSACTION_COMMANDS) {
        CmtReleaseLock(mgr->transactionLock);
        return PSB_ERROR_INVALID_PARAM;
    }
    
    QueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) {
        CmtReleaseLock(mgr->transactionLock);
        return PSB_ERROR_COMM;
    }
    
    cmd->transactionId = txnId;
    txn->commands[txn->commandCount++] = cmd;
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(LOG_DEVICE_PSB, "Added %s to transaction %u", 
             PSB_QueueGetCommandTypeName(type), txnId);
    return PSB_SUCCESS;
}

int PSB_QueueCommitTransaction(PSBQueueManager *mgr, TransactionHandle txnId,
                             PSBTransactionCallback callback, void *userData) {
    if (!mgr || txnId == 0) return PSB_ERROR_INVALID_PARAM;
    
    CmtGetLock(mgr->transactionLock);
    Transaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->commandCount == 0) {
        CmtReleaseLock(mgr->transactionLock);
        return PSB_ERROR_INVALID_PARAM;
    }
    
    txn->callback = callback;
    txn->userData = userData;
    txn->committed = true;
    
    // Queue all commands as high priority
    for (int i = 0; i < txn->commandCount; i++) {
        QueuedCommand *cmd = txn->commands[i];
        cmd->priority = PSB_PRIORITY_HIGH;
        
        if (CmtWriteTSQData(mgr->highPriorityQueue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
            LogErrorEx(LOG_DEVICE_PSB, "Failed to queue transaction command %d", i);
            // Continue anyway - partial transaction execution
        }
    }
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessageEx(LOG_DEVICE_PSB, "Committed transaction %u with %d commands", 
               txnId, txn->commandCount);
    return PSB_SUCCESS;
}

/******************************************************************************
 * Wrapper Functions
 ******************************************************************************/

// Global queue manager pointer - set by main application
static PSBQueueManager *g_psbQueueManager = NULL;

void PSB_SetGlobalQueueManager(PSBQueueManager *mgr) {
    g_psbQueueManager = mgr;
}

PSBQueueManager* PSB_GetGlobalQueueManager(void) {
    return g_psbQueueManager;
}

int PSB_SetRemoteModeQueued(PSB_Handle *handle, int enable) {
    if (!g_psbQueueManager) return PSB_SetRemoteMode(handle, enable);
    
    PSBCommandParams params = {.remoteMode = {enable}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_REMOTE_MODE,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetOutputEnableQueued(PSB_Handle *handle, int enable) {
    if (!g_psbQueueManager) return PSB_SetOutputEnable(handle, enable);
    
    PSBCommandParams params = {.outputEnable = {enable}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_OUTPUT_ENABLE,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetVoltageQueued(PSB_Handle *handle, double voltage) {
    if (!g_psbQueueManager) return PSB_SetVoltage(handle, voltage);
    
    PSBCommandParams params = {.setVoltage = {voltage}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetCurrentQueued(PSB_Handle *handle, double current) {
    if (!g_psbQueueManager) return PSB_SetCurrent(handle, current);
    
    PSBCommandParams params = {.setCurrent = {current}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetPowerQueued(PSB_Handle *handle, double power) {
    if (!g_psbQueueManager) return PSB_SetPower(handle, power);
    
    PSBCommandParams params = {.setPower = {power}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetVoltageLimitsQueued(PSB_Handle *handle, double minVoltage, double maxVoltage) {
    if (!g_psbQueueManager) return PSB_SetVoltageLimits(handle, minVoltage, maxVoltage);
    
    PSBCommandParams params = {.voltageLimits = {minVoltage, maxVoltage}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE_LIMITS,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetCurrentLimitsQueued(PSB_Handle *handle, double minCurrent, double maxCurrent) {
    if (!g_psbQueueManager) return PSB_SetCurrentLimits(handle, minCurrent, maxCurrent);
    
    PSBCommandParams params = {.currentLimits = {minCurrent, maxCurrent}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT_LIMITS,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_SetPowerLimitQueued(PSB_Handle *handle, double maxPower) {
    if (!g_psbQueueManager) return PSB_SetPowerLimit(handle, maxPower);
    
    PSBCommandParams params = {.powerLimit = {maxPower}};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER_LIMIT,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    return error;
}

int PSB_GetStatusQueued(PSB_Handle *handle, PSB_Status *status) {
    if (!g_psbQueueManager || !status) return PSB_GetStatus(handle, status);
    
    PSBCommandParams params = {0};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_GET_STATUS,
                                       &params, PSB_PRIORITY_NORMAL, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == PSB_SUCCESS) {
        *status = result.data.status;
    }
    return error;
}

int PSB_GetActualValuesQueued(PSB_Handle *handle, double *voltage, double *current, double *power) {
    if (!g_psbQueueManager) return PSB_GetActualValues(handle, voltage, current, power);
    
    PSBCommandParams params = {0};
    PSBCommandResult result;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_GET_ACTUAL_VALUES,
                                       &params, PSB_PRIORITY_NORMAL, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == PSB_SUCCESS) {
        if (voltage) *voltage = result.data.actualValues.voltage;
        if (current) *current = result.data.actualValues.current;
        if (power) *power = result.data.actualValues.power;
    }
    return error;
}

/******************************************************************************
 * Internal Functions
 ******************************************************************************/

static QueuedCommand* CreateCommand(PSBCommandType type, PSBCommandParams *params) {
    static volatile CommandID nextId = 1;
    
    QueuedCommand *cmd = calloc(1, sizeof(QueuedCommand));
    if (!cmd) return NULL;
    
    cmd->id = InterlockedIncrement(&nextId);
    cmd->type = type;
    cmd->timestamp = Timer();
    
    if (params) {
        cmd->params = *params;
        
        // For raw Modbus, we need to copy the buffers
        if (type == PSB_CMD_RAW_MODBUS && params->rawModbus.txBuffer) {
            cmd->txBufferCopy = malloc(params->rawModbus.txLength);
            if (cmd->txBufferCopy) {
                memcpy(cmd->txBufferCopy, params->rawModbus.txBuffer, params->rawModbus.txLength);
                cmd->params.rawModbus.txBuffer = cmd->txBufferCopy;
            }
            
            if (params->rawModbus.rxBufferSize > 0) {
                cmd->rxBufferCopy = malloc(params->rawModbus.rxBufferSize);
                cmd->params.rawModbus.rxBuffer = cmd->rxBufferCopy;
            }
        }
    }
    
    return cmd;
}

static void FreeCommand(QueuedCommand *cmd) {
    if (!cmd) return;
    
    if (cmd->txBufferCopy) free(cmd->txBufferCopy);
    if (cmd->rxBufferCopy) free(cmd->rxBufferCopy);
    
    free(cmd);
}

static int CVICALLBACK ProcessingThreadFunction(void *functionData) {
    PSBQueueManager *mgr = (PSBQueueManager*)functionData;
    
    LogMessageEx(LOG_DEVICE_PSB, "PSB queue processing thread started");
    
    while (!mgr->shutdownRequested) {
        QueuedCommand *cmd = NULL;
        unsigned int itemsRead = 0;
        
        // Check connection state
        if (!mgr->isConnected) {
            if (Timer() - mgr->lastReconnectTime > (PSB_QUEUE_RECONNECT_DELAY_MS / 1000.0)) {
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
                FreeCommand(cmd);
            }
        }
    }
    
    LogMessageEx(LOG_DEVICE_PSB, "PSB queue processing thread stopped");
    return 0;
}

static int ProcessCommand(PSBQueueManager *mgr, QueuedCommand *cmd) {
    PSBCommandResult result = {0};
    
    LogDebugEx(LOG_DEVICE_PSB, "Processing command: %s (ID: %u)", 
             PSB_QueueGetCommandTypeName(cmd->type), cmd->id);
    
    // Execute the command
    result.errorCode = ExecutePSBCommand(mgr->psbHandlePtr, cmd, &result);
    
    // Update statistics
    CmtGetLock(mgr->statsLock);
    mgr->totalProcessed++;
    if (result.errorCode != PSB_SUCCESS) {
        mgr->totalErrors++;
    }
    CmtReleaseLock(mgr->statsLock);
    
    // Handle connection loss
    if (result.errorCode == PSB_ERROR_NOT_CONNECTED || 
        result.errorCode == PSB_ERROR_COMM ||
        result.errorCode == PSB_ERROR_TIMEOUT) {
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
        LogWarningEx(LOG_DEVICE_PSB, "Lost connection during command execution");
    }
    
    // Notify completion
    NotifyCommandComplete(cmd, &result);
    
    // Check if this was part of a transaction
    if (cmd->transactionId != 0) {
        CmtGetLock(mgr->transactionLock);
        Transaction *txn = FindTransaction(mgr, cmd->transactionId);
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
    int delayMs = PSB_QueueGetCommandDelay(cmd->type);
    if (delayMs > 0) {
        Delay(delayMs / 1000.0);
    }
    
    return result.errorCode;
}

static int ExecutePSBCommand(PSB_Handle *handle, QueuedCommand *cmd, PSBCommandResult *result) {
    switch (cmd->type) {
        case PSB_CMD_SET_REMOTE_MODE:
            result->errorCode = PSB_SetRemoteMode(handle, cmd->params.remoteMode.enable);
            break;
            
        case PSB_CMD_SET_OUTPUT_ENABLE:
            result->errorCode = PSB_SetOutputEnable(handle, cmd->params.outputEnable.enable);
            break;
            
        case PSB_CMD_SET_VOLTAGE:
            result->errorCode = PSB_SetVoltage(handle, cmd->params.setVoltage.voltage);
            break;
            
        case PSB_CMD_SET_CURRENT:
            result->errorCode = PSB_SetCurrent(handle, cmd->params.setCurrent.current);
            break;
            
        case PSB_CMD_SET_POWER:
            result->errorCode = PSB_SetPower(handle, cmd->params.setPower.power);
            break;
            
        case PSB_CMD_SET_VOLTAGE_LIMITS:
            result->errorCode = PSB_SetVoltageLimits(handle, 
                cmd->params.voltageLimits.minVoltage,
                cmd->params.voltageLimits.maxVoltage);
            break;
            
        case PSB_CMD_SET_CURRENT_LIMITS:
            result->errorCode = PSB_SetCurrentLimits(handle,
                cmd->params.currentLimits.minCurrent,
                cmd->params.currentLimits.maxCurrent);
            break;
            
        case PSB_CMD_SET_POWER_LIMIT:
            result->errorCode = PSB_SetPowerLimit(handle, cmd->params.powerLimit.maxPower);
            break;
            
        case PSB_CMD_GET_STATUS:
            result->errorCode = PSB_GetStatus(handle, &result->data.status);
            break;
            
        case PSB_CMD_GET_ACTUAL_VALUES:
            result->errorCode = PSB_GetActualValues(handle,
                &result->data.actualValues.voltage,
                &result->data.actualValues.current,
                &result->data.actualValues.power);
            break;
            
        case PSB_CMD_RAW_MODBUS:
            // TODO: Implement raw Modbus command execution
            result->errorCode = PSB_ERROR_NOT_SUPPORTED;
            break;
            
        default:
            result->errorCode = PSB_ERROR_INVALID_PARAM;
            break;
    }
    
    return result->errorCode;
}

static void NotifyCommandComplete(QueuedCommand *cmd, PSBCommandResult *result) {
    // For blocking commands
    if (cmd->completionEvent && cmd->completedPtr) {
        // Lock to safely update shared data
        CmtGetLock(cmd->completionEvent);
        
        // Copy results
        if (cmd->resultPtr) {
            *cmd->resultPtr = *result;
        }
        if (cmd->errorCodePtr) {
            *cmd->errorCodePtr = result->errorCode;
        }
        
        // Set completed flag - this signals the waiting thread
        *cmd->completedPtr = 1;
        
        CmtReleaseLock(cmd->completionEvent);
        
        // DON'T discard the lock here - the blocking thread owns it
    }
    
    // For async commands
    if (cmd->callback) {
        cmd->callback(cmd->id, cmd->type, result, cmd->userData);
    }
}

static int AttemptReconnection(PSBQueueManager *mgr) {
    LogMessageEx(LOG_DEVICE_PSB, "Attempting to reconnect to PSB...");
    
    mgr->reconnectAttempts++;
    
    // Try to reconnect
    int result = ConnectPSB(mgr);
    
    if (result == PSB_SUCCESS) {
        mgr->isConnected = 1;
        mgr->reconnectAttempts = 0;
        LogMessageEx(LOG_DEVICE_PSB, "Successfully reconnected to PSB");
        return PSB_SUCCESS;
    }
    
    // Calculate next retry delay with exponential backoff
    double delay = PSB_QUEUE_RECONNECT_DELAY_MS * pow(2, MIN(mgr->reconnectAttempts - 1, 5));
    delay = MIN(delay, PSB_QUEUE_MAX_RECONNECT_DELAY);
    mgr->lastReconnectTime = Timer() + (delay / 1000.0);
    
    LogWarningEx(LOG_DEVICE_PSB, "Reconnection failed, next attempt in %.1f seconds", delay / 1000.0);
    return PSB_ERROR_COMM;
}

static Transaction* FindTransaction(PSBQueueManager *mgr, TransactionHandle id) {
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        Transaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr && (*txnPtr)->id == id) {
            return *txnPtr;
        }
    }
    return NULL;
}

static void ProcessTransaction(PSBQueueManager *mgr, Transaction *txn) {
    if (!txn->callback) return;
    
    // Collect results
    PSBCommandResult *results = calloc(txn->commandCount, sizeof(PSBCommandResult));
    int successCount = 0;
    int failureCount = 0;
    
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            // Copy result from command
            // Note: In a real implementation, we'd store results in the commands
            if (txn->commands[i]->errorCodePtr && *txn->commands[i]->errorCodePtr == PSB_SUCCESS) {
                successCount++;
            } else {
                failureCount++;
            }
        }
    }
    
    // Call transaction callback
    txn->callback(txn->id, successCount, failureCount, results, txn->userData);
    
    // Clean up
    free(results);
    for (int i = 0; i < txn->commandCount; i++) {
        if (txn->commands[i]) {
            FreeCommand(txn->commands[i]);
        }
    }
    
    // Remove from active list
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        Transaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr == txn) {
            ListRemoveItem(mgr->activeTransactions, 0, i);
            break;
        }
    }
    
    free(txn);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* PSB_QueueGetCommandTypeName(PSBCommandType type) {
    if (type >= 0 && type < PSB_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int PSB_QueueGetCommandDelay(PSBCommandType type) {
    switch (type) {
        case PSB_CMD_SET_REMOTE_MODE:
        case PSB_CMD_SET_OUTPUT_ENABLE:
            return PSB_DELAY_STATE_CHANGE;
            
        case PSB_CMD_SET_VOLTAGE:
        case PSB_CMD_SET_CURRENT:
        case PSB_CMD_SET_POWER:
            return PSB_DELAY_PARAM_CHANGE;
            
        case PSB_CMD_SET_VOLTAGE_LIMITS:
        case PSB_CMD_SET_CURRENT_LIMITS:
        case PSB_CMD_SET_POWER_LIMIT:
            return PSB_DELAY_AFTER_WRITE_REGISTER;
            
        case PSB_CMD_GET_STATUS:
        case PSB_CMD_GET_ACTUAL_VALUES:
            return PSB_DELAY_AFTER_READ;
            
        default:
            return PSB_DELAY_RECOVERY;
    }
}