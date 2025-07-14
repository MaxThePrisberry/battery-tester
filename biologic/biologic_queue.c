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
    "RUN_SPEIS",
    "RUN_GEIS",
    "RUN_SGEIS",
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
    BioCommandResult *cmdResult = (BioCommandResult*)result;
    
    // Check connection for most commands
    if (commandType != BIO_CMD_CONNECT && (!ctx->isConnected || ctx->deviceID < 0)) {
        cmdResult->errorCode = BL_ERR_NOINSTRUMENTCONNECTED;
        return cmdResult->errorCode;
    }
    
    switch ((BioCommandType)commandType) {
        case BIO_CMD_CONNECT: {
		    // Connection is handled by the adapter connect function
		    cmdResult->errorCode = SUCCESS;
		    break;
		}   
		
        case BIO_CMD_DISCONNECT: {
            cmdResult->errorCode = BL_Disconnect(ctx->deviceID);
            if (cmdResult->errorCode == SUCCESS) {
                ctx->isConnected = false;
                ctx->deviceID = -1;
            }
            break;
        }
            
        case BIO_CMD_TEST_CONNECTION: {
            cmdResult->errorCode = BL_TestConnection(ctx->deviceID);
            break;
        }
            
        case BIO_CMD_RUN_OCV: {
            BioOCVCommand *cmd = (BioOCVCommand*)params;
            BL_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start OCV
            cmdResult->errorCode = BL_StartOCV(
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
            BioPEISCommand *cmd = (BioPEISCommand*)params;
            BL_TechniqueContext *techContext = NULL;
            double startTime = Timer();
            
            // Start PEIS
            cmdResult->errorCode = BL_StartPEIS(
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
		
		case BIO_CMD_RUN_SPEIS: {
		    BioSPEISCommand *cmd = (BioSPEISCommand*)params;
		    BL_TechniqueContext *techContext = NULL;
		    double startTime = Timer();
		    
		    // Start SPEIS
		    cmdResult->errorCode = BL_StartSPEIS(
		        ctx->deviceID,
		        cmd->base.channel,
		        cmd->vs_initial,
		        cmd->vs_final,
		        cmd->initial_voltage_step,
		        cmd->final_voltage_step,
		        cmd->duration_step,
		        cmd->step_number,
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
		    while (!BL_IsTechniqueComplete(techContext)) {
		        BL_UpdateTechnique(techContext);
		        Delay(0.1);
		    }
		    
		    // Get results (same as OCV/PEIS)
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
		
		case BIO_CMD_RUN_GEIS: {
		    BioGEISCommand *cmd = (BioGEISCommand*)params;
		    BL_TechniqueContext *techContext = NULL;
		    double startTime = Timer();
		    
		    // Start GEIS
		    cmdResult->errorCode = BL_StartGEIS(
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
		    while (!BL_IsTechniqueComplete(techContext)) {
		        BL_UpdateTechnique(techContext);
		        Delay(0.1);
		    }
		    
		    // Get results (same as OCV/PEIS/SPEIS)
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

		case BIO_CMD_RUN_SGEIS: {
		    BioSGEISCommand *cmd = (BioSGEISCommand*)params;
		    BL_TechniqueContext *techContext = NULL;
		    double startTime = Timer();
		    
		    // Start SGEIS
		    cmdResult->errorCode = BL_StartSGEIS(
		        ctx->deviceID,
		        cmd->base.channel,
		        cmd->vs_initial,
		        cmd->vs_final,
		        cmd->initial_current_step,
		        cmd->final_current_step,
		        cmd->duration_step,
		        cmd->step_number,
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
		    while (!BL_IsTechniqueComplete(techContext)) {
		        BL_UpdateTechnique(techContext);
		        Delay(0.1);
		    }
		    
		    // Get results (same as other techniques)
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
        
        case BIO_CMD_GET_HARDWARE_CONFIG: {
            BioChannelCommand *cmd = (BioChannelCommand*)params;
            cmdResult->errorCode = BL_GetHardConf(ctx->deviceID,
                                                cmd->base.channel,
                                                &cmdResult->data.hardwareConfig);
            break;
        }
            
        case BIO_CMD_SET_HARDWARE_CONFIG: {
            BioHardwareConfigCommand *cmd = (BioHardwareConfigCommand*)params;
            cmdResult->errorCode = BL_SetHardConf(ctx->deviceID,
                                                cmd->base.channel,
                                                cmd->config);
            break;
        }
            
        default:
            cmdResult->errorCode = ERR_INVALID_PARAMETER;
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
		
		case BIO_CMD_RUN_SPEIS: {
		    BioSPEISCommand *cmd = malloc(sizeof(BioSPEISCommand));
		    if (cmd) *cmd = *(BioSPEISCommand*)sourceParams;
		    return cmd;
		}
		
		case BIO_CMD_RUN_GEIS: {
		    BioGEISCommand *cmd = malloc(sizeof(BioGEISCommand));
		    if (cmd) *cmd = *(BioGEISCommand*)sourceParams;
		    return cmd;
		}

		case BIO_CMD_RUN_SGEIS: {
		    BioSGEISCommand *cmd = malloc(sizeof(BioSGEISCommand));
		    if (cmd) *cmd = *(BioSGEISCommand*)sourceParams;
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
	     commandType == BIO_CMD_RUN_SPEIS || commandType == BIO_CMD_RUN_GEIS || 
	     commandType == BIO_CMD_RUN_SGEIS) &&
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
	if ((commandType == BIO_CMD_RUN_OCV || commandType == BIO_CMD_RUN_PEIS || 
	     commandType == BIO_CMD_RUN_SPEIS || commandType == BIO_CMD_RUN_GEIS || 
	     commandType == BIO_CMD_RUN_SGEIS) &&
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
					bool processData,
                    BL_RawDataBuffer **data,
                    int timeout_ms,
                    BioTechniqueProgressCallback progressCallback,
                    void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartOCV(ID, channel, duration_s, sample_interval_s,
                               record_every_dE, record_every_dT, e_range, processData, &context);
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
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_OCV,
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

int BL_RunPEISQueued(int ID, uint8_t channel,
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
                     BL_RawDataBuffer **data,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartPEIS(ID, channel, vs_initial, initial_voltage_step,
                                duration_step, record_every_dT, record_every_dI,
                                initial_freq, final_freq, sweep_linear,
                                amplitude_voltage, frequency_number, average_n_times,
                                correction, wait_for_steady, processData, &context);
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
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_PEIS,
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

int BL_RunSPEISQueued(int ID, uint8_t channel,
                      bool vs_initial,
                      bool vs_final,
                      double initial_voltage_step,
                      double final_voltage_step,
                      double duration_step,
                      int step_number,
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
                      BL_RawDataBuffer **data,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartSPEIS(ID, channel, vs_initial, vs_final,
                                  initial_voltage_step, final_voltage_step,
                                  duration_step, step_number,
                                  record_every_dT, record_every_dI,
                                  initial_freq, final_freq, sweep_linear,
                                  amplitude_voltage, frequency_number, average_n_times,
                                  correction, wait_for_steady, processData, &context);
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
    
    BioSPEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_SPEIS,
            .channel = channel,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : 
                         (int)((duration_step * step_number + 60) * 1000),
            .progressCallback = progressCallback,
            .userData = userData
        },
        .vs_initial = vs_initial,
        .vs_final = vs_final,
        .initial_voltage_step = initial_voltage_step,
        .final_voltage_step = final_voltage_step,
        .duration_step = duration_step,
        .step_number = step_number,
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
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_SPEIS,
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

int BL_RunGEISQueued(int ID, uint8_t channel,
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
                     BL_RawDataBuffer **data,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartGEIS(ID, channel, vs_initial, initial_current_step,
                                duration_step, record_every_dT, record_every_dE,
                                initial_freq, final_freq, sweep_linear,
                                amplitude_current, frequency_number, average_n_times,
                                correction, wait_for_steady, i_range, processData, &context);
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
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_GEIS,
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
                                       cmd.base.timeout_ms);
    
    if ((error == SUCCESS || error == BL_ERR_PARTIAL_DATA) && data) {
        *data = result.data.techniqueResult.rawData;
    }
    
    return error;
}

int BL_RunSGEISQueued(int ID, uint8_t channel,
                      bool vs_initial,
                      bool vs_final,
                      double initial_current_step,
                      double final_current_step,
                      double duration_step,
                      int step_number,
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
                      BL_RawDataBuffer **data,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) {
        // Direct call without queue
        BL_TechniqueContext *context;
        int result = BL_StartSGEIS(ID, channel, vs_initial, vs_final,
                                  initial_current_step, final_current_step,
                                  duration_step, step_number,
                                  record_every_dT, record_every_dE,
                                  initial_freq, final_freq, sweep_linear,
                                  amplitude_current, frequency_number, average_n_times,
                                  correction, wait_for_steady, i_range, processData, &context);
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
    
    BioSGEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_SGEIS,
            .channel = channel,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : 
                         (int)((duration_step * step_number + 60) * 1000),
            .progressCallback = progressCallback,
            .userData = userData
        },
        .vs_initial = vs_initial,
        .vs_final = vs_final,
        .initial_current_step = initial_current_step,
        .final_current_step = final_current_step,
        .duration_step = duration_step,
        .step_number = step_number,
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
    
    BioCommandResult result;
    int error = BIO_QueueCommandBlocking(mgr, BIO_CMD_RUN_SGEIS,
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
                                       cmd.base.timeout_ms);
    
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
							bool processData,
                            BioCommandCallback callback,
                            void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
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
                                BIO_PRIORITY_HIGH, callback, userData);
}

BioCommandID BL_RunPEISAsync(int ID, uint8_t channel,
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
                             BioCommandCallback callback,
                             void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
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
                                BIO_PRIORITY_HIGH, callback, userData);
}



BioCommandID BL_RunSPEISAsync(int ID, uint8_t channel,
                              bool vs_initial,
                              bool vs_final,
                              double initial_voltage_step,
                              double final_voltage_step,
                              double duration_step,
                              int step_number,
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
                              BioCommandCallback callback,
                              void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
    BioSPEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_SPEIS,
            .channel = channel,
            .timeout_ms = (int)((duration_step * step_number + 60) * 1000),
            .progressCallback = NULL,
            .userData = NULL
        },
        .vs_initial = vs_initial,
        .vs_final = vs_final,
        .initial_voltage_step = initial_voltage_step,
        .final_voltage_step = final_voltage_step,
        .duration_step = duration_step,
        .step_number = step_number,
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
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_SPEIS, (BioCommandParams*)&cmd,
                                BIO_PRIORITY_HIGH, callback, userData);
}

BioCommandID BL_RunGEISAsync(int ID, uint8_t channel,
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
                             BioCommandCallback callback,
                             void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
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
                                BIO_PRIORITY_HIGH, callback, userData);
}

BioCommandID BL_RunSGEISAsync(int ID, uint8_t channel,
                              bool vs_initial,
                              bool vs_final,
                              double initial_current_step,
                              double final_current_step,
                              double duration_step,
                              int step_number,
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
                              BioCommandCallback callback,
                              void *userData) {
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (!mgr) return -1;
    
    BioSGEISCommand cmd = {
        .base = {
            .type = BIO_CMD_RUN_SGEIS,
            .channel = channel,
            .timeout_ms = (int)((duration_step * step_number + 60) * 1000),
            .progressCallback = NULL,
            .userData = NULL
        },
        .vs_initial = vs_initial,
        .vs_final = vs_final,
        .initial_current_step = initial_current_step,
        .final_current_step = final_current_step,
        .duration_step = duration_step,
        .step_number = step_number,
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
    
    return BIO_QueueCommandAsync(mgr, BIO_CMD_RUN_SGEIS, (BioCommandParams*)&cmd,
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
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
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
                                  (BioCommandParams*)&cmd, BIO_PRIORITY_NORMAL, &result,
                                  BIO_QUEUE_COMMAND_TIMEOUT_MS);
}

int BL_GetHardConfQueued(int ID, uint8_t ch, THardwareConf_t* pHardConf) {
    if (!g_bioQueueManager || !pHardConf) return BL_GetHardConf(ID, ch, pHardConf);
    
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
                                       (BioCommandParams*)&cmd, BIO_PRIORITY_NORMAL, &result,
                                       BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == SUCCESS) {
        *pHardConf = result.data.hardwareConfig;
    }
    return error;
}

int BL_SetHardConfQueued(int ID, uint8_t ch, THardwareConf_t HardConf) {
    if (!g_bioQueueManager) return BL_SetHardConf(ID, ch, HardConf);
    
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
                                  (BioCommandParams*)&cmd, BIO_PRIORITY_HIGH, &result,
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
		case BIO_CMD_RUN_SPEIS:
		case BIO_CMD_RUN_GEIS:
		case BIO_CMD_RUN_SGEIS:
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