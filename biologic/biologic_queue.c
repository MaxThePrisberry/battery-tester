/******************************************************************************
 * biologic_queue.c
 * 
 * Thread-safe command queue implementation for BioLogic SP-150e
 * Built on top of the generic device queue system
 ******************************************************************************/

#include "biologic_queue.h"
#include "logging.h"
#include <ansi_c.h>

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

// Global queue manager pointer
static BioQueueManager *g_bioQueueManager = NULL;

/******************************************************************************
 * BioLogic Device Context Structure
 ******************************************************************************/

typedef struct {
    int32_t deviceID;
    char lastAddress[64];
    bool isConnected;
} BioLogicDeviceContext;

/******************************************************************************
 * BioLogic Connection Parameters
 ******************************************************************************/

typedef struct {
    char address[64];
    uint8_t timeout;
} BioLogicConnectionParams;

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int BIO_AdapterConnect(void *deviceContext, void *connectionParams);
static int BIO_AdapterDisconnect(void *deviceContext);
static int BIO_AdapterTestConnection(void *deviceContext);
static bool BIO_AdapterIsConnected(void *deviceContext);
static int BIO_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* BIO_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void BIO_AdapterFreeCommandParams(int commandType, void *params);
static void* BIO_AdapterCreateCommandResult(int commandType);
static void BIO_AdapterFreeCommandResult(int commandType, void *result);
static void BIO_AdapterCopyCommandResult(int commandType, void *dest, void *src);
static TEccParam_t* CopyEccParams(TEccParams_t *params);

// BioLogic device adapter
static const DeviceAdapter g_bioAdapter = {
    .deviceName = "BioLogic SP-150e",
    
    // Connection management
    .connect = BIO_AdapterConnect,
    .disconnect = BIO_AdapterDisconnect,
    .testConnection = BIO_AdapterTestConnection,
    .isConnected = BIO_AdapterIsConnected,
    
    // Command execution
    .executeCommand = BIO_AdapterExecuteCommand,
    
    // Command management
    .createCommandParams = BIO_AdapterCreateCommandParams,
    .freeCommandParams = BIO_AdapterFreeCommandParams,
    .createCommandResult = BIO_AdapterCreateCommandResult,
    .freeCommandResult = BIO_AdapterFreeCommandResult,
    .copyCommandResult = BIO_AdapterCopyCommandResult,
    
    // Utility functions
    .getCommandTypeName = (const char* (*)(int))BIO_QueueGetCommandTypeName,
    .getCommandDelay = BIO_QueueGetCommandDelay,
    .getErrorString = GetErrorString,
    
    // Raw command support
    .supportsRawCommands = NULL,
    .executeRawCommand = NULL
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int BIO_AdapterConnect(void *deviceContext, void *connectionParams) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    BioLogicConnectionParams *params = (BioLogicConnectionParams*)connectionParams;
    
    // Initialize BioLogic DLL if needed
    if (!IsBioLogicInitialized()) {
        int initResult = InitializeBioLogic();
        if (initResult != SUCCESS) {
            LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize BioLogic DLL: %d", initResult);
            return initResult;
        }
    }
    
    // Connect to device
    TDeviceInfos_t deviceInfo;
    int result = BL_Connect(params->address, params->timeout, &ctx->deviceID, &deviceInfo);
    
    if (result == SUCCESS) {
        ctx->isConnected = true;
        SAFE_STRCPY(ctx->lastAddress, params->address, sizeof(ctx->lastAddress));
        
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
                   deviceTypeName, ctx->deviceID);
        LogMessageEx(LOG_DEVICE_BIO, "  Device Code: %d", deviceInfo.DeviceCode);
        LogMessageEx(LOG_DEVICE_BIO, "  Firmware Version: %d", deviceInfo.FirmwareVersion);
        LogMessageEx(LOG_DEVICE_BIO, "  Channels: %d", deviceInfo.NumberOfChannels);
        
        // Test the connection
        result = BL_TestConnection(ctx->deviceID);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_BIO, "BioLogic connection test failed");
            BL_Disconnect(ctx->deviceID);
            ctx->deviceID = -1;
            ctx->isConnected = false;
            return result;
        }
        
        // Small delay to let device stabilize
        Delay(0.5);
        
        // Get plugged channels - returns array of 0s and 1s
        LogMessageEx(LOG_DEVICE_BIO, "Scanning for plugged channels...");
        uint8_t channelsPlugged[16] = {0};
        result = BL_GetChannelsPlugged(ctx->deviceID, channelsPlugged, 16);
        
        if (result == SUCCESS) {
            // Log which channels are plugged
            for (int i = 0; i < 16; i++) {
                if (channelsPlugged[i]) {
                    LogMessageEx(LOG_DEVICE_BIO, "  Channel %d: PLUGGED", i);
                }
            }
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Failed to get plugged channels: %s - assuming channel 0", 
                       BL_GetErrorString(result));
            channelsPlugged[0] = 1;  // Assume channel 0 is plugged
        }
        
        // Load firmware using the plugged channels array directly
        LogMessageEx(LOG_DEVICE_BIO, "Loading firmware...");
        
        int loadResults[16] = {0};
        
        // Pass the plugged array [1,0,0,0,...] directly as channels parameter
        result = BL_LoadFirmware(ctx->deviceID,      // Device ID
                               channelsPlugged,       // Plugged channels array [1,0,0,...]
                               loadResults,           // Results array
                               16,                    // Always 16
                               true,                  // ShowGauge = true
                               false,                 // ForceReload = false
                               NULL,                  // NULL kernel path (use internal)
                               NULL);                 // NULL XLX path (use internal)
        
        if (result == SUCCESS) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware loaded successfully");
            
            // Verify channel status
            TChannelInfos_t channelInfo;
            if (BL_GetChannelInfos(ctx->deviceID, 0, &channelInfo) == SUCCESS) {
                LogMessageEx(LOG_DEVICE_BIO, "Channel 0 status:");
                LogMessageEx(LOG_DEVICE_BIO, "  Firmware Code: %d%s", channelInfo.FirmwareCode,
                           channelInfo.FirmwareCode == KIBIO_FIRM_KERNEL ? " (Kernel)" : "");
                LogMessageEx(LOG_DEVICE_BIO, "  State: %d%s", channelInfo.State,
                           channelInfo.State == KBIO_STATE_STOP ? " (Stopped)" : "");
            }
            
        } else if (result == ERR_FIRM_FIRMWARENOTLOADED) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware already loaded");
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Firmware load failed: %s - continuing anyway", 
                       BL_GetErrorString(result));
        }
        
        // Final connection test
        result = BL_TestConnection(ctx->deviceID);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_BIO, "Connection test failed: %s", BL_GetErrorString(result));
        } else {
            LogMessageEx(LOG_DEVICE_BIO, "Connection test passed");
        }
        
        // Always return success to allow operation
        return SUCCESS;
        
    } else {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to connect to BioLogic at %s: %s", 
                   params->address, BL_GetErrorString(result));
        ctx->isConnected = false;
    }
    
    return result;
}

static int BIO_AdapterDisconnect(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    
    if (ctx->isConnected && ctx->deviceID >= 0) {
        int result = BL_Disconnect(ctx->deviceID);
        ctx->isConnected = false;
        ctx->deviceID = -1;
        return result;
    }
    
    return SUCCESS;
}

static int BIO_AdapterTestConnection(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    
    if (!ctx->isConnected || ctx->deviceID < 0) {
        return BL_ERR_NOINSTRUMENTCONNECTED;
    }
    
    return BL_TestConnection(ctx->deviceID);
}

static bool BIO_AdapterIsConnected(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    return ctx->isConnected;
}

static int BIO_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    BioCommandParams *cmdParams = (BioCommandParams*)params;
    BioCommandResult *cmdResult = (BioCommandResult*)result;
    
    // Check connection for most commands
    if (commandType != BIO_CMD_CONNECT && (!ctx->isConnected || ctx->deviceID < 0)) {
        cmdResult->errorCode = BL_ERR_NOINSTRUMENTCONNECTED;
        return cmdResult->errorCode;
    }
    
    switch ((BioCommandType)commandType) {
        case BIO_CMD_CONNECT:
            // Connection is handled by the adapter connect function
            cmdResult->errorCode = SUCCESS;
            break;
            
        case BIO_CMD_DISCONNECT:
            cmdResult->errorCode = BL_Disconnect(ctx->deviceID);
            if (cmdResult->errorCode == SUCCESS) {
                ctx->isConnected = false;
                ctx->deviceID = -1;
            }
            break;
            
        case BIO_CMD_TEST_CONNECTION:
            cmdResult->errorCode = BL_TestConnection(ctx->deviceID);
            break;
            
        case BIO_CMD_START_CHANNEL:
            cmdResult->errorCode = BL_StartChannel(ctx->deviceID, cmdParams->channel.channel);
            break;
            
        case BIO_CMD_STOP_CHANNEL:
            cmdResult->errorCode = BL_StopChannel(ctx->deviceID, cmdParams->channel.channel);
            break;
            
        case BIO_CMD_GET_CHANNEL_INFO:
            cmdResult->errorCode = BL_GetChannelInfos(ctx->deviceID, 
                                                    cmdParams->channel.channel,
                                                    &cmdResult->data.channelInfo);
            break;
            
        case BIO_CMD_LOAD_TECHNIQUE:
            cmdResult->errorCode = BL_LoadTechnique(ctx->deviceID,
                cmdParams->loadTechnique.channel,
                cmdParams->loadTechnique.techniquePath,
                cmdParams->loadTechnique.params,
                cmdParams->loadTechnique.firstTechnique,
                cmdParams->loadTechnique.lastTechnique,
                cmdParams->loadTechnique.displayParams);
            break;
            
        case BIO_CMD_UPDATE_PARAMETERS:
            cmdResult->errorCode = BL_UpdateParameters(ctx->deviceID,
                cmdParams->updateParams.channel,
                cmdParams->updateParams.techniqueIndex,
                cmdParams->updateParams.params,
                cmdParams->updateParams.eccFileName);
            break;
            
        case BIO_CMD_GET_CURRENT_VALUES:
            cmdResult->errorCode = BL_GetCurrentValues(ctx->deviceID,
                                                     cmdParams->channel.channel,
                                                     &cmdResult->data.currentValues);
            break;
            
        case BIO_CMD_GET_DATA:
            cmdResult->errorCode = BL_GetData(ctx->deviceID,
                                            cmdParams->channel.channel,
                                            &cmdResult->data.data.buffer,
                                            &cmdResult->data.data.info,
                                            NULL);
            break;
            
        case BIO_CMD_GET_HARDWARE_CONFIG:
            cmdResult->errorCode = BL_GetHardConf(ctx->deviceID,
                                                cmdParams->channel.channel,
                                                &cmdResult->data.hardwareConfig);
            break;
            
        case BIO_CMD_SET_HARDWARE_CONFIG:
            cmdResult->errorCode = BL_SetHardConf(ctx->deviceID,
                                                cmdParams->hardwareConfig.channel,
                                                cmdParams->hardwareConfig.config);
            break;
            
        default:
            cmdResult->errorCode = ERR_INVALID_PARAMETER;
            break;
    }
    
    return cmdResult->errorCode;
}

static void* BIO_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    BioCommandParams *params = malloc(sizeof(BioCommandParams));
    if (!params) return NULL;
    
    *params = *(BioCommandParams*)sourceParams;
    
    // Handle special cases that need deep copies
    if (commandType == BIO_CMD_LOAD_TECHNIQUE && params->loadTechnique.params.pParams) {
        params->loadTechnique.params.pParams = CopyEccParams(&((BioCommandParams*)sourceParams)->loadTechnique.params);
    } else if (commandType == BIO_CMD_UPDATE_PARAMETERS && params->updateParams.params.pParams) {
        params->updateParams.params.pParams = CopyEccParams(&((BioCommandParams*)sourceParams)->updateParams.params);
    }
    
    return params;
}

static void BIO_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    
    BioCommandParams *cmdParams = (BioCommandParams*)params;
    
    // Free ECC parameters if allocated
    if (commandType == BIO_CMD_LOAD_TECHNIQUE && cmdParams->loadTechnique.params.pParams) {
        free(cmdParams->loadTechnique.params.pParams);
    } else if (commandType == BIO_CMD_UPDATE_PARAMETERS && cmdParams->updateParams.params.pParams) {
        free(cmdParams->updateParams.params.pParams);
    }
    
    free(params);
}

static void* BIO_AdapterCreateCommandResult(int commandType) {
    BioCommandResult *result = calloc(1, sizeof(BioCommandResult));
    return result;
}

static void BIO_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    free(result);
}

static void BIO_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    BioCommandResult *destResult = (BioCommandResult*)dest;
    BioCommandResult *srcResult = (BioCommandResult*)src;
    
    *destResult = *srcResult;
}

static TEccParam_t* CopyEccParams(TEccParams_t *params) {
    if (!params || !params->pParams || params->len <= 0) return NULL;
    
    TEccParam_t *copy = malloc(params->len * sizeof(TEccParam_t));
    if (!copy) return NULL;
    
    memcpy(copy, params->pParams, params->len * sizeof(TEccParam_t));
    return copy;
}

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

BioQueueManager* BIO_QueueInit(const char *address) {
    if (!address || strlen(address) == 0) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: No address provided");
        return NULL;
    }
    
    // Create device context
    BioLogicDeviceContext *context = calloc(1, sizeof(BioLogicDeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to allocate device context");
        return NULL;
    }
    
    context->deviceID = -1;
    context->isConnected = false;
    
    // Create connection parameters
    BioLogicConnectionParams *connParams = calloc(1, sizeof(BioLogicConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    SAFE_STRCPY(connParams->address, address, sizeof(connParams->address));
    connParams->timeout = TIMEOUT;
    
    // Create the generic device queue
    BioQueueManager *mgr = DeviceQueue_Create(&g_bioAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_BIO);
    
    return mgr;
}

void BIO_QueueShutdown(BioQueueManager *mgr) {
    if (!mgr) return;
    
    // Get and free the device context
    BioLogicDeviceContext *context = (BioLogicDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    
    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);
    
    // Free our contexts
    if (context) free(context);
    // Note: Connection params are freed by the generic queue
}

bool BIO_QueueIsRunning(BioQueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void BIO_QueueGetStats(BioQueueManager *mgr, BioQueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

int BIO_QueueCommandBlocking(BioQueueManager *mgr, BioCommandType type,
                           BioCommandParams *params, BioPriority priority,
                           BioCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

BioCommandID BIO_QueueCommandAsync(BioQueueManager *mgr, BioCommandType type,
                                 BioCommandParams *params, BioPriority priority,
                                 BioCommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
}

bool BIO_QueueHasCommandType(BioQueueManager *mgr, BioCommandType type) {
    return DeviceQueue_HasCommandType(mgr, type);
}

int BIO_QueueCancelAll(BioQueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

BioTransactionHandle BIO_QueueBeginTransaction(BioQueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int BIO_QueueAddToTransaction(BioQueueManager *mgr, BioTransactionHandle txn,
                            BioCommandType type, BioCommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int BIO_QueueCommitTransaction(BioQueueManager *mgr, BioTransactionHandle txn,
                             BioTransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

/******************************************************************************
 * Wrapper Functions
 ******************************************************************************/

void BIO_SetGlobalQueueManager(BioQueueManager *mgr) {
    g_bioQueueManager = mgr;
}

BioQueueManager* BIO_GetGlobalQueueManager(void) {
    return g_bioQueueManager;
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
        BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)DeviceQueue_GetDeviceContext(g_bioQueueManager);
        if (ctx && pID) *pID = ctx->deviceID;
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

/******************************************************************************
 * Not Implemented Functions (delegate to generic queue)
 ******************************************************************************/

int BIO_QueueCancelCommand(BioQueueManager *mgr, BioCommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int BIO_QueueCancelByType(BioQueueManager *mgr, BioCommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int BIO_QueueCancelByAge(BioQueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int BIO_QueueCancelTransaction(BioQueueManager *mgr, BioTransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}

int BL_UpdateParametersQueued(int ID, uint8_t channel, int TechIndx, 
                            TEccParams_t Params, const char* EccFileName) {
    if (!g_bioQueueManager) {
        return BL_UpdateParameters(ID, channel, TechIndx, Params, EccFileName);
    }
    
    BioCommandParams params = {
        .updateParams = {
            .channel = channel,
            .techniqueIndex = TechIndx,
            .params = Params
        }
    };
    strncpy(params.updateParams.eccFileName, EccFileName, 
            sizeof(params.updateParams.eccFileName) - 1);
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_UPDATE_PARAMETERS,
                                  &params, BIO_PRIORITY_HIGH, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_GetDataQueued(int ID, uint8_t channel, TDataBuffer_t* pBuf, 
                   TDataInfos_t* pInfos, TCurrentValues_t* pValues) {
    if (!g_bioQueueManager || !pBuf || !pInfos) {
        return BL_GetData(ID, channel, pBuf, pInfos, pValues);
    }
    
    BioCommandParams params = {.channel = {channel}};
    BioCommandResult result;
    
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_DATA,
                                       &params, BIO_PRIORITY_NORMAL, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pBuf = result.data.data.buffer;
        *pInfos = result.data.data.info;
        // Note: pValues is not filled by this command
    }
    return error;
}