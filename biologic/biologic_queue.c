/******************************************************************************
 * biologic_queue.c
 * 
 * Thread-safe command queue implementation for BioLogic SP-150e
 * Updated to use high-level technique functions with state machine approach
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
    "RUN_OCV",
    "RUN_PEIS",
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
 * Helper Functions
 ******************************************************************************/

static BL_RawDataBuffer* CopyRawDataBuffer(BL_RawDataBuffer *src) {
    if (!src || !src->rawData) return NULL;
    
    BL_RawDataBuffer *copy = malloc(sizeof(BL_RawDataBuffer));
    if (!copy) return NULL;
    
    *copy = *src;  // Copy all fields
    
    // Deep copy the data array
    int dataSize = src->bufferSize * sizeof(unsigned int);
    copy->rawData = malloc(dataSize);
    if (!copy->rawData) {
        free(copy);
        return NULL;
    }
    
    memcpy(copy->rawData, src->rawData, dataSize);
    return copy;
}

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
        
        // Get plugged channels
        LogMessageEx(LOG_DEVICE_BIO, "Scanning for plugged channels...");
        uint8_t channelsPlugged[16] = {0};
        result = BL_GetChannelsPlugged(ctx->deviceID, channelsPlugged, 16);
        
        if (result == SUCCESS) {
            for (int i = 0; i < 16; i++) {
                if (channelsPlugged[i]) {
                    LogMessageEx(LOG_DEVICE_BIO, "  Channel %d: PLUGGED", i);
                }
            }
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Failed to get plugged channels: %s - assuming channel 0", 
                       BL_GetErrorString(result));
            channelsPlugged[0] = 1;
        }
        
        // Load firmware
        LogMessageEx(LOG_DEVICE_BIO, "Loading firmware...");
        
        int loadResults[16] = {0};
        result = BL_LoadFirmware(ctx->deviceID, channelsPlugged, loadResults, 16,
                               true, false, NULL, NULL);
        
        if (result == SUCCESS) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware loaded successfully");
        } else if (result == BL_ERR_FIRM_FIRMWARENOTLOADED) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware already loaded");
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Firmware load failed: %s - continuing anyway", 
                       BL_GetErrorString(result));
        }
        
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
            
        case BIO_CMD_RUN_OCV: {
            BL_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start OCV
            cmdResult->errorCode = BL_StartOCV(
                ctx->deviceID,
                cmdParams->runOCV.channel,
                cmdParams->runOCV.duration_s,
                cmdParams->runOCV.sample_interval_s,
                cmdParams->runOCV.record_every_dE,
                cmdParams->runOCV.record_every_dT,
                cmdParams->runOCV.e_range,
                &techContext
            );
            
            if (cmdResult->errorCode != SUCCESS) break;
            
            // Set up progress callback if provided
            if (cmdParams->runOCV.progressCallback) {
                techContext->progressCallback = cmdParams->runOCV.progressCallback;
                techContext->userData = cmdParams->runOCV.userData;
            }
            
            // Poll until complete
            while (!BL_IsTechniqueComplete(techContext)) {
                BL_UpdateTechnique(techContext);
                Delay(0.1);  // Poll every 100ms
            }
            
            // Get results
            BL_RawDataBuffer *rawData = NULL;
            bool partialData = false;
            
            if (techContext->state == BIO_TECH_STATE_ERROR) {
                // Try to get partial data
                if (BL_GetTechniqueRawData(techContext, &rawData) == SUCCESS && rawData) {
                    partialData = true;
                    cmdResult->errorCode = BL_ERR_PARTIAL_DATA;
                } else {
                    cmdResult->errorCode = techContext->lastError;
                }
            } else if (techContext->state == BIO_TECH_STATE_COMPLETED) {
                if (BL_GetTechniqueRawData(techContext, &rawData) == SUCCESS) {
                    cmdResult->errorCode = SUCCESS;
                } else {
                    cmdResult->errorCode = BL_ERR_FUNCTIONFAILED;
                }
            }
            
            // Copy raw data to result
            if (rawData) {
                cmdResult->data.techniqueResult.rawData = CopyRawDataBuffer(rawData);
                cmdResult->data.techniqueResult.elapsedTime = Timer() - startTime;
                cmdResult->data.techniqueResult.finalState = techContext->state;
                cmdResult->data.techniqueResult.partialData = partialData;
            }
            
            // Clean up
            BL_FreeTechniqueContext(techContext);
            break;
        }
        
        case BIO_CMD_RUN_PEIS: {
            BL_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start PEIS
            cmdResult->errorCode = BL_StartPEIS(
                ctx->deviceID,
                cmdParams->runPEIS.channel,
                cmdParams->runPEIS.e_dc,
                cmdParams->runPEIS.amplitude,
                cmdParams->runPEIS.initial_freq,
                cmdParams->runPEIS.final_freq,
                cmdParams->runPEIS.points_per_decade,
                cmdParams->runPEIS.i_range,
                cmdParams->runPEIS.e_range,
                cmdParams->runPEIS.bandwidth,
                &techContext
            );
            
            if (cmdResult->errorCode != SUCCESS) break;
            
            // Set up progress callback if provided
            if (cmdParams->runPEIS.progressCallback) {
                techContext->progressCallback = cmdParams->runPEIS.progressCallback;
                techContext->userData = cmdParams->runPEIS.userData;
            }
            
            // Poll until complete
            while (!BL_IsTechniqueComplete(techContext)) {
                BL_UpdateTechnique(techContext);
                Delay(0.1);
            }
            
            // Get results (same as OCV)
            BL_RawDataBuffer *rawData = NULL;
            bool partialData = false;
            
            if (techContext->state == BIO_TECH_STATE_ERROR) {
                if (BL_GetTechniqueRawData(techContext, &rawData) == SUCCESS && rawData) {
                    partialData = true;
                    cmdResult->errorCode = BL_ERR_PARTIAL_DATA;
                } else {
                    cmdResult->errorCode = techContext->lastError;
                }
            } else if (techContext->state == BIO_TECH_STATE_COMPLETED) {
                if (BL_GetTechniqueRawData(techContext, &rawData) == SUCCESS) {
                    cmdResult->errorCode = SUCCESS;
                } else {
                    cmdResult->errorCode = BL_ERR_FUNCTIONFAILED;
                }
            }
            
            // Copy raw data to result
            if (rawData) {
                cmdResult->data.techniqueResult.rawData = CopyRawDataBuffer(rawData);
                cmdResult->data.techniqueResult.elapsedTime = Timer() - startTime;
                cmdResult->data.techniqueResult.finalState = techContext->state;
                cmdResult->data.techniqueResult.partialData = partialData;
            }
            
            // Clean up
            BL_FreeTechniqueContext(techContext);
            break;
        }
        
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
    
    // No deep copies needed for current parameter types
    
    return params;
}

static void BIO_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    free(params);
}

static void* BIO_AdapterCreateCommandResult(int commandType) {
    BioCommandResult *result = calloc(1, sizeof(BioCommandResult));
    return result;
}

static void BIO_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    
    BioCommandResult *cmdResult = (BioCommandResult*)result;
    
    // Free technique result data
    if ((commandType == BIO_CMD_RUN_OCV || commandType == BIO_CMD_RUN_PEIS) &&
        cmdResult->data.techniqueResult.rawData) {
        BL_FreeTechniqueResult(cmdResult->data.techniqueResult.rawData);
    }
    
    free(result);
}

static void BIO_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    BioCommandResult *destResult = (BioCommandResult*)dest;
    BioCommandResult *srcResult = (BioCommandResult*)src;
    
    *destResult = *srcResult;
    
    // Deep copy technique results
    if ((commandType == BIO_CMD_RUN_OCV || commandType == BIO_CMD_RUN_PEIS) &&
        srcResult->data.techniqueResult.rawData) {
        destResult->data.techniqueResult.rawData = 
            CopyRawDataBuffer(srcResult->data.techniqueResult.rawData);
    }
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
 * High-Level Technique Functions (Blocking)
 ******************************************************************************/

int BL_RunOCVQueued(int ID, uint8_t channel,
                    double duration_s,
                    double sample_interval_s,
                    double record_every_dE,
                    double record_every_dT,
                    int e_range,
                    BL_RawDataBuffer **data,
                    int timeout_ms,
                    BioTechniqueProgressCallback progressCallback,
                    void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartOCV(ID, channel, duration_s, sample_interval_s,
                               record_every_dE, record_every_dT, e_range, &context);
        if (result != SUCCESS) return result;
        
        if (progressCallback) {
            context->progressCallback = progressCallback;
            context->userData = userData;
        }
        
        while (!BL_IsTechniqueComplete(context)) {
            BL_UpdateTechnique(context);
            Delay(0.1);
        }
        
        result = BL_GetTechniqueRawData(context, data);
        BL_FreeTechniqueContext(context);
        return result;
    }
    
    BioCommandParams params = {
        .runOCV = {
            .channel = channel,
            .duration_s = duration_s,
            .sample_interval_s = sample_interval_s,
            .record_every_dE = record_every_dE,
            .record_every_dT = record_every_dT,
            .e_range = e_range,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : 
                         (int)((duration_s + 5) * 1000),
            .progressCallback = progressCallback,
            .userData = userData
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_OCV,
                                       &params, BIO_PRIORITY_HIGH, &result,
                                       params.runOCV.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

int BL_RunPEISQueued(int ID, uint8_t channel,
                     double e_dc,
                     double amplitude,
                     double initial_freq,
                     double final_freq,
                     int points_per_decade,
                     double i_range,
                     double e_range,
                     double bandwidth,
                     BL_RawDataBuffer **data,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartPEIS(ID, channel, e_dc, amplitude, initial_freq,
                                final_freq, points_per_decade, i_range, e_range,
                                bandwidth, &context);
        if (result != SUCCESS) return result;
        
        if (progressCallback) {
            context->progressCallback = progressCallback;
            context->userData = userData;
        }
        
        while (!BL_IsTechniqueComplete(context)) {
            BL_UpdateTechnique(context);
            Delay(0.1);
        }
        
        result = BL_GetTechniqueRawData(context, data);
        BL_FreeTechniqueContext(context);
        return result;
    }
    
    BioCommandParams params = {
        .runPEIS = {
            .channel = channel,
            .e_dc = e_dc,
            .amplitude = amplitude,
            .initial_freq = initial_freq,
            .final_freq = final_freq,
            .points_per_decade = points_per_decade,
            .i_range = i_range,
            .e_range = e_range,
            .bandwidth = bandwidth,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = progressCallback,
            .userData = userData
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_PEIS,
                                       &params, BIO_PRIORITY_HIGH, &result,
                                       params.runPEIS.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

/******************************************************************************
 * High-Level Technique Functions (Async)
 ******************************************************************************/

BioCommandID BL_RunOCVAsync(int ID, uint8_t channel,
                            double duration_s,
                            double sample_interval_s,
                            double record_every_dE,
                            double record_every_dT,
                            int e_range,
                            BioCommandCallback callback,
                            void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
    BioCommandParams params = {
        .runOCV = {
            .channel = channel,
            .duration_s = duration_s,
            .sample_interval_s = sample_interval_s,
            .record_every_dE = record_every_dE,
            .record_every_dT = record_every_dT,
            .e_range = e_range,
            .timeout_ms = (int)((duration_s + 5) * 1000),
            .progressCallback = NULL,  // Progress callbacks not supported in async mode
            .userData = NULL
        }
    };
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_OCV, &params,
                                BIO_PRIORITY_HIGH, callback, userData);
}

BioCommandID BL_RunPEISAsync(int ID, uint8_t channel,
                             double e_dc,
                             double amplitude,
                             double initial_freq,
                             double final_freq,
                             int points_per_decade,
                             double i_range,
                             double e_range,
                             double bandwidth,
                             BioCommandCallback callback,
                             void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
    BioCommandParams params = {
        .runPEIS = {
            .channel = channel,
            .e_dc = e_dc,
            .amplitude = amplitude,
            .initial_freq = initial_freq,
            .final_freq = final_freq,
            .points_per_decade = points_per_decade,
            .i_range = i_range,
            .e_range = e_range,
            .bandwidth = bandwidth,
            .timeout_ms = BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_PEIS, &params,
                                BIO_PRIORITY_HIGH, callback, userData);
}

/******************************************************************************
 * Connection and Configuration Functions
 ******************************************************************************/

void BIO_SetGlobalQueueManager(BioQueueManager *mgr) {
    g_bioQueueManager = mgr;
}

BioQueueManager* BIO_GetGlobalQueueManager(void) {
    return g_bioQueueManager;
}

int BIO_QueueGetDeviceID(BioQueueManager *mgr) {
    if (!mgr) return -1;
    
    BioLogicDeviceContext *context = (BioLogicDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return -1;
    
    return context->deviceID;
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
            
        case BIO_CMD_RUN_OCV:
        case BIO_CMD_RUN_PEIS:
            return BIO_DELAY_AFTER_TECHNIQUE;
            
        case BIO_CMD_SET_HARDWARE_CONFIG:
        case BIO_CMD_GET_HARDWARE_CONFIG:
            return BIO_DELAY_AFTER_CONFIG;
            
        default:
            return BIO_DELAY_RECOVERY;
    }
}

void BL_FreeTechniqueResult(BL_RawDataBuffer *data) {
    if (!data) return;
    
    if (data->rawData) {
        free(data->rawData);
    }
    free(data);
}

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
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