/******************************************************************************
 * biologic_queue.c
 * 
 * Thread-safe command queue implementation for BioLogic SP-150e
 ******************************************************************************/

#include "biologic_queue.h"
#include "logging.h"
#include "toolbox.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Internal Structures
 ******************************************************************************/

// Queued command structure
struct BioQueuedCommand {
    BioCommandID id;
    BioCommandType type;
    BioPriority priority;
    double timestamp;
    BioCommandParams params;
    BioCommandCallback callback;
    void *userData;
    BioTransactionHandle transactionId;
    
    // For blocking calls
    CmtThreadLockHandle completionEvent;
    BioCommandResult *resultPtr;
    int *errorCodePtr;
    volatile int *completedPtr;  // Pointer to completed flag
    
    // For techniques - we need to copy the params
    TEccParam_t *paramsCopy;
};

// Transaction structure
typedef struct {
    BioTransactionHandle id;
    BioQueuedCommand *commands[BIO_MAX_TRANSACTION_COMMANDS];
    int commandCount;
    BioTransactionCallback callback;
    void *userData;
    bool committed;
} BioTransaction;

// Queue manager structure
struct BioQueueManager {
    // BioLogic device ID
    int32_t deviceID;
    
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
    char lastAddress[64];
    
    // Command tracking
    volatile BioCommandID nextCommandId;
    volatile BioTransactionHandle nextTransactionId;
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
    "CONNECT",
    "DISCONNECT",
    "TEST_CONNECTION",
    "START_CHANNEL",
    "STOP_CHANNEL",
    "GET_CHANNEL_INFO",
    "LOAD_TECHNIQUE",
    "UPDATE_PARAMETERS",
    "GET_CURRENT_VALUES",
    "GET_DATA",
    "SET_HARDWARE_CONFIG",
    "GET_HARDWARE_CONFIG"
};

// Global queue manager for wrapper functions
static BioQueueManager *g_bioQueueManager = NULL;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CVICALLBACK ProcessingThreadFunction(void *functionData);
static BioQueuedCommand* CreateCommand(BioCommandType type, BioCommandParams *params);
static void FreeCommand(BioQueuedCommand *cmd);
static int ProcessCommand(BioQueueManager *mgr, BioQueuedCommand *cmd);
static int ExecuteBioCommand(BioQueueManager *mgr, BioQueuedCommand *cmd, BioCommandResult *result);
static void NotifyCommandComplete(BioQueuedCommand *cmd, BioCommandResult *result);
static int AttemptReconnection(BioQueueManager *mgr);
static BioTransaction* FindTransaction(BioQueueManager *mgr, BioTransactionHandle id);
static void ProcessTransaction(BioQueueManager *mgr, BioTransaction *txn);
static TEccParam_t* CopyEccParams(TEccParams_t *params);

void BIO_SetGlobalQueueManager(BioQueueManager *mgr);
BioQueueManager* BIO_GetGlobalQueueManager(void);

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

BioQueueManager* BIO_QueueInit(const char *address) {
    if (!address || strlen(address) == 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: No address provided");
        return NULL;
    }
    
    BioQueueManager *mgr = calloc(1, sizeof(BioQueueManager));
    if (!mgr) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to allocate manager");
        return NULL;
    }
    
    // Initialize all fields
    mgr->deviceID = -1;  // Start with invalid ID
    mgr->nextCommandId = 1;
    mgr->nextTransactionId = 1;
    mgr->isConnected = 0;
    mgr->shutdownRequested = 0;
    mgr->lastReconnectTime = 0;
    mgr->reconnectAttempts = 0;
    mgr->processingThreadId = 0;
    strncpy(mgr->lastAddress, address, sizeof(mgr->lastAddress) - 1);
    
    // Create queues
    int error = 0;
    
    error = CmtNewTSQ(BIO_QUEUE_HIGH_PRIORITY_SIZE, sizeof(BioQueuedCommand*), 0, &mgr->highPriorityQueue);
    if (error < 0 || mgr->highPriorityQueue == 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create high priority queue, error %d", error);
        goto cleanup;
    }
    
    error = CmtNewTSQ(BIO_QUEUE_NORMAL_PRIORITY_SIZE, sizeof(BioQueuedCommand*), 0, &mgr->normalPriorityQueue);
    if (error < 0 || mgr->normalPriorityQueue == 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create normal priority queue, error %d", error);
        goto cleanup;
    }
    
    error = CmtNewTSQ(BIO_QUEUE_LOW_PRIORITY_SIZE, sizeof(BioQueuedCommand*), 0, &mgr->lowPriorityQueue);
    if (error < 0 || mgr->lowPriorityQueue == 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create low priority queue, error %d", error);
        goto cleanup;
    }
    
    // Create locks
    error = CmtNewLock(NULL, 0, &mgr->commandLock);
    if (error < 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create command lock, error %d", error);
        goto cleanup;
    }
    
    error = CmtNewLock(NULL, 0, &mgr->transactionLock);
    if (error < 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create transaction lock, error %d", error);
        goto cleanup;
    }
    
    error = CmtNewLock(NULL, 0, &mgr->statsLock);
    if (error < 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create stats lock, error %d", error);
        goto cleanup;
    }
    
    // Initialize transaction list
    mgr->activeTransactions = ListCreate(sizeof(BioTransaction*));
    if (!mgr->activeTransactions) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to create transaction list");
        goto cleanup;
    }
    
    // Initialize statistics
    CmtGetLock(mgr->statsLock);
    mgr->totalProcessed = 0;
    mgr->totalErrors = 0;
    mgr->reconnectAttempts = 0;
    CmtReleaseLock(mgr->statsLock);
    
    // Ensure queues are valid before continuing
    Delay(0.1);
    
    // Attempt initial connection
    LogMessageEx(LOG_DEVICE_BIO, "Attempting to connect to BioLogic at %s...", address);
    
    // Initialize BioLogic DLL if needed
    if (!IsBioLogicInitialized()) {
        int initResult = InitializeBioLogic();
        if (initResult != SUCCESS) {
            LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize BioLogic DLL: %d", initResult);
            // Continue anyway - will retry in background
        }
    }
    
    // Try to connect
    if (IsBioLogicInitialized()) {
        TDeviceInfos_t deviceInfo;
        int32_t deviceID;
        
        int connectResult = BL_Connect(address, TIMEOUT, &deviceID, &deviceInfo);
        
        if (connectResult == SUCCESS) {
            mgr->deviceID = deviceID;
            mgr->isConnected = 1;
            
            const char* deviceTypeName = "Unknown";
            switch(deviceInfo.DeviceCode) {
                case KBIO_DEV_SP150E: deviceTypeName = "SP-150e"; break;
                case KBIO_DEV_SP150: deviceTypeName = "SP-150"; break;
                case KBIO_DEV_SP50E: deviceTypeName = "SP-50e"; break;
                case KBIO_DEV_VSP300: deviceTypeName = "VSP-300"; break;
                case KBIO_DEV_VMP300: deviceTypeName = "VMP-300"; break;
                case KBIO_DEV_SP300: deviceTypeName = "SP-300"; break;
                default: break;
            }
            
            LogMessageEx(LOG_DEVICE_BIO, "Successfully connected to BioLogic %s (ID: %d)", 
                       deviceTypeName, deviceID);
            LogMessageEx(LOG_DEVICE_BIO, "  Firmware Version: %d", deviceInfo.FirmwareVersion);
            LogMessageEx(LOG_DEVICE_BIO, "  Channels: %d", deviceInfo.NumberOfChannels);
            
            // Test the connection
            int testResult = BL_TestConnection(deviceID);
            if (testResult != SUCCESS) {
                LogWarningEx(LOG_DEVICE_BIO, "BioLogic connection test failed, will retry");
                BL_Disconnect(deviceID);
                mgr->deviceID = -1;
                mgr->isConnected = 0;
                mgr->lastReconnectTime = Timer();
            }
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Failed initial connection to BioLogic at %s: %s", 
                       address, GetErrorString(connectResult));
            mgr->isConnected = 0;
            mgr->lastReconnectTime = Timer();
        }
    } else {
        LogWarningEx(LOG_DEVICE_BIO, "BioLogic DLL not initialized, will retry in background");
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
    }
    
    // Start processing thread (even if not connected - it will handle reconnection)
    if (g_threadPool > 0) {
        error = CmtScheduleThreadPoolFunction(g_threadPool, ProcessingThreadFunction, 
                                            mgr, &mgr->processingThreadId);
        if (error != 0) {
            LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to start processing thread, error %d", error);
            goto cleanup;
        }
        
        // Give thread time to start
        Delay(0.05);
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: No thread pool available");
        goto cleanup;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "BioLogic queue manager initialized for %s", address);
    return mgr;
    
cleanup:
    LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Initialization failed, cleaning up");
    BIO_QueueShutdown(mgr);
    return NULL;
}

void BIO_QueueShutdown(BioQueueManager *mgr) {
    if (!mgr) return;
    
    LogMessageEx(LOG_DEVICE_BIO, "Shutting down BioLogic queue manager...");
    
    // Signal shutdown
    InterlockedExchange(&mgr->shutdownRequested, 1);
    
    // Wait for processing thread
    if (mgr->processingThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, mgr->processingThreadId,
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Cancel all pending commands
    BIO_QueueCancelAll(mgr);
    
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
            BioTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
            if (txnPtr && *txnPtr) {
                free(*txnPtr);
            }
        }
        ListDispose(mgr->activeTransactions);
    }
    
    free(mgr);
    LogMessageEx(LOG_DEVICE_BIO, "BioLogic queue manager shut down");
}

bool BIO_QueueIsRunning(BioQueueManager *mgr) {
    return mgr && !mgr->shutdownRequested;
}

void BIO_QueueGetStats(BioQueueManager *mgr, BioQueueStats *stats) {
    if (!mgr || !stats) return;
    
    memset(stats, 0, sizeof(BioQueueStats));
    
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
 * Command Queueing Functions
 ******************************************************************************/

int BIO_QueueCommandBlocking(BioQueueManager *mgr, BioCommandType type,
                           BioCommandParams *params, BioPriority priority,
                           BioCommandResult *result, int timeoutMs) {
    if (!mgr || !result) return ERR_INVALID_PARAMETER;
    
    // Create a synchronization structure that we control
    typedef struct {
        CmtThreadLockHandle lock;
        BioCommandResult result;
        int errorCode;
        volatile int completed;
    } SyncBlock;
    
    SyncBlock *sync = calloc(1, sizeof(SyncBlock));
    if (!sync) return ERR_BASE_SYSTEM;
    
    // Create a regular lock for synchronization
    int error = CmtNewLock(NULL, 0, &sync->lock);
    if (error < 0) {
        free(sync);
        return ERR_BASE_SYSTEM;
    }
    
    // Create the command
    BioQueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) {
        CmtDiscardLock(sync->lock);
        free(sync);
        return ERR_BASE_SYSTEM;
    }
    
    cmd->priority = priority;
    cmd->completionEvent = sync->lock;  // Store the lock handle
    cmd->resultPtr = &sync->result;
    cmd->errorCodePtr = &sync->errorCode;
    cmd->completedPtr = &sync->completed;  // Store pointer to completed flag
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case BIO_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case BIO_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case BIO_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to enqueue command type %s", 
                 BIO_QueueGetCommandTypeName(type));
        CmtDiscardLock(sync->lock);
        free(sync);
        FreeCommand(cmd);
        return ERR_BASE_SYSTEM;
    }
    
    // Wait for completion using polling
    double startTime = Timer();
    double timeout = (timeoutMs > 0 ? timeoutMs : 30000) / 1000.0; // Convert to seconds
    int errorCode = ERR_TIMEOUT;
    
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
    
    if (errorCode == ERR_TIMEOUT) {
        LogWarningEx(LOG_DEVICE_BIO, "Command %s timed out", 
                   BIO_QueueGetCommandTypeName(type));
    }
    
    // Clean up
    CmtDiscardLock(sync->lock);
    free(sync);
    
    return errorCode;
}

BioCommandID BIO_QueueCommandAsync(BioQueueManager *mgr, BioCommandType type,
                                 BioCommandParams *params, BioPriority priority,
                                 BioCommandCallback callback, void *userData) {
    if (!mgr) return 0;
    
    BioQueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) return 0;
    
    cmd->priority = priority;
    cmd->callback = callback;
    cmd->userData = userData;
    
    // Select queue based on priority
    CmtTSQHandle queue;
    switch (priority) {
        case BIO_PRIORITY_HIGH:   queue = mgr->highPriorityQueue; break;
        case BIO_PRIORITY_NORMAL: queue = mgr->normalPriorityQueue; break;
        case BIO_PRIORITY_LOW:    queue = mgr->lowPriorityQueue; break;
        default: queue = mgr->normalPriorityQueue; break;
    }
    
    // Enqueue command
    if (CmtWriteTSQData(queue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to enqueue async command type %s", 
                 BIO_QueueGetCommandTypeName(type));
        FreeCommand(cmd);
        return 0;
    }
    
    return cmd->id;
}

bool BIO_QueueHasCommandType(BioQueueManager *mgr, BioCommandType type) {
    if (!mgr) return false;
    
    // For now, simple check for normal priority queue
    int count = 0;
    CmtGetTSQAttribute(mgr->normalPriorityQueue, ATTR_TSQ_ITEMS_IN_QUEUE, &count);
    
    // TODO: Implement proper scanning of queue for command type
    return (type == BIO_CMD_GET_CURRENT_VALUES && count > 0);
}

int BIO_QueueCancelAll(BioQueueManager *mgr) {
    if (!mgr) return ERR_INVALID_PARAMETER;
    
    // Flush all queues
    CmtFlushTSQ(mgr->highPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->normalPriorityQueue, TSQ_FLUSH_ALL, NULL);
    CmtFlushTSQ(mgr->lowPriorityQueue, TSQ_FLUSH_ALL, NULL);
    
    return SUCCESS;
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

BioTransactionHandle BIO_QueueBeginTransaction(BioQueueManager *mgr) {
    if (!mgr) return 0;
    
    BioTransaction *txn = calloc(1, sizeof(BioTransaction));
    if (!txn) return 0;
    
    CmtGetLock(mgr->transactionLock);
    txn->id = mgr->nextTransactionId++;
    ListInsertItem(mgr->activeTransactions, &txn, END_OF_LIST);
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(LOG_DEVICE_BIO, "Started transaction %u", txn->id);
    return txn->id;
}

int BIO_QueueAddToTransaction(BioQueueManager *mgr, BioTransactionHandle txnId,
                            BioCommandType type, BioCommandParams *params) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    BioTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    if (txn->commandCount >= BIO_MAX_TRANSACTION_COMMANDS) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    BioQueuedCommand *cmd = CreateCommand(type, params);
    if (!cmd) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_BASE_SYSTEM;
    }
    
    cmd->transactionId = txnId;
    txn->commands[txn->commandCount++] = cmd;
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogDebugEx(LOG_DEVICE_BIO, "Added %s to transaction %u", 
             BIO_QueueGetCommandTypeName(type), txnId);
    return SUCCESS;
}

int BIO_QueueCommitTransaction(BioQueueManager *mgr, BioTransactionHandle txnId,
                             BioTransactionCallback callback, void *userData) {
    if (!mgr || txnId == 0) return ERR_INVALID_PARAMETER;
    
    CmtGetLock(mgr->transactionLock);
    BioTransaction *txn = FindTransaction(mgr, txnId);
    if (!txn || txn->committed || txn->commandCount == 0) {
        CmtReleaseLock(mgr->transactionLock);
        return ERR_INVALID_PARAMETER;
    }
    
    txn->callback = callback;
    txn->userData = userData;
    txn->committed = true;
    
    // Queue all commands as high priority
    for (int i = 0; i < txn->commandCount; i++) {
        BioQueuedCommand *cmd = txn->commands[i];
        cmd->priority = BIO_PRIORITY_HIGH;
        
        if (CmtWriteTSQData(mgr->highPriorityQueue, &cmd, 1, TSQ_INFINITE_TIMEOUT, NULL) < 0) {
            LogErrorEx(LOG_DEVICE_BIO, "Failed to queue transaction command %d", i);
        }
    }
    
    CmtReleaseLock(mgr->transactionLock);
    
    LogMessageEx(LOG_DEVICE_BIO, "Committed transaction %u with %d commands", 
               txnId, txn->commandCount);
    return SUCCESS;
}

/******************************************************************************
 * Wrapper Functions
 ******************************************************************************/

void BIO_SetGlobalQueueManager(BioQueueManager *mgr) {
    g_bioQueueManager = mgr;
}

int BL_ConnectQueued(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos) {
    if (!g_bioQueueManager) return BL_Connect(address, timeout, pID, pInfos);
    
    BioCommandParams params = {.connect = {.timeout = timeout}};
    strncpy(params.connect.address, address, sizeof(params.connect.address) - 1);
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_CONNECT,
                                       &params, BIO_PRIORITY_HIGH, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        if (pID) *pID = g_bioQueueManager->deviceID;
        if (pInfos) *pInfos = result.data.deviceInfo;
    }
    return error;
}

int BL_DisconnectQueued(int ID) {
    if (!g_bioQueueManager) return BL_Disconnect(ID);
    
    BioCommandParams params = {0};
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_DISCONNECT,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_TestConnectionQueued(int ID) {
    if (!g_bioQueueManager) return BL_TestConnection(ID);
    
    BioCommandParams params = {0};
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_TEST_CONNECTION,
                                  &params, BIO_PRIORITY_NORMAL, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_StartChannelQueued(int ID, uint8_t channel) {
    if (!g_bioQueueManager) return BL_StartChannel(ID, channel);
    
    BioCommandParams params = {.channel = {channel}};
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_START_CHANNEL,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_StopChannelQueued(int ID, uint8_t channel) {
    if (!g_bioQueueManager) return BL_StopChannel(ID, channel);
    
    BioCommandParams params = {.channel = {channel}};
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_STOP_CHANNEL,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_GetChannelInfosQueued(int ID, uint8_t ch, TChannelInfos_t* pInfos) {
    if (!g_bioQueueManager || !pInfos) return BL_GetChannelInfos(ID, ch, pInfos);
    
    BioCommandParams params = {.channel = {ch}};
    BioCommandResult result;
    
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_CHANNEL_INFO,
                                       &params, BIO_PRIORITY_NORMAL, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pInfos = result.data.channelInfo;
    }
    return error;
}

int BL_LoadTechniqueQueued(int ID, uint8_t channel, const char* pFName, 
                         TEccParams_t Params, bool FirstTechnique, 
                         bool LastTechnique, bool DisplayParams) {
    if (!g_bioQueueManager) {
        return BL_LoadTechnique(ID, channel, pFName, Params, 
                              FirstTechnique, LastTechnique, DisplayParams);
    }
    
    BioCommandParams params = {
        .loadTechnique = {
            .channel = channel,
            .params = Params,
            .firstTechnique = FirstTechnique,
            .lastTechnique = LastTechnique,
            .displayParams = DisplayParams
        }
    };
    strncpy(params.loadTechnique.techniquePath, pFName, 
            sizeof(params.loadTechnique.techniquePath) - 1);
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_LOAD_TECHNIQUE,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_GetCurrentValuesQueued(int ID, uint8_t channel, TCurrentValues_t* pValues) {
    if (!g_bioQueueManager || !pValues) return BL_GetCurrentValues(ID, channel, pValues);
    
    BioCommandParams params = {.channel = {channel}};
    BioCommandResult result;
    
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_CURRENT_VALUES,
                                       &params, BIO_PRIORITY_NORMAL, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pValues = result.data.currentValues;
    }
    return error;
}

int BL_GetHardConfQueued(int ID, uint8_t ch, THardwareConf_t* pHardConf) {
    if (!g_bioQueueManager || !pHardConf) return BL_GetHardConf(ID, ch, pHardConf);
    
    BioCommandParams params = {.channel = {ch}};
    BioCommandResult result;
    
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_HARDWARE_CONFIG,
                                       &params, BIO_PRIORITY_NORMAL, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pHardConf = result.data.hardwareConfig;
    }
    return error;
}

int BL_SetHardConfQueued(int ID, uint8_t ch, THardwareConf_t HardConf) {
    if (!g_bioQueueManager) return BL_SetHardConf(ID, ch, HardConf);
    
    BioCommandParams params = {
        .hardwareConfig = {
            .channel = ch,
            .config = HardConf
        }
    };
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_SET_HARDWARE_CONFIG,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

/******************************************************************************
 * Internal Functions
 ******************************************************************************/

static BioQueuedCommand* CreateCommand(BioCommandType type, BioCommandParams *params) {
    static volatile BioCommandID nextId = 1;
    
    BioQueuedCommand *cmd = calloc(1, sizeof(BioQueuedCommand));
    if (!cmd) return NULL;
    
    cmd->id = InterlockedIncrement(&nextId);
    cmd->type = type;
    cmd->timestamp = Timer();
    
    if (params) {
        cmd->params = *params;
        
        // For load technique, we need to copy the ECC params
        if (type == BIO_CMD_LOAD_TECHNIQUE && params->loadTechnique.params.pParams) {
            cmd->paramsCopy = CopyEccParams(&params->loadTechnique.params);
            cmd->params.loadTechnique.params.pParams = cmd->paramsCopy;
        } else if (type == BIO_CMD_UPDATE_PARAMETERS && params->updateParams.params.pParams) {
            cmd->paramsCopy = CopyEccParams(&params->updateParams.params);
            cmd->params.updateParams.params.pParams = cmd->paramsCopy;
        }
    }
    
    return cmd;
}

static void FreeCommand(BioQueuedCommand *cmd) {
    if (!cmd) return;
    
    if (cmd->paramsCopy) {
        free(cmd->paramsCopy);
    }
    
    free(cmd);
}

static int CVICALLBACK ProcessingThreadFunction(void *functionData) {
    BioQueueManager *mgr = (BioQueueManager*)functionData;
    
    // Validate manager
    if (!mgr) {
        LogErrorEx(LOG_DEVICE_BIO, "ProcessingThreadFunction: NULL manager passed");
        return -1;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "BioLogic queue processing thread started");
    
    // Add startup delay to ensure full initialization
    Delay(0.2);
    
    // Validate queues exist before entering main loop
    if (!mgr->highPriorityQueue || !mgr->normalPriorityQueue || !mgr->lowPriorityQueue) {
        LogErrorEx(LOG_DEVICE_BIO, "ProcessingThreadFunction: Invalid queue handles at startup");
        return -1;
    }
    
    while (!mgr->shutdownRequested) {
        BioQueuedCommand *cmd = NULL;
        unsigned int itemsRead = 0;
        int error = 0;
        
        // Re-validate manager state each iteration
        if (!mgr || mgr->shutdownRequested) {
            break;
        }
        
        // Validate all queue handles before use
        if (!mgr->highPriorityQueue || !mgr->normalPriorityQueue || !mgr->lowPriorityQueue) {
            LogErrorEx(LOG_DEVICE_BIO, "ProcessingThreadFunction: Queue handles became invalid");
            Delay(0.5);
            continue;
        }
        
        // Check connection state
        if (!mgr->isConnected && mgr->deviceID >= 0) {
            // Lost connection, try to reconnect
            if (Timer() - mgr->lastReconnectTime > (BIO_QUEUE_RECONNECT_DELAY_MS / 1000.0)) {
                AttemptReconnection(mgr);
            }
            Delay(0.1);
            continue;
        }
        
        // Try to read from queues in priority order
        // High priority queue
        if (mgr->highPriorityQueue) {
            error = CmtReadTSQData(mgr->highPriorityQueue, &cmd, 1, 0, itemsRead);
            if (error >= 0 && itemsRead > 0 && cmd != NULL) {
                // Got high priority command
                LogDebugEx(LOG_DEVICE_BIO, "Processing high priority command");
            } else if (error < 0 && error != -1) {  // -1 is timeout, which is OK
                LogErrorEx(LOG_DEVICE_BIO, "Error reading high priority queue: %d", error);
                cmd = NULL;  // Ensure cmd is NULL on error
            }
        }
        
        // Normal priority queue
        if (!cmd && mgr->normalPriorityQueue) {
            error = CmtReadTSQData(mgr->normalPriorityQueue, &cmd, 1, 0, itemsRead);
            if (error >= 0 && itemsRead > 0 && cmd != NULL) {
                // Got normal priority command
                LogDebugEx(LOG_DEVICE_BIO, "Processing normal priority command");
            } else if (error < 0 && error != -1) {
                LogErrorEx(LOG_DEVICE_BIO, "Error reading normal priority queue: %d", error);
                cmd = NULL;
            }
        }
        
        // Low priority queue
        if (!cmd && mgr->lowPriorityQueue) {
            error = CmtReadTSQData(mgr->lowPriorityQueue, &cmd, 1, 0, itemsRead);
            if (error >= 0 && itemsRead > 0 && cmd != NULL) {
                // Got low priority command
                LogDebugEx(LOG_DEVICE_BIO, "Processing low priority command");
            } else if (error < 0 && error != -1) {
                LogErrorEx(LOG_DEVICE_BIO, "Error reading low priority queue: %d", error);
                cmd = NULL;
            }
        }
        
        // Process command if we got one
        if (cmd) {
            LogDebugEx(LOG_DEVICE_BIO, "Processing command type %s (ID: %u)", 
                     BIO_QueueGetCommandTypeName(cmd->type), cmd->id);
            
            int result = ProcessCommand(mgr, cmd);
            
            if (result != SUCCESS) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to process command %u: %s", 
                         cmd->id, GetBioLogicErrorString(result));
            }
            
            // Free the command after processing
            FreeCommand(cmd);
            cmd = NULL;
        } else {
            // No commands available, yield CPU
            Delay(0.01);  // Small delay to prevent busy waiting
        }
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "BioLogic queue processing thread ending");
    return 0;
}

static int ProcessCommand(BioQueueManager *mgr, BioQueuedCommand *cmd) {
    BioCommandResult result = {0};
    
    LogDebugEx(LOG_DEVICE_BIO, "Processing command: %s (ID: %u)", 
             BIO_QueueGetCommandTypeName(cmd->type), cmd->id);
    
    // Execute the command
    result.errorCode = ExecuteBioCommand(mgr, cmd, &result);
    
    // Update statistics
    CmtGetLock(mgr->statsLock);
    mgr->totalProcessed++;
    if (result.errorCode != SUCCESS) {
        mgr->totalErrors++;
    }
    CmtReleaseLock(mgr->statsLock);
    
    // Handle connection loss
    if (result.errorCode == BL_ERR_NOINSTRUMENTCONNECTED || 
        result.errorCode == BL_ERR_COMM_FAILED) {
        mgr->isConnected = 0;
        mgr->lastReconnectTime = Timer();
        LogWarningEx(LOG_DEVICE_BIO, "Lost connection during command execution");
    }
    
    // Notify completion (this signals the waiting thread for blocking commands)
    NotifyCommandComplete(cmd, &result);
    
    // Check if this was part of a transaction
    if (cmd->transactionId != 0) {
        CmtGetLock(mgr->transactionLock);
        BioTransaction *txn = FindTransaction(mgr, cmd->transactionId);
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
    int delayMs = BIO_QueueGetCommandDelay(cmd->type);
    if (delayMs > 0) {
        Delay(delayMs / 1000.0);
    }
    
    // Note: The command is freed by the processing thread after this function returns
    
    return result.errorCode;
}

static int ExecuteBioCommand(BioQueueManager *mgr, BioQueuedCommand *cmd, BioCommandResult *result) {
    switch (cmd->type) {
        case BIO_CMD_CONNECT:
            result->errorCode = BL_Connect(cmd->params.connect.address, 
                                         cmd->params.connect.timeout,
                                         &mgr->deviceID, &result->data.deviceInfo);
            if (result->errorCode == SUCCESS) {
                mgr->isConnected = 1;
                strncpy(mgr->lastAddress, cmd->params.connect.address, sizeof(mgr->lastAddress) - 1);
            }
            break;
            
        case BIO_CMD_DISCONNECT:
            result->errorCode = BL_Disconnect(mgr->deviceID);
            if (result->errorCode == SUCCESS) {
                mgr->isConnected = 0;
                mgr->deviceID = -1;
            }
            break;
            
        case BIO_CMD_TEST_CONNECTION:
            result->errorCode = BL_TestConnection(mgr->deviceID);
            break;
            
        case BIO_CMD_START_CHANNEL:
            result->errorCode = BL_StartChannel(mgr->deviceID, cmd->params.channel.channel);
            break;
            
        case BIO_CMD_STOP_CHANNEL:
            result->errorCode = BL_StopChannel(mgr->deviceID, cmd->params.channel.channel);
            break;
            
        case BIO_CMD_GET_CHANNEL_INFO:
            result->errorCode = BL_GetChannelInfos(mgr->deviceID, 
                                                 cmd->params.channel.channel,
                                                 &result->data.channelInfo);
            break;
            
        case BIO_CMD_LOAD_TECHNIQUE:
            result->errorCode = BL_LoadTechnique(mgr->deviceID,
                cmd->params.loadTechnique.channel,
                cmd->params.loadTechnique.techniquePath,
                cmd->params.loadTechnique.params,
                cmd->params.loadTechnique.firstTechnique,
                cmd->params.loadTechnique.lastTechnique,
                cmd->params.loadTechnique.displayParams);
            break;
            
        case BIO_CMD_GET_CURRENT_VALUES:
            result->errorCode = BL_GetCurrentValues(mgr->deviceID,
                                                  cmd->params.channel.channel,
                                                  &result->data.currentValues);
            break;
            
        case BIO_CMD_GET_HARDWARE_CONFIG:
            result->errorCode = BL_GetHardConf(mgr->deviceID,
                                             cmd->params.channel.channel,
                                             &result->data.hardwareConfig);
            break;
            
        case BIO_CMD_SET_HARDWARE_CONFIG:
            result->errorCode = BL_SetHardConf(mgr->deviceID,
                                             cmd->params.hardwareConfig.channel,
                                             cmd->params.hardwareConfig.config);
            break;
            
        default:
            result->errorCode = ERR_INVALID_PARAMETER;
            break;
    }
    
    return result->errorCode;
}

static void NotifyCommandComplete(BioQueuedCommand *cmd, BioCommandResult *result) {
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

static int AttemptReconnection(BioQueueManager *mgr) {
    LogMessageEx(LOG_DEVICE_BIO, "Attempting to reconnect to BioLogic...");
    
    mgr->reconnectAttempts++;
    
    // Try to reconnect using the last known address
    if (strlen(mgr->lastAddress) > 0) {
        TDeviceInfos_t deviceInfo;
        int32_t newDeviceID;
        
        int result = BL_Connect(mgr->lastAddress, TIMEOUT, &newDeviceID, &deviceInfo);
        
        if (result == SUCCESS) {
            mgr->deviceID = newDeviceID;
            mgr->isConnected = 1;
            mgr->reconnectAttempts = 0;
            LogMessageEx(LOG_DEVICE_BIO, "Successfully reconnected to BioLogic (ID: %d)", newDeviceID);
            return SUCCESS;
        }
    }
    
    // Calculate next retry delay with exponential backoff
    double delay = BIO_QUEUE_RECONNECT_DELAY_MS * pow(2, MIN(mgr->reconnectAttempts - 1, 5));
    delay = MIN(delay, BIO_QUEUE_MAX_RECONNECT_DELAY);
    mgr->lastReconnectTime = Timer() + (delay / 1000.0);
    
    LogWarningEx(LOG_DEVICE_BIO, "Reconnection failed, next attempt in %.1f seconds", delay / 1000.0);
    return BL_ERR_COMM_FAILED;
}

static BioTransaction* FindTransaction(BioQueueManager *mgr, BioTransactionHandle id) {
    int count = ListNumItems(mgr->activeTransactions);
    for (int i = 1; i <= count; i++) {
        BioTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr && (*txnPtr)->id == id) {
            return *txnPtr;
        }
    }
    return NULL;
}

static void ProcessTransaction(BioQueueManager *mgr, BioTransaction *txn) {
    if (!txn->callback) return;
    
    // Collect results
    BioCommandResult *results = calloc(txn->commandCount, sizeof(BioCommandResult));
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
        BioTransaction **txnPtr = ListGetPtrToItem(mgr->activeTransactions, i);
        if (txnPtr && *txnPtr == txn) {
            ListRemoveItem(mgr->activeTransactions, 0, i);
            break;
        }
    }
    
    free(txn);
}

static TEccParam_t* CopyEccParams(TEccParams_t *params) {
    if (!params || !params->pParams || params->len <= 0) return NULL;
    
    TEccParam_t *copy = malloc(params->len * sizeof(TEccParam_t));
    if (!copy) return NULL;
    
    memcpy(copy, params->pParams, params->len * sizeof(TEccParam_t));
    return copy;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* BIO_QueueGetCommandTypeName(BioCommandType type) {
    if (type >= 0 && type < BIO_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int BIO_QueueGetCommandDelay(BioCommandType type) {
    switch (type) {
        case BIO_CMD_CONNECT:
        case BIO_CMD_DISCONNECT:
            return BIO_DELAY_AFTER_CONNECT;
            
        case BIO_CMD_START_CHANNEL:
            return BIO_DELAY_AFTER_START;
            
        case BIO_CMD_STOP_CHANNEL:
            return BIO_DELAY_AFTER_STOP;
            
        case BIO_CMD_LOAD_TECHNIQUE:
            return BIO_DELAY_AFTER_LOAD_TECHNIQUE;
            
        case BIO_CMD_UPDATE_PARAMETERS:
            return BIO_DELAY_AFTER_PARAMETER;
            
        case BIO_CMD_GET_CURRENT_VALUES:
        case BIO_CMD_GET_DATA:
            return BIO_DELAY_AFTER_DATA_READ;
            
        default:
            return BIO_DELAY_RECOVERY;
    }
}

BioQueueManager* BIO_GetGlobalQueueManager(void) {
    return g_bioQueueManager;
}