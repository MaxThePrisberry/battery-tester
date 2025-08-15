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
    "RUN_GEIS",
    "SET_HARDWARE_CONFIG",
    "GET_HARDWARE_CONFIG",
    "STOP_CHANNEL",
    "GET_CURRENT_VALUES",
    "GET_CHANNEL_INFOS",
    "IS_CHANNEL_PLUGGED",
    "GET_CHANNELS_PLUGGED",
    "START_CHANNEL",
    "GET_DATA",
    "GET_EXPERIMENT_INFOS",
    "SET_EXPERIMENT_INFOS",
    "LOAD_FIRMWARE",
    "GET_LIB_VERSION",
    "GET_MESSAGE"
};

// Global queue manager pointer
static BioQueueManager *g_bioQueueManager = NULL;

// Queue a command (blocking)
static int BIO_QueueCommandBlocking(BioQueueManager *mgr, BioCommandType type,
                           BioCommandParams *params, DevicePriority priority,
                           BioCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
static BioCommandID BIO_QueueCommandAsync(BioQueueManager *mgr, BioCommandType type,
                                 BioCommandParams *params, DevicePriority priority,
                                 BioCommandCallback callback, void *userData);

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
    .getErrorString = GetErrorString
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int BIO_AdapterConnect(void *deviceContext, void *connectionParams) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    BioConnectCommand *params = (BioConnectCommand*)connectionParams;
    
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
    int result = BIO_Connect(params->address, params->timeout, &ctx->deviceID, &deviceInfo);
    
    if (result == SUCCESS) {
        ctx->isConnected = true;
        strncpy(ctx->lastAddress, params->address, sizeof(ctx->lastAddress));
        
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
        result = BIO_TestConnection(ctx->deviceID);
        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_BIO, "BioLogic connection test failed");
            BIO_Disconnect(ctx->deviceID);
            ctx->deviceID = -1;
            ctx->isConnected = false;
            return result;
        }
        
        // Small delay to let device stabilize
        Delay(0.5);
        
        // Get plugged channels
        LogMessageEx(LOG_DEVICE_BIO, "Scanning for plugged channels...");
        uint8_t channelsPlugged[16] = {0};
        result = BIO_GetChannelsPlugged(ctx->deviceID, channelsPlugged, 16);
        
        if (result == SUCCESS) {
            for (int i = 0; i < 16; i++) {
                if (channelsPlugged[i]) {
                    LogMessageEx(LOG_DEVICE_BIO, "  Channel %d: PLUGGED", i);
                }
            }
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Failed to get plugged channels: %s - assuming channel 0", 
                       BIO_GetErrorString(result));
            channelsPlugged[0] = 1;
        }
        
        // Load firmware
        LogMessageEx(LOG_DEVICE_BIO, "Loading firmware...");
        
        int loadResults[16] = {0};
        result = BIO_LoadFirmware(ctx->deviceID, channelsPlugged, loadResults, 16,
                               true, false, NULL, NULL);
        
        if (result == SUCCESS) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware loaded successfully");
        } else if (result == BIO_DEV_FIRM_FIRMWARENOTLOADED) {
            LogMessageEx(LOG_DEVICE_BIO, "Firmware already loaded");
        } else {
            LogWarningEx(LOG_DEVICE_BIO, "Firmware load failed: %s - continuing anyway", 
                       BIO_GetErrorString(result));
        }
        
        return SUCCESS;
        
    } else {
        LogWarningEx(LOG_DEVICE_BIO, "Failed to connect to BioLogic at %s: %s", 
                   params->address, BIO_GetErrorString(result));
        ctx->isConnected = false;
    }
    
    return result;
}

static int BIO_AdapterDisconnect(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    
    if (ctx->isConnected && ctx->deviceID >= 0) {
        int result = BIO_Disconnect(ctx->deviceID);
        ctx->isConnected = false;
        ctx->deviceID = -1;
        return result;
    }
    
    return SUCCESS;
}

static int BIO_AdapterTestConnection(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    
    if (!ctx->isConnected || ctx->deviceID < 0) {
        return BIO_DEV_NOINSTRUMENTCONNECTED;
    }
    
    return BIO_TestConnection(ctx->deviceID);
}

static bool BIO_AdapterIsConnected(void *deviceContext) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    return ctx->isConnected;
}

static int BIO_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)deviceContext;
    BioCommandResult *cmdResult = (BioCommandResult*)result;
    
    // Check connection for most commands
    if (commandType != BIO_CMD_CONNECT && (!ctx->isConnected || ctx->deviceID < 0)) {
        cmdResult->errorCode = BIO_DEV_NOINSTRUMENTCONNECTED;
        return cmdResult->errorCode;
    }
    
    switch ((BioCommandType)commandType) {
        case BIO_CMD_CONNECT: {
            // Connection is handled by the adapter connect function
            cmdResult->errorCode = SUCCESS;
            break;
        }   
        
        case BIO_CMD_DISCONNECT: {
            cmdResult->errorCode = BIO_Disconnect(ctx->deviceID);
            if (cmdResult->errorCode == SUCCESS) {
                ctx->isConnected = false;
                ctx->deviceID = -1;
            }
            break;
        }
            
        case BIO_CMD_TEST_CONNECTION: {
            cmdResult->errorCode = BIO_TestConnection(ctx->deviceID);
            break;
        }
            
        case BIO_CMD_RUN_OCV: {
            BioOCVCommand *cmd = (BioOCVCommand*)params;
            BIO_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start OCV
            cmdResult->errorCode = BIO_StartOCV(
                ctx->deviceID,
                cmd->base.channel,
                cmd->duration_s,
                cmd->sample_interval_s,
                cmd->record_every_dE,
                cmd->record_every_dT,
                cmd->e_range,
                cmd->processData,
                &techContext
            );
            
            if (cmdResult->errorCode != SUCCESS) break;
            
            // Set up progress callback if provided
            if (cmd->base.progressCallback) {
                techContext->progressCallback = cmd->base.progressCallback;
                techContext->userData = cmd->base.userData;
            }
            
            // Poll until complete
            while (!BIO_IsTechniqueComplete(techContext)) {
                BIO_UpdateTechnique(techContext);
                Delay(0.1);  // Poll every 100ms
            }
            
            // Create combined result
            BIO_TechniqueData *combinedResult = calloc(1, sizeof(BIO_TechniqueData));
            if (!combinedResult) {
                cmdResult->errorCode = BIO_ERR_MEMORY_ALLOCATION_FAILED;
                BIO_FreeTechniqueContext(techContext);
                break;
            }
            
            // Get results
            BIO_RawDataBuffer *rawData = NULL;
            bool partialData = false;
            
            if (techContext->state == BIO_TECH_STATE_ERROR) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS && rawData) {
                    partialData = true;
                    cmdResult->errorCode = BIO_ERR_PARTIAL_DATA;
                } else {
                    cmdResult->errorCode = techContext->lastError;
                }
            } else if (techContext->state == BIO_TECH_STATE_COMPLETED) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS) {
                    cmdResult->errorCode = SUCCESS;
                } else {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
            }
            
            // Copy data to combined result
            if (rawData) {
                combinedResult->rawData = BIO_CopyRawDataBuffer(rawData);
                if (!combinedResult->rawData) {
                    cmdResult->errorCode = BIO_ERR_DATA_COPY_FAILED;
                    free(combinedResult);
                    BIO_FreeTechniqueContext(techContext);
                    break;
                }
                
                // Transfer converted data ownership if available
                if (techContext->convertedData) {
                    combinedResult->convertedData = techContext->convertedData;
                    techContext->convertedData = NULL; // Transfer ownership
                }
                
                cmdResult->data.techniqueResult.techniqueData = combinedResult;
                cmdResult->data.techniqueResult.elapsedTime = Timer() - startTime;
                cmdResult->data.techniqueResult.finalState = techContext->state;
                cmdResult->data.techniqueResult.partialData = partialData;
            } else {
                if (cmdResult->errorCode == SUCCESS) {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
                free(combinedResult);
            }
            
            // Clean up
            BIO_FreeTechniqueContext(techContext);
            break;
        }
        
        case BIO_CMD_RUN_PEIS: {
            BioPEISCommand *cmd = (BioPEISCommand*)params;
            BIO_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start PEIS
            cmdResult->errorCode = BIO_StartPEIS(
                ctx->deviceID,
                cmd->base.channel,
                cmd->vs_initial,
                cmd->initial_voltage_step,
                cmd->duration_step,
                cmd->record_every_dT,
                cmd->record_every_dI,
                cmd->initial_freq,
                cmd->final_freq,
                cmd->sweep_linear,
                cmd->amplitude_voltage,
                cmd->frequency_number,
                cmd->average_n_times,
                cmd->correction,
                cmd->wait_for_steady,
                cmd->processData,
                &techContext
            );
            
            if (cmdResult->errorCode != SUCCESS) break;
            
            // Set up progress callback if provided
            if (cmd->base.progressCallback) {
                techContext->progressCallback = cmd->base.progressCallback;
                techContext->userData = cmd->base.userData;
            }
            
            // Poll until complete
            while (!BIO_IsTechniqueComplete(techContext)) {
                BIO_UpdateTechnique(techContext);
                Delay(0.1);
            }
            
            // Create combined result
            BIO_TechniqueData *combinedResult = calloc(1, sizeof(BIO_TechniqueData));
            if (!combinedResult) {
                cmdResult->errorCode = BIO_ERR_MEMORY_ALLOCATION_FAILED;
                BIO_FreeTechniqueContext(techContext);
                break;
            }
            
            // Get results
            BIO_RawDataBuffer *rawData = NULL;
            bool partialData = false;
            
            if (techContext->state == BIO_TECH_STATE_ERROR) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS && rawData) {
                    partialData = true;
                    cmdResult->errorCode = BIO_ERR_PARTIAL_DATA;
                } else {
                    cmdResult->errorCode = techContext->lastError;
                }
            } else if (techContext->state == BIO_TECH_STATE_COMPLETED) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS) {
                    cmdResult->errorCode = SUCCESS;
                } else {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
            }
            
            // Copy data to combined result
            if (rawData) {
                combinedResult->rawData = BIO_CopyRawDataBuffer(rawData);
                if (!combinedResult->rawData) {
                    cmdResult->errorCode = BIO_ERR_DATA_COPY_FAILED;
                    free(combinedResult);
                    BIO_FreeTechniqueContext(techContext);
                    break;
                }
                
                // Transfer converted data ownership if available
                if (techContext->convertedData) {
                    combinedResult->convertedData = techContext->convertedData;
                    techContext->convertedData = NULL; // Transfer ownership
                }
                
                cmdResult->data.techniqueResult.techniqueData = combinedResult;
                cmdResult->data.techniqueResult.elapsedTime = Timer() - startTime;
                cmdResult->data.techniqueResult.finalState = techContext->state;
                cmdResult->data.techniqueResult.partialData = partialData;
            } else {
                if (cmdResult->errorCode == SUCCESS) {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
                free(combinedResult);
            }
            
            // Clean up
            BIO_FreeTechniqueContext(techContext);
            break;
        }
        
        case BIO_CMD_RUN_GEIS: {
            BioGEISCommand *cmd = (BioGEISCommand*)params;
            BIO_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start GEIS
            cmdResult->errorCode = BIO_StartGEIS(
                ctx->deviceID,
                cmd->base.channel,
                cmd->vs_initial,
                cmd->initial_current_step,
                cmd->duration_step,
                cmd->record_every_dT,
                cmd->record_every_dE,
                cmd->initial_freq,
                cmd->final_freq,
                cmd->sweep_linear,
                cmd->amplitude_current,
                cmd->frequency_number,
                cmd->average_n_times,
                cmd->correction,
                cmd->wait_for_steady,
                cmd->i_range,
                cmd->processData,
                &techContext
            );
            
            if (cmdResult->errorCode != SUCCESS) break;
            
            // Set up progress callback if provided
            if (cmd->base.progressCallback) {
                techContext->progressCallback = cmd->base.progressCallback;
                techContext->userData = cmd->base.userData;
            }
            
            // Poll until complete
            while (!BIO_IsTechniqueComplete(techContext)) {
                BIO_UpdateTechnique(techContext);
                Delay(0.1);
            }
            
            // Create combined result
            BIO_TechniqueData *combinedResult = calloc(1, sizeof(BIO_TechniqueData));
            if (!combinedResult) {
                cmdResult->errorCode = BIO_ERR_MEMORY_ALLOCATION_FAILED;
                BIO_FreeTechniqueContext(techContext);
                break;
            }
            
            // Get results
            BIO_RawDataBuffer *rawData = NULL;
            bool partialData = false;
            
            if (techContext->state == BIO_TECH_STATE_ERROR) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS && rawData) {
                    partialData = true;
                    cmdResult->errorCode = BIO_ERR_PARTIAL_DATA;
                } else {
                    cmdResult->errorCode = techContext->lastError;
                }
            } else if (techContext->state == BIO_TECH_STATE_COMPLETED) {
                if (BIO_GetTechniqueRawData(techContext, &rawData) == SUCCESS) {
                    cmdResult->errorCode = SUCCESS;
                } else {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
            }
            
            // Copy data to combined result
            if (rawData) {
                combinedResult->rawData = BIO_CopyRawDataBuffer(rawData);
                if (!combinedResult->rawData) {
                    cmdResult->errorCode = BIO_ERR_DATA_COPY_FAILED;
                    free(combinedResult);
                    BIO_FreeTechniqueContext(techContext);
                    break;
                }
                
                // Transfer converted data ownership if available
                if (techContext->convertedData) {
                    combinedResult->convertedData = techContext->convertedData;
                    techContext->convertedData = NULL; // Transfer ownership
                }
                
                cmdResult->data.techniqueResult.techniqueData = combinedResult;
                cmdResult->data.techniqueResult.elapsedTime = Timer() - startTime;
                cmdResult->data.techniqueResult.finalState = techContext->state;
                cmdResult->data.techniqueResult.partialData = partialData;
            } else {
                if (cmdResult->errorCode == SUCCESS) {
                    cmdResult->errorCode = BIO_ERR_NO_DATA_RETRIEVED;
                }
                free(combinedResult);
            }
            
            // Clean up
            BIO_FreeTechniqueContext(techContext);
            break;
		}
        
        case BIO_CMD_GET_HARDWARE_CONFIG: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_GetHardConf(ctx->deviceID,
                                                cmd->base.channel,
                                                &cmdResult->data.hardwareConfig);
            break;
        }
            
        case BIO_CMD_SET_HARDWARE_CONFIG: {
            BioHardwareConfigCommand *cmd = (BioHardwareConfigCommand*)params;
            cmdResult->errorCode = BIO_SetHardConf(ctx->deviceID,
                                                cmd->base.channel,
                                                cmd->config);
            break;
        }
        
        case BIO_CMD_STOP_CHANNEL: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_StopChannel(ctx->deviceID, cmd->base.channel);
            break;
        }
        
        case BIO_CMD_START_CHANNEL: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_StartChannel(ctx->deviceID, cmd->base.channel);
            break;
        }
        
        case BIO_CMD_GET_CURRENT_VALUES: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_GetCurrentValues(ctx->deviceID, cmd->base.channel,
                                                      &cmdResult->data.currentValues);
            break;
        }
        
        case BIO_CMD_GET_CHANNEL_INFOS: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_GetChannelInfos(ctx->deviceID, cmd->base.channel,
                                                     &cmdResult->data.channelInfos);
            break;
        }
        
        case BIO_CMD_IS_CHANNEL_PLUGGED: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->data.isPlugged = BIO_IsChannelPlugged(ctx->deviceID, cmd->base.channel);
            cmdResult->errorCode = SUCCESS;
            break;
        }
        
        case BIO_CMD_GET_CHANNELS_PLUGGED: {
            BioGetChannelsPluggedCommand *cmd = (BioGetChannelsPluggedCommand*)params;
            uint8_t channelsPlugged[16] = {0};
            cmdResult->errorCode = BIO_GetChannelsPlugged(ctx->deviceID, channelsPlugged, cmd->maxChannels);
            if (cmdResult->errorCode == SUCCESS) {
                memcpy(cmdResult->data.channelsPlugged, channelsPlugged, 
                       cmd->maxChannels * sizeof(uint8_t));
            }
            break;
        }
        
        case BIO_CMD_GET_DATA: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            // Allocate buffers for data
            TDataBuffer_t dataBuffer;
            TDataInfos_t dataInfo;
            TCurrentValues_t currentValues;
            
            cmdResult->errorCode = BIO_GetData(ctx->deviceID, cmd->base.channel,
                                             &dataBuffer, &dataInfo, &currentValues);
            
            if (cmdResult->errorCode == SUCCESS) {
                // Copy data info and current values
                cmdResult->data.dataResult.dataInfo = dataInfo;
                cmdResult->data.dataResult.currentValues = currentValues;
                
                // Copy raw data
                int dataSize = dataInfo.NbRows * dataInfo.NbCols;
                cmdResult->data.dataResult.rawData = malloc(dataSize * sizeof(unsigned int));
                if (cmdResult->data.dataResult.rawData) {
                    memcpy(cmdResult->data.dataResult.rawData, dataBuffer.data,
                           dataSize * sizeof(unsigned int));
                } else {
                    cmdResult->errorCode = BIO_ERR_MEMORY_ALLOCATION_FAILED;
                }
            }
            break;
        }
        
        case BIO_CMD_GET_EXPERIMENT_INFOS: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BIO_GetExperimentInfos(ctx->deviceID, cmd->base.channel,
                                                        &cmdResult->data.experimentInfos);
            break;
        }
        
        case BIO_CMD_SET_EXPERIMENT_INFOS: {
            BioSetExperimentInfosCommand *cmd = (BioSetExperimentInfosCommand*)params;
            cmdResult->errorCode = BIO_SetExperimentInfos(ctx->deviceID, cmd->base.channel,
                                                        cmd->expInfo);
            break;
        }
        
        case BIO_CMD_LOAD_FIRMWARE: {
            BioLoadFirmwareCommand *cmd = (BioLoadFirmwareCommand*)params;
            int loadResults[16] = {0};
            cmdResult->errorCode = BIO_LoadFirmware(ctx->deviceID, cmd->channels, loadResults,
                                                  cmd->numChannels, cmd->showGauge, cmd->forceReload,
                                                  cmd->binFile[0] ? cmd->binFile : NULL,
                                                  cmd->xlxFile[0] ? cmd->xlxFile : NULL);
            if (cmdResult->errorCode == SUCCESS) {
                memcpy(cmdResult->data.firmwareResults, loadResults, 
                       cmd->numChannels * sizeof(int));
            }
            break;
        }
        
        case BIO_CMD_GET_LIB_VERSION: {
            char version[256];
            unsigned int size = sizeof(version);
            cmdResult->errorCode = BIO_GetLibVersion(version, &size);
            if (cmdResult->errorCode == SUCCESS) {
                strncpy(cmdResult->data.version, version, sizeof(cmdResult->data.version) - 1);
                cmdResult->data.version[sizeof(cmdResult->data.version) - 1] = '\0';
            }
            break;
        }
        
        case BIO_CMD_GET_MESSAGE: {
            BioGetMessageCommand *cmd = (BioGetMessageCommand*)params;
            char message[1024];
            unsigned int size = cmd->maxSize;
            cmdResult->errorCode = BIO_GetMessage(ctx->deviceID, cmd->base.channel, message, &size);
            if (cmdResult->errorCode == SUCCESS) {
                cmdResult->data.message = malloc(size + 1);
                if (cmdResult->data.message) {
                    memcpy(cmdResult->data.message, message, size);
                    cmdResult->data.message[size] = '\0';
                } else {
                    cmdResult->errorCode = BIO_ERR_MEMORY_ALLOCATION_FAILED;
                }
            }
            break;
        }
            
        default:
            cmdResult->errorCode = BIO_DEV_INVALIDPARAMETERS;
            break;
    }
    
    return cmdResult->errorCode;
}

static void* BIO_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    switch (commandType) {
        case BIO_CMD_CONNECT: {
            BioConnectCommand *cmd = malloc(sizeof(BioConnectCommand));
            if (cmd) *cmd = *(BioConnectCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_DISCONNECT:
        case BIO_CMD_TEST_CONNECTION: {
            BioChannelCommand *cmd = malloc(sizeof(BioChannelCommand));
            if (cmd) *cmd = *(BioChannelCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_RUN_OCV: {
            BioOCVCommand *cmd = malloc(sizeof(BioOCVCommand));
            if (cmd) *cmd = *(BioOCVCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_RUN_PEIS: {
            BioPEISCommand *cmd = malloc(sizeof(BioPEISCommand));
            if (cmd) *cmd = *(BioPEISCommand*)sourceParams;
            return cmd;
        }
		
		case BIO_CMD_RUN_GEIS: {
		    BioGEISCommand *cmd = malloc(sizeof(BioGEISCommand));
		    if (cmd) *cmd = *(BioGEISCommand*)sourceParams;
		    return cmd;
		}
        
        case BIO_CMD_GET_HARDWARE_CONFIG: {
            BioChannelCommand *cmd = malloc(sizeof(BioChannelCommand));
            if (cmd) *cmd = *(BioChannelCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_SET_HARDWARE_CONFIG: {
            BioHardwareConfigCommand *cmd = malloc(sizeof(BioHardwareConfigCommand));
            if (cmd) *cmd = *(BioHardwareConfigCommand*)sourceParams;
            return cmd;
        }
		
		case BIO_CMD_STOP_CHANNEL:
        case BIO_CMD_START_CHANNEL:
        case BIO_CMD_GET_CURRENT_VALUES:
        case BIO_CMD_GET_CHANNEL_INFOS:
        case BIO_CMD_IS_CHANNEL_PLUGGED:
        case BIO_CMD_GET_DATA:
        case BIO_CMD_GET_EXPERIMENT_INFOS: {
            BioChannelCommand *cmd = malloc(sizeof(BioChannelCommand));
            if (cmd) *cmd = *(BioChannelCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_GET_CHANNELS_PLUGGED: {
            BioGetChannelsPluggedCommand *cmd = malloc(sizeof(BioGetChannelsPluggedCommand));
            if (cmd) *cmd = *(BioGetChannelsPluggedCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_SET_EXPERIMENT_INFOS: {
            BioSetExperimentInfosCommand *cmd = malloc(sizeof(BioSetExperimentInfosCommand));
            if (cmd) *cmd = *(BioSetExperimentInfosCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_LOAD_FIRMWARE: {
            BioLoadFirmwareCommand *cmd = malloc(sizeof(BioLoadFirmwareCommand));
            if (cmd) *cmd = *(BioLoadFirmwareCommand*)sourceParams;
            return cmd;
        }
        
        case BIO_CMD_GET_LIB_VERSION: {
            // No params needed
            return NULL;
        }
        
        case BIO_CMD_GET_MESSAGE: {
            BioGetMessageCommand *cmd = malloc(sizeof(BioGetMessageCommand));
            if (cmd) *cmd = *(BioGetMessageCommand*)sourceParams;
            return cmd;
        }
        
        default:
            return NULL;
    }
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
	if ((commandType == BIO_CMD_RUN_OCV || commandType == BIO_CMD_RUN_PEIS ||
			commandType == BIO_CMD_RUN_GEIS) &&
	    cmdResult->data.techniqueResult.techniqueData) {
	    BIO_FreeTechniqueData(cmdResult->data.techniqueResult.techniqueData);
	}
	
    if (commandType == BIO_CMD_GET_DATA && cmdResult->data.dataResult.rawData) {
        free(cmdResult->data.dataResult.rawData);
    }
    
    if (commandType == BIO_CMD_GET_MESSAGE && cmdResult->data.message) {
        free(cmdResult->data.message);
    }
    
    free(result);
}

static void BIO_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    BioCommandResult *destResult = (BioCommandResult*)dest;
    BioCommandResult *srcResult = (BioCommandResult*)src;
    
    *destResult = *srcResult;
    
    // For technique results, transfer ownership
	if ((commandType == BIO_CMD_RUN_OCV || commandType == BIO_CMD_RUN_PEIS || commandType == BIO_CMD_RUN_GEIS) &&
	    srcResult->data.techniqueResult.techniqueData) {
	    destResult->data.techniqueResult.techniqueData = 
	        srcResult->data.techniqueResult.techniqueData;
	    srcResult->data.techniqueResult.techniqueData = NULL; // Transfer ownership
	}
	
    if (commandType == BIO_CMD_GET_DATA && srcResult->data.dataResult.rawData) {
        int dataSize = srcResult->data.dataResult.dataInfo.NbRows * 
                      srcResult->data.dataResult.dataInfo.NbCols;
        destResult->data.dataResult.rawData = malloc(dataSize * sizeof(unsigned int));
        if (destResult->data.dataResult.rawData) {
            memcpy(destResult->data.dataResult.rawData, srcResult->data.dataResult.rawData,
                   dataSize * sizeof(unsigned int));
        }
        srcResult->data.dataResult.rawData = NULL; // Transfer ownership
    }
    
    if (commandType == BIO_CMD_GET_MESSAGE && srcResult->data.message) {
        destResult->data.message = srcResult->data.message;
        srcResult->data.message = NULL; // Transfer ownership
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
    BioConnectCommand *connParams = calloc(1, sizeof(BioConnectCommand));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_BIO, "BIO_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    // Initialize base parameters
    connParams->base.type = BIO_CMD_CONNECT;
    connParams->base.channel = 0;
    connParams->base.timeout_ms = TIMEOUT * 1000;
    connParams->base.progressCallback = NULL;
    connParams->base.userData = NULL;
    
    // Set connection-specific parameters
    strncpy(connParams->address, address, sizeof(connParams->address));
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

static int BIO_QueueCommandBlocking(BioQueueManager *mgr, BioCommandType type,
                           BioCommandParams *params, DevicePriority priority,
                           BioCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

static BioCommandID BIO_QueueCommandAsync(BioQueueManager *mgr, BioCommandType type,
                                 BioCommandParams *params, DevicePriority priority,
                                 BioCommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
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

int BIO_RunOCVQueued(int ID, uint8_t channel,
                    double duration_s,
                    double sample_interval_s,
                    double record_every_dE,
                    double record_every_dT,
                    int e_range,
					bool processData,
                    BIO_TechniqueData **result,
                    int timeout_ms,
                    DevicePriority priority,
                    BioTechniqueProgressCallback progressCallback,
                    void *userData) {

    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    BioOCVCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_OCV,
            .channel = channel,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : (int)((duration_s + 5) * 1000),
            .progressCallback = progressCallback,
            .userData = userData
        },
        .duration_s = duration_s,
        .sample_interval_s = sample_interval_s,
        .record_every_dE = record_every_dE,
        .record_every_dT = record_every_dT,
        .e_range = e_range,
		.processData = processData
    };
    
    BioCommandResult cmdResult;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_OCV,
                                       (BioCommandParams*)&cmd, priority, &cmdResult,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BIO_ERR_PARTIAL_DATA) && result) {
        *result = cmdResult.data.techniqueResult.techniqueData;
        // Transfer ownership - don't free in adapter
        cmdResult.data.techniqueResult.techniqueData = NULL;
    }
    
    return error;
}

int BIO_RunPEISQueued(int ID, uint8_t channel,
                     bool vs_initial,
                     double initial_voltage_step,
                     double duration_step,
                     double record_every_dT,
                     double record_every_dI,
                     double initial_freq,
                     double final_freq,
                     bool sweep_linear,
                     double amplitude_voltage,
                     int frequency_number,
                     int average_n_times,
                     bool correction,
                     double wait_for_steady,
					 bool processData,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     DevicePriority priority,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    BioPEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_PEIS,
            .channel = channel,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = progressCallback,
            .userData = userData
        },
        .vs_initial = vs_initial,
        .initial_voltage_step = initial_voltage_step,
        .duration_step = duration_step,
        .record_every_dT = record_every_dT,
        .record_every_dI = record_every_dI,
        .initial_freq = initial_freq,
        .final_freq = final_freq,
        .sweep_linear = sweep_linear,
        .amplitude_voltage = amplitude_voltage,
        .frequency_number = frequency_number,
        .average_n_times = average_n_times,
        .correction = correction,
        .wait_for_steady = wait_for_steady,
		.processData = processData
    };
    
    BioCommandResult cmdResult;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_PEIS,
                                       (BioCommandParams*)&cmd, priority, &cmdResult,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BIO_ERR_PARTIAL_DATA) && result) {
        *result = cmdResult.data.techniqueResult.techniqueData;
        // Transfer ownership - don't free in adapter
        cmdResult.data.techniqueResult.techniqueData = NULL;
    }
    
    return error;
}

int BIO_RunGEISQueued(int ID, uint8_t channel,
                     bool vs_initial,
                     double initial_current_step,
                     double duration_step,
                     double record_every_dT,
                     double record_every_dE,
                     double initial_freq,
                     double final_freq,
                     bool sweep_linear,
                     double amplitude_current,
                     int frequency_number,
                     int average_n_times,
                     bool correction,
                     double wait_for_steady,
                     int i_range,
					 bool processData,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     DevicePriority priority,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    BioGEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_GEIS,
            .channel = channel,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = progressCallback,
            .userData = userData
        },
        .vs_initial = vs_initial,
        .initial_current_step = initial_current_step,
        .duration_step = duration_step,
        .record_every_dT = record_every_dT,
        .record_every_dE = record_every_dE,
        .initial_freq = initial_freq,
        .final_freq = final_freq,
        .sweep_linear = sweep_linear,
        .amplitude_current = amplitude_current,
        .frequency_number = frequency_number,
        .average_n_times = average_n_times,
        .correction = correction,
        .wait_for_steady = wait_for_steady,
        .i_range = i_range,
		.processData = processData
    };
    
    BioCommandResult cmdResult;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_GEIS,
                                       (BioCommandParams*)&cmd, priority, &cmdResult,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BIO_ERR_PARTIAL_DATA) && result) {
        *result = cmdResult.data.techniqueResult.techniqueData;
        // Transfer ownership - don't free in adapter
        cmdResult.data.techniqueResult.techniqueData = NULL;
    }
    
    return error;
}

/******************************************************************************
 * High-Level Technique Functions (Async)
 ******************************************************************************/

BioCommandID BIO_RunOCVAsync(int ID, uint8_t channel,
                            double duration_s,
                            double sample_interval_s,
                            double record_every_dE,
                            double record_every_dT,
                            int e_range,
							bool processData,
                            DevicePriority priority,
                            BioCommandCallback callback,
                            void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return 0;
    
    BioOCVCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_OCV,
            .channel = channel,
            .timeout_ms = (int)((duration_s + 5) * 1000),
            .progressCallback = NULL,  // Progress callbacks not supported in async mode
            .userData = NULL
        },
        .duration_s = duration_s,
        .sample_interval_s = sample_interval_s,
        .record_every_dE = record_every_dE,
        .record_every_dT = record_every_dT,
        .e_range = e_range,
		.processData = processData
    };
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_OCV, (BioCommandParams*)&cmd,
                                priority, callback, userData);
}

BioCommandID BIO_RunPEISAsync(int ID, uint8_t channel,
                             bool vs_initial,
                             double initial_voltage_step,
                             double duration_step,
                             double record_every_dT,
                             double record_every_dI,
                             double initial_freq,
                             double final_freq,
                             bool sweep_linear,
                             double amplitude_voltage,
                             int frequency_number,
                             int average_n_times,
                             bool correction,
                             double wait_for_steady,
							 bool processData,
                             DevicePriority priority,
                             BioCommandCallback callback,
                             void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return 0;
    
    BioPEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_PEIS,
            .channel = channel,
            .timeout_ms = BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .vs_initial = vs_initial,
        .initial_voltage_step = initial_voltage_step,
        .duration_step = duration_step,
        .record_every_dT = record_every_dT,
        .record_every_dI = record_every_dI,
        .initial_freq = initial_freq,
        .final_freq = final_freq,
        .sweep_linear = sweep_linear,
        .amplitude_voltage = amplitude_voltage,
        .frequency_number = frequency_number,
        .average_n_times = average_n_times,
        .correction = correction,
        .wait_for_steady = wait_for_steady,
		.processData = processData
    };
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_PEIS, (BioCommandParams*)&cmd,
                                priority, callback, userData);
}

BioCommandID BIO_RunGEISAsync(int ID, uint8_t channel,
                             bool vs_initial,
                             double initial_current_step,
                             double duration_step,
                             double record_every_dT,
                             double record_every_dE,
                             double initial_freq,
                             double final_freq,
                             bool sweep_linear,
                             double amplitude_current,
                             int frequency_number,
                             int average_n_times,
                             bool correction,
                             double wait_for_steady,
                             int i_range,
							 bool processData,
                             DevicePriority priority,
                             BioCommandCallback callback,
                             void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return 0;
    
    BioGEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_GEIS,
            .channel = channel,
            .timeout_ms = BIO_DEFAULT_PEIS_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .vs_initial = vs_initial,
        .initial_current_step = initial_current_step,
        .duration_step = duration_step,
        .record_every_dT = record_every_dT,
        .record_every_dE = record_every_dE,
        .initial_freq = initial_freq,
        .final_freq = final_freq,
        .sweep_linear = sweep_linear,
        .amplitude_current = amplitude_current,
        .frequency_number = frequency_number,
        .average_n_times = average_n_times,
        .correction = correction,
        .wait_for_steady = wait_for_steady,
        .i_range = i_range,
		.processData = processData
    };
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_GEIS, (BioCommandParams*)&cmd,
                                priority, callback, userData);
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

int BIO_ConnectQueued(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioConnectCommand cmd = {
        .base = {
            .type = BIO_CMD_CONNECT,
            .channel = 0,
            .timeout_ms = timeout * 1000,
            .progressCallback = NULL,
            .userData = NULL
        },
        .timeout = timeout
    };
    strncpy(cmd.address, address, sizeof(cmd.address) - 1);
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_CONNECT,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        BioLogicDeviceContext *ctx = (BioLogicDeviceContext*)DeviceQueue_GetDeviceContext(g_bioQueueManager);
        if (ctx && pID) *pID = ctx->deviceID;
        if (pInfos) *pInfos = result.data.deviceInfo;
    }
    return error;
}

int BIO_DisconnectQueued(int ID, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioCommandParams params = {0};
    BioCommandResult result;
    
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_DISCONNECT,
                                  &params, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_TestConnectionQueued(int ID, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_TEST_CONNECTION,
            .channel = 0,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_TEST_CONNECTION,
                                  (BioCommandParams*)&cmd, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_GetHardConfQueued(int ID, uint8_t ch, THardwareConf_t* pHardConf, DevicePriority priority) {
    if (!g_bioQueueManager || !pHardConf) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_HARDWARE_CONFIG,
            .channel = ch,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_HARDWARE_CONFIG,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pHardConf = result.data.hardwareConfig;
    }
    return error;
}

int BIO_SetHardConfQueued(int ID, uint8_t ch, THardwareConf_t HardConf, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioHardwareConfigCommand cmd = {
        .base = {
            .type = BIO_CMD_SET_HARDWARE_CONFIG,
            .channel = ch,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .config = HardConf
    };
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_SET_HARDWARE_CONFIG,
                                  (BioCommandParams*)&cmd, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_StopChannelQueued(int ID, uint8_t channel, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_STOP_CHANNEL,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_STOP_CHANNEL,
                                  (BioCommandParams*)&cmd, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_StartChannelQueued(int ID, uint8_t channel, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_START_CHANNEL,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_START_CHANNEL,
                                  (BioCommandParams*)&cmd, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_GetCurrentValuesQueued(int ID, uint8_t channel, TCurrentValues_t* pValues, DevicePriority priority) {
    if (!g_bioQueueManager || !pValues) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_CURRENT_VALUES,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_CURRENT_VALUES,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pValues = result.data.currentValues;
    }
    return error;
}

int BIO_GetChannelInfosQueued(int ID, uint8_t channel, TChannelInfos_t* pInfos, DevicePriority priority) {
    if (!g_bioQueueManager || !pInfos) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_CHANNEL_INFOS,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_CHANNEL_INFOS,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pInfos = result.data.channelInfos;
    }
    return error;
}

bool BIO_IsChannelPluggedQueued(int ID, uint8_t channel, DevicePriority priority) {
    if (!g_bioQueueManager) return false;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_IS_CHANNEL_PLUGGED,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_IS_CHANNEL_PLUGGED,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    return (error == SUCCESS) ? result.data.isPlugged : false;
}

int BIO_GetChannelsPluggedQueued(int ID, uint8_t* pChPlugged, uint8_t Size, DevicePriority priority) {
    if (!g_bioQueueManager || !pChPlugged) return ERR_QUEUE_NOT_INIT;
    
    BioGetChannelsPluggedCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_CHANNELS_PLUGGED,
            .channel = 0,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .maxChannels = Size
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_CHANNELS_PLUGGED,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        memcpy(pChPlugged, result.data.channelsPlugged, Size * sizeof(uint8_t));
    }
    return error;
}

int BIO_GetDataQueued(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues, DevicePriority priority) {
    if (!g_bioQueueManager || !pBuf || !pInfos || !pValues) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_DATA,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_DATA,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pInfos = result.data.dataResult.dataInfo;
        *pValues = result.data.dataResult.currentValues;
        
        // Copy raw data to provided buffer
        int dataSize = pInfos->NbRows * pInfos->NbCols;
        memcpy(pBuf->data, result.data.dataResult.rawData, dataSize * sizeof(unsigned int));
        
        // Free the allocated raw data
        free(result.data.dataResult.rawData);
    }
    return error;
}

int BIO_GetExperimentInfosQueued(int ID, uint8_t channel, TExperimentInfos_t* pExpInfos, DevicePriority priority) {
    if (!g_bioQueueManager || !pExpInfos) return ERR_QUEUE_NOT_INIT;
    
    BioChannelCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_EXPERIMENT_INFOS,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        }
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_EXPERIMENT_INFOS,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pExpInfos = result.data.experimentInfos;
    }
    return error;
}

int BIO_SetExperimentInfosQueued(int ID, uint8_t channel, TExperimentInfos_t ExpInfos, DevicePriority priority) {
    if (!g_bioQueueManager) return ERR_QUEUE_NOT_INIT;
    
    BioSetExperimentInfosCommand cmd = {
        .base = {
            .type = BIO_CMD_SET_EXPERIMENT_INFOS,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .expInfo = ExpInfos
    };
    
    BioCommandResult result;
    return BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_SET_EXPERIMENT_INFOS,
                                  (BioCommandParams*)&cmd, priority, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BIO_LoadFirmwareQueued(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                          bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile, DevicePriority priority) {
    if (!g_bioQueueManager || !pChannels || !pResults) return ERR_QUEUE_NOT_INIT;
    
    BioLoadFirmwareCommand cmd = {
        .base = {
            .type = BIO_CMD_LOAD_FIRMWARE,
            .channel = 0,
            .timeout_ms = 30000,  // 30 seconds for firmware load
            .progressCallback = NULL,
            .userData = NULL
        },
        .numChannels = Length,
        .showGauge = ShowGauge,
        .forceReload = ForceReload
    };
    
    memcpy(cmd.channels, pChannels, Length * sizeof(uint8_t));
    
    if (BinFile) {
        strncpy(cmd.binFile, BinFile, sizeof(cmd.binFile) - 1);
    } else {
        cmd.binFile[0] = '\0';
    }
    
    if (XlxFile) {
        strncpy(cmd.xlxFile, XlxFile, sizeof(cmd.xlxFile) - 1);
    } else {
        cmd.xlxFile[0] = '\0';
    }
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_LOAD_FIRMWARE,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       cmd.base.timeout_ms);
    
    if (error == SUCCESS) {
        memcpy(pResults, result.data.firmwareResults, Length * sizeof(int));
    }
    return error;
}

int BIO_GetLibVersionQueued(char* pVersion, unsigned int* psize, DevicePriority priority) {
    if (!g_bioQueueManager || !pVersion || !psize) return ERR_QUEUE_NOT_INIT;
    
    BioCommandParams params = {.type = BIO_CMD_GET_LIB_VERSION};
    BioCommandResult result;
    
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_LIB_VERSION,
                                       &params, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        unsigned int len = strlen(result.data.version);
        if (len < *psize) {
            strcpy(pVersion, result.data.version);
            *psize = len;
        } else {
            strncpy(pVersion, result.data.version, *psize - 1);
            pVersion[*psize - 1] = '\0';
            *psize = *psize - 1;
        }
    }
    return error;
}

int BIO_GetMessageQueued(int ID, uint8_t channel, char* msg, unsigned int* size, DevicePriority priority) {
    if (!g_bioQueueManager || !msg || !size) return ERR_QUEUE_NOT_INIT;
    
    BioGetMessageCommand cmd = {
        .base = {
            .type = BIO_CMD_GET_MESSAGE,
            .channel = channel,
            .timeout_ms = BIO_QUEUE_COMMAND_TIMEOUT_MS,
            .progressCallback = NULL,
            .userData = NULL
        },
        .maxSize = *size
    };
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(g_bioQueueManager, BIO_CMD_GET_MESSAGE,
                                       (BioCommandParams*)&cmd, priority, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS && result.data.message) {
        unsigned int len = strlen(result.data.message);
        if (len < *size) {
            strcpy(msg, result.data.message);
            *size = len;
        } else {
            strncpy(msg, result.data.message, *size - 1);
            msg[*size - 1] = '\0';
            *size = *size - 1;
        }
        free(result.data.message);
    }
    return error;
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
		case BIO_CMD_LOAD_FIRMWARE:
            return BIO_DELAY_AFTER_CONNECT;
            
        case BIO_CMD_RUN_OCV:
		case BIO_CMD_RUN_PEIS:
		case BIO_CMD_RUN_GEIS:
		    return BIO_DELAY_AFTER_TECHNIQUE;
            
        case BIO_CMD_SET_HARDWARE_CONFIG:
        case BIO_CMD_GET_HARDWARE_CONFIG:
		case BIO_CMD_STOP_CHANNEL:
        case BIO_CMD_START_CHANNEL:
            return BIO_DELAY_AFTER_CONFIG;
		
		case BIO_CMD_GET_CURRENT_VALUES:
        case BIO_CMD_GET_CHANNEL_INFOS:
        case BIO_CMD_IS_CHANNEL_PLUGGED:
        case BIO_CMD_GET_CHANNELS_PLUGGED:
        case BIO_CMD_GET_DATA:
        case BIO_CMD_GET_EXPERIMENT_INFOS:
        case BIO_CMD_SET_EXPERIMENT_INFOS:
        case BIO_CMD_GET_LIB_VERSION:
        case BIO_CMD_GET_MESSAGE:
            return BIO_DELAY_RECOVERY;
            
        default:
            return BIO_DELAY_RECOVERY;
    }
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