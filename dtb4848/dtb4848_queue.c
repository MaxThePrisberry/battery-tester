/******************************************************************************
 * dtb4848_queue.c
 * 
 * Thread-safe command queue implementation for DTB 4848 Temperature Controller
 * Built on top of the generic device queue system
 * 
 * Supports multiple DTB devices on the same COM port with different slave addresses
 ******************************************************************************/

#include "dtb4848_queue.h"
#include "logging.h"
#include <ansi_c.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* g_commandTypeNames[] = {
    "NONE",
    "SET_RUN_STOP",
    "SET_SETPOINT",
    "START_AUTO_TUNING",
    "STOP_AUTO_TUNING",
    "SET_CONTROL_METHOD",
    "SET_PID_MODE",
    "SET_SENSOR_TYPE",
    "SET_TEMPERATURE_LIMITS",
    "SET_ALARM_LIMITS",
    "SET_HEATING_COOLING",
    "CONFIGURE",
    "CONFIGURE_DEFAULT",
    "FACTORY_RESET",
    "GET_STATUS",
    "GET_PROCESS_VALUE",
    "GET_SETPOINT",
    "GET_PID_PARAMS",
    "GET_ALARM_STATUS",
    "CLEAR_ALARM",
    "SET_FRONT_PANEL_LOCK",
    "GET_FRONT_PANEL_LOCK",
    "ENABLE_WRITE_ACCESS",
    "DISABLE_WRITE_ACCESS", 
    "GET_WRITE_ACCESS_STATUS",
    "RAW_MODBUS"
};

// Global queue manager pointer
static DTBQueueManager *g_dtbQueueManager = NULL;

// Queue a command (blocking)
static int DTB_QueueCommandBlocking(DTBQueueManager *mgr, DTBCommandType type,
                           DTBCommandParams *params, DevicePriority priority,
                           DTBCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
static CommandID DTB_QueueCommandAsync(DTBQueueManager *mgr, DTBCommandType type,
                              DTBCommandParams *params, DevicePriority priority,
                              DTBCommandCallback callback, void *userData);

/******************************************************************************
 * DTB Multi-Device Context Structure
 ******************************************************************************/

typedef struct {
    DTB_Handle handles[MAX_DTB_DEVICES];
    int slaveAddresses[MAX_DTB_DEVICES];
    int numDevices;
    int comPort;
    int baudRate;
} DTBDeviceContext;

/******************************************************************************
 * DTB Connection Parameters
 ******************************************************************************/

typedef struct {
    int comPort;
    int baudRate;
    int *slaveAddresses;
    int numSlaves;
} DTBConnectionParams;

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static int FindDeviceIndex(DTBDeviceContext *ctx, int slaveAddress) {
    if (!ctx) return -1;
    
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->slaveAddresses[i] == slaveAddress) {
            return i;
        }
    }
    return -1;
}

static DTB_Handle* GetDeviceHandle(DTBDeviceContext *ctx, int slaveAddress) {
    int index = FindDeviceIndex(ctx, slaveAddress);
    if (index < 0) return NULL;
    
    return &ctx->handles[index];
}

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int DTB_AdapterConnect(void *deviceContext, void *connectionParams);
static int DTB_AdapterDisconnect(void *deviceContext);
static int DTB_AdapterTestConnection(void *deviceContext);
static bool DTB_AdapterIsConnected(void *deviceContext);
static int DTB_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* DTB_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void DTB_AdapterFreeCommandParams(int commandType, void *params);
static void* DTB_AdapterCreateCommandResult(int commandType);
static void DTB_AdapterFreeCommandResult(int commandType, void *result);
static void DTB_AdapterCopyCommandResult(int commandType, void *dest, void *src);

// DTB device adapter
static const DeviceAdapter g_dtbAdapter = {
    .deviceName = "DTB 4848",
    
    // Connection management
    .connect = DTB_AdapterConnect,
    .disconnect = DTB_AdapterDisconnect,
    .testConnection = DTB_AdapterTestConnection,
    .isConnected = DTB_AdapterIsConnected,
    
    // Command execution
    .executeCommand = DTB_AdapterExecuteCommand,
    
    // Command management
    .createCommandParams = DTB_AdapterCreateCommandParams,
    .freeCommandParams = DTB_AdapterFreeCommandParams,
    .createCommandResult = DTB_AdapterCreateCommandResult,
    .freeCommandResult = DTB_AdapterFreeCommandResult,
    .copyCommandResult = DTB_AdapterCopyCommandResult,
    
    // Utility functions
    .getCommandTypeName = (const char* (*)(int))DTB_QueueGetCommandTypeName,
    .getCommandDelay = DTB_QueueGetCommandDelay,
    .getErrorString = GetErrorString
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int DTB_AdapterConnect(void *deviceContext, void *connectionParams) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    DTBConnectionParams *params = (DTBConnectionParams*)connectionParams;
    
    if (!ctx || !params || !params->slaveAddresses) {
        LogErrorEx(LOG_DEVICE_DTB, "Invalid parameters for DTB adapter connect");
        return DTB_ERROR_INVALID_PARAM;
    }
    
    if (params->numSlaves <= 0 || params->numSlaves > MAX_DTB_DEVICES) {
        LogErrorEx(LOG_DEVICE_DTB, "Invalid number of slaves: %d (max %d)", 
                   params->numSlaves, MAX_DTB_DEVICES);
        return DTB_ERROR_INVALID_PARAM;
    }
    
    // Initialize context
    ctx->comPort = params->comPort;
    ctx->baudRate = params->baudRate;
    ctx->numDevices = params->numSlaves;
    
    // Copy slave addresses
    for (int i = 0; i < params->numSlaves; i++) {
        ctx->slaveAddresses[i] = params->slaveAddresses[i];
    }
    
    // Initialize each device
    int successCount = 0;
    for (int i = 0; i < ctx->numDevices; i++) {
        LogMessageEx(LOG_DEVICE_DTB, "Connecting to DTB slave %d on COM%d...", 
                     ctx->slaveAddresses[i], ctx->comPort);
        
        int result = DTB_Initialize(&ctx->handles[i], ctx->comPort, 
                                   ctx->slaveAddresses[i], ctx->baudRate);
        
        if (result == DTB_SUCCESS) {
            LogMessageEx(LOG_DEVICE_DTB, "Successfully connected to DTB slave %d", 
                         ctx->slaveAddresses[i]);
            successCount++;
        } else {
            LogErrorEx(LOG_DEVICE_DTB, "Failed to connect to DTB slave %d: %s", 
                       ctx->slaveAddresses[i], DTB_GetErrorString(result));
            // Continue trying other devices
        }
    }
    
    if (successCount == 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to connect to any DTB devices");
        return DTB_ERROR_COMM;
    }
    
    LogMessageEx(LOG_DEVICE_DTB, "Connected to %d of %d DTB devices", 
                 successCount, ctx->numDevices);
    
    return DTB_SUCCESS;
}

static int DTB_AdapterDisconnect(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    
    if (!ctx) return DTB_SUCCESS;
    
    // Disconnect all devices
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->handles[i].isConnected) {
            LogMessageEx(LOG_DEVICE_DTB, "Disconnecting DTB slave %d...", 
                         ctx->slaveAddresses[i]);
            
            // Stop output before disconnecting
            DTB_SetRunStop(&ctx->handles[i], 0);
            DTB_Close(&ctx->handles[i]);
        }
    }
    
    return DTB_SUCCESS;
}

static int DTB_AdapterTestConnection(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    
    if (!ctx) return DTB_ERROR_NOT_CONNECTED;
    
    // Test connection to all devices
    int connectedCount = 0;
    for (int i = 0; i < ctx->numDevices; i++) {
        if (DTB_TestConnection(&ctx->handles[i]) == DTB_SUCCESS) {
            connectedCount++;
        }
    }
    
    return (connectedCount > 0) ? DTB_SUCCESS : DTB_ERROR_NOT_CONNECTED;
}

static bool DTB_AdapterIsConnected(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    
    if (!ctx) return false;
    
    // Return true if any device is connected
    for (int i = 0; i < ctx->numDevices; i++) {
        if (ctx->handles[i].isConnected) {
            return true;
        }
    }
    
    return false;
}

static int DTB_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    DTBCommandParams *cmdParams = (DTBCommandParams*)params;
    DTBCommandResult *cmdResult = (DTBCommandResult*)result;
    
    if (!ctx || !cmdParams || !cmdResult) {
        return DTB_ERROR_INVALID_PARAM;
    }
	
	// Find the target device handle
    DTB_Handle *handle = GetDeviceHandle(ctx, cmdParams->runStop.slaveAddress);
    if (!handle) {
        LogWarningEx(LOG_DEVICE_DTB, "Unrecognized slave address: %d", cmdParams->runStop.slaveAddress);
		
		DTB_Handle rawHandle = {
	        .comPort = DTB_COM_PORT,                    // From constants
	        .slaveAddress = cmdParams->rawModbus.slaveAddress,  // Target address
	        .baudRate = DTB_BAUD_RATE,                  // From constants
	        .timeoutMs = DEFAULT_TIMEOUT_MS,            // Default timeout
	        .isConnected = 1,                          // Mark as connected
	        .state = DEVICE_STATE_CONNECTED            // Connected state
	    };
	    
	    strcpy(rawHandle.modelNumber, "Raw Modbus");
		
		handle = &rawHandle;
    }
    
    switch ((DTBCommandType)commandType) {
        case DTB_CMD_SET_RUN_STOP:
            cmdResult->errorCode = DTB_SetRunStop(handle, cmdParams->runStop.run);
            break;
            
        case DTB_CMD_SET_SETPOINT:
            cmdResult->errorCode = DTB_SetSetPoint(handle, cmdParams->setpoint.temperature);
            break;
            
        case DTB_CMD_START_AUTO_TUNING:
            cmdResult->errorCode = DTB_StartAutoTuning(handle);
            break;
            
        case DTB_CMD_STOP_AUTO_TUNING:
            cmdResult->errorCode = DTB_StopAutoTuning(handle);
            break;
            
        case DTB_CMD_SET_CONTROL_METHOD:
            cmdResult->errorCode = DTB_SetControlMethod(handle, cmdParams->controlMethod.method);
            break;
            
        case DTB_CMD_SET_PID_MODE:
            cmdResult->errorCode = DTB_SetPIDMode(handle, cmdParams->pidMode.mode);
            break;
            
        case DTB_CMD_SET_SENSOR_TYPE:
            cmdResult->errorCode = DTB_SetSensorType(handle, cmdParams->sensorType.sensorType);
            break;
            
        case DTB_CMD_SET_TEMPERATURE_LIMITS:
            cmdResult->errorCode = DTB_SetTemperatureLimits(handle,
                cmdParams->temperatureLimits.upperLimit,
                cmdParams->temperatureLimits.lowerLimit);
            break;
            
        case DTB_CMD_SET_ALARM_LIMITS:
            cmdResult->errorCode = DTB_SetAlarmLimits(handle,
                cmdParams->alarmLimits.upperLimit,
                cmdParams->alarmLimits.lowerLimit);
            break;
        
        case DTB_CMD_SET_HEATING_COOLING:
            cmdResult->errorCode = DTB_SetHeatingCooling(handle, cmdParams->heatingCooling.mode);
            break;
            
        case DTB_CMD_CONFIGURE:
            cmdResult->errorCode = DTB_Configure(handle, &cmdParams->configure.config);
            break;
            
        case DTB_CMD_CONFIGURE_DEFAULT:
            cmdResult->errorCode = DTB_ConfigureDefault(handle);
            break;
            
        case DTB_CMD_FACTORY_RESET:
            cmdResult->errorCode = DTB_FactoryReset(handle);
            break;
            
        case DTB_CMD_GET_STATUS:
            cmdResult->errorCode = DTB_GetStatus(handle, &cmdResult->data.status);
            break;
            
        case DTB_CMD_GET_PROCESS_VALUE:
            cmdResult->errorCode = DTB_GetProcessValue(handle, &cmdResult->data.temperature);
            break;
            
        case DTB_CMD_GET_SETPOINT:
            cmdResult->errorCode = DTB_GetSetPoint(handle, &cmdResult->data.setpoint);
            break;
            
        case DTB_CMD_GET_PID_PARAMS:
            cmdResult->errorCode = DTB_GetPIDParams(handle, 
                cmdParams->getPidParams.pidNumber,
                &cmdResult->data.pidParams);
            break;
            
        case DTB_CMD_GET_ALARM_STATUS:
            cmdResult->errorCode = DTB_GetAlarmStatus(handle, &cmdResult->data.alarmActive);
            break;
            
        case DTB_CMD_CLEAR_ALARM:
            cmdResult->errorCode = DTB_ClearAlarm(handle);
            break;
            
        case DTB_CMD_SET_FRONT_PANEL_LOCK:
            cmdResult->errorCode = DTB_SetFrontPanelLock(handle, 
                cmdParams->frontPanelLock.lockMode);
            break;
            
        case DTB_CMD_GET_FRONT_PANEL_LOCK:
            cmdResult->errorCode = DTB_GetFrontPanelLock(handle, 
                &cmdResult->data.frontPanelLockMode);
            break;
            
        case DTB_CMD_ENABLE_WRITE_ACCESS:
            cmdResult->errorCode = DTB_EnableWriteAccess(handle);
            break;
            
        case DTB_CMD_DISABLE_WRITE_ACCESS:
            cmdResult->errorCode = DTB_DisableWriteAccess(handle);
            break;
            
        case DTB_CMD_GET_WRITE_ACCESS_STATUS:
            cmdResult->errorCode = DTB_GetWriteAccessStatus(handle, 
                &cmdResult->data.writeAccessEnabled);
            break;
            
        case DTB_CMD_RAW_MODBUS:
            // Validate parameters
            if (!cmdParams->rawModbus.rxBuffer && cmdParams->rawModbus.rxBufferSize > 0) {
                cmdResult->errorCode = DTB_ERROR_INVALID_PARAM;
                break;
            }
            
            // Route based on Modbus function code
            switch (cmdParams->rawModbus.functionCode) {
                case MODBUS_READ_REGISTERS:  // 0x03
                    {
                        unsigned short value;
                        cmdResult->errorCode = DTB_ReadRegister(handle, 
                                                               cmdParams->rawModbus.address, 
                                                               &value);
                        
                        if (cmdResult->errorCode == DTB_SUCCESS) {
                            // Store the value in the result
                            cmdResult->data.rawResponse.rxLength = 2;  // 2 bytes for register value
                            cmdResult->data.rawResponse.rxData = malloc(2);
                            if (cmdResult->data.rawResponse.rxData) {
                                // Store as big-endian (Modbus standard)
                                cmdResult->data.rawResponse.rxData[0] = (value >> 8) & 0xFF;
                                cmdResult->data.rawResponse.rxData[1] = value & 0xFF;
                            } else {
                                cmdResult->errorCode = DTB_ERROR_RESPONSE;
                            }
                        }
                    }
                    break;
                    
                case MODBUS_WRITE_REGISTER:  // 0x06
                    {
                        cmdResult->errorCode = DTB_WriteRegister(handle,
                                                               cmdParams->rawModbus.address,
                                                               cmdParams->rawModbus.data);
                        
                        if (cmdResult->errorCode == DTB_SUCCESS) {
                            // For write register, response echoes the address and data
                            cmdResult->data.rawResponse.rxLength = 4;  // 2 bytes address + 2 bytes data
                            cmdResult->data.rawResponse.rxData = malloc(4);
                            if (cmdResult->data.rawResponse.rxData) {
                                // Address (big-endian)
                                cmdResult->data.rawResponse.rxData[0] = (cmdParams->rawModbus.address >> 8) & 0xFF;
                                cmdResult->data.rawResponse.rxData[1] = cmdParams->rawModbus.address & 0xFF;
                                // Data (big-endian)
                                cmdResult->data.rawResponse.rxData[2] = (cmdParams->rawModbus.data >> 8) & 0xFF;
                                cmdResult->data.rawResponse.rxData[3] = cmdParams->rawModbus.data & 0xFF;
                            } else {
                                cmdResult->errorCode = DTB_ERROR_RESPONSE;
                            }
                        }
                    }
                    break;
                    
                case MODBUS_READ_BITS:  // 0x02
                    {
                        int bitValue;
                        cmdResult->errorCode = DTB_ReadBit(handle,
                                                         cmdParams->rawModbus.address,
                                                         &bitValue);
                        
                        if (cmdResult->errorCode == DTB_SUCCESS) {
                            // For read bits, response is 1 byte containing the bit value
                            cmdResult->data.rawResponse.rxLength = 1;
                            cmdResult->data.rawResponse.rxData = malloc(1);
                            if (cmdResult->data.rawResponse.rxData) {
                                cmdResult->data.rawResponse.rxData[0] = bitValue ? 0x01 : 0x00;
                            } else {
                                cmdResult->errorCode = DTB_ERROR_RESPONSE;
                            }
                        }
                    }
                    break;
                    
                case MODBUS_WRITE_BIT:  // 0x05
                    {
                        // For write bit, data field should be 0xFF00 for ON, 0x0000 for OFF
                        int bitValue = (cmdParams->rawModbus.data == 0xFF00) ? 1 : 0;
                        
                        cmdResult->errorCode = DTB_WriteBit(handle,
                                                          cmdParams->rawModbus.address,
                                                          bitValue);
                        
                        if (cmdResult->errorCode == DTB_SUCCESS) {
                            // For write bit, response echoes the address and data
                            cmdResult->data.rawResponse.rxLength = 4;  // 2 bytes address + 2 bytes data
                            cmdResult->data.rawResponse.rxData = malloc(4);
                            if (cmdResult->data.rawResponse.rxData) {
                                // Address (big-endian)
                                cmdResult->data.rawResponse.rxData[0] = (cmdParams->rawModbus.address >> 8) & 0xFF;
                                cmdResult->data.rawResponse.rxData[1] = cmdParams->rawModbus.address & 0xFF;
                                // Data (big-endian) - echo what was sent
                                cmdResult->data.rawResponse.rxData[2] = (cmdParams->rawModbus.data >> 8) & 0xFF;
                                cmdResult->data.rawResponse.rxData[3] = cmdParams->rawModbus.data & 0xFF;
                            } else {
                                cmdResult->errorCode = DTB_ERROR_RESPONSE;
                            }
                        }
                    }
                    break;
                    
                default:
                    LogErrorEx(LOG_DEVICE_DTB, "Unsupported Modbus function code: 0x%02X", 
                             cmdParams->rawModbus.functionCode);
                    cmdResult->errorCode = DTB_ERROR_NOT_SUPPORTED;
                    break;
            }
            
            // If requested, copy data to the provided buffer
            if (cmdResult->errorCode == DTB_SUCCESS && 
                cmdParams->rawModbus.rxBuffer && 
                cmdParams->rawModbus.rxBufferSize > 0 &&
                cmdResult->data.rawResponse.rxData) {
                
                int copyLen = cmdResult->data.rawResponse.rxLength;
                if (copyLen > cmdParams->rawModbus.rxBufferSize) {
                    copyLen = cmdParams->rawModbus.rxBufferSize;
                }
                
                memcpy(cmdParams->rawModbus.rxBuffer, 
                       cmdResult->data.rawResponse.rxData, 
                       copyLen);
            }
            break;
            
        default:
            cmdResult->errorCode = DTB_ERROR_INVALID_PARAM;
            break;
    }
    
    // Log errors appropriately
    if (cmdResult->errorCode != DTB_SUCCESS) {
        switch (cmdResult->errorCode) {
            case DTB_ERROR_BUSY:
                LogWarningEx(LOG_DEVICE_DTB, "Device slave %d busy: %s", 
                           cmdParams->runStop.slaveAddress,
                           DTB_GetErrorString(cmdResult->errorCode));
                break;
            case DTB_ERROR_TIMEOUT:
            case DTB_ERROR_COMM:
            case DTB_ERROR_NOT_CONNECTED:
                LogErrorEx(LOG_DEVICE_DTB, "Communication error with slave %d: %s", 
                         cmdParams->runStop.slaveAddress,
                         DTB_GetErrorString(cmdResult->errorCode));
                break;
            default:
                LogErrorEx(LOG_DEVICE_DTB, "Command %s failed for slave %d: %s",
                         DTB_QueueGetCommandTypeName(commandType),
                         cmdParams->runStop.slaveAddress,
                         DTB_GetErrorString(cmdResult->errorCode));
                break;
        }
    }
    
    return cmdResult->errorCode;
}

static void* DTB_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    DTBCommandParams *params = malloc(sizeof(DTBCommandParams));
    if (!params) return NULL;
    
    *params = *(DTBCommandParams*)sourceParams;
    
    // Handle special cases (like raw Modbus buffers)
    if (commandType == DTB_CMD_RAW_MODBUS && sourceParams) {
        DTBCommandParams *src = (DTBCommandParams*)sourceParams;
        if (src->rawModbus.rxBuffer && src->rawModbus.rxBufferSize > 0) {
            params->rawModbus.rxBuffer = malloc(src->rawModbus.rxBufferSize);
            // Initialize the buffer to avoid garbage data
            if (params->rawModbus.rxBuffer) {
                memset(params->rawModbus.rxBuffer, 0, src->rawModbus.rxBufferSize);
            }
        }
    }
    
    return params;
}

static void DTB_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    
    DTBCommandParams *cmdParams = (DTBCommandParams*)params;
    
    // Free raw Modbus buffers
    if (commandType == DTB_CMD_RAW_MODBUS) {
        if (cmdParams->rawModbus.rxBuffer) free(cmdParams->rawModbus.rxBuffer);
    }
    
    free(params);
}

static void* DTB_AdapterCreateCommandResult(int commandType) {
    DTBCommandResult *result = calloc(1, sizeof(DTBCommandResult));
    
    // Initialize any command-specific result fields if needed
    
    return result;
}

static void DTB_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    
    DTBCommandResult *cmdResult = (DTBCommandResult*)result;
    
    // Free any allocated result data
    if (commandType == DTB_CMD_RAW_MODBUS && cmdResult->data.rawResponse.rxData) {
        free(cmdResult->data.rawResponse.rxData);
    }
    
    free(result);
}

static void DTB_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    DTBCommandResult *destResult = (DTBCommandResult*)dest;
    DTBCommandResult *srcResult = (DTBCommandResult*)src;
    
    *destResult = *srcResult;
    
    // Handle deep copies for pointer fields
    if (commandType == DTB_CMD_RAW_MODBUS && srcResult->data.rawResponse.rxData) {
        destResult->data.rawResponse.rxData = malloc(srcResult->data.rawResponse.rxLength);
        if (destResult->data.rawResponse.rxData) {
            memcpy(destResult->data.rawResponse.rxData, 
                   srcResult->data.rawResponse.rxData, 
                   srcResult->data.rawResponse.rxLength);
        }
    }
}

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

DTBQueueManager* DTB_QueueInit(int comPort, int baudRate, int *slaveAddresses, int numSlaves) {
    if (!slaveAddresses || numSlaves <= 0 || numSlaves > MAX_DTB_DEVICES) {
        LogErrorEx(LOG_DEVICE_DTB, "DTB_QueueInit: Invalid parameters (numSlaves=%d, max=%d)", 
                   numSlaves, MAX_DTB_DEVICES);
        return NULL;
    }
    
    // Create device context
    DTBDeviceContext *context = calloc(1, sizeof(DTBDeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_DTB, "DTB_QueueInit: Failed to allocate device context");
        return NULL;
    }
    
    // Create connection parameters
    DTBConnectionParams *connParams = calloc(1, sizeof(DTBConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_DTB, "DTB_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    // Allocate and copy slave addresses
    connParams->slaveAddresses = malloc(sizeof(int) * numSlaves);
    if (!connParams->slaveAddresses) {
        free(context);
        free(connParams);
        LogErrorEx(LOG_DEVICE_DTB, "DTB_QueueInit: Failed to allocate slave address array");
        return NULL;
    }
    
    connParams->comPort = comPort;
    connParams->baudRate = baudRate;
    connParams->numSlaves = numSlaves;
    
    for (int i = 0; i < numSlaves; i++) {
        connParams->slaveAddresses[i] = slaveAddresses[i];
        LogMessageEx(LOG_DEVICE_DTB, "DTB_QueueInit: Will initialize slave address %d", 
                     slaveAddresses[i]);
    }
    
    // Create the generic device queue
    DTBQueueManager *mgr = DeviceQueue_Create(&g_dtbAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams->slaveAddresses);
        free(connParams);
        LogErrorEx(LOG_DEVICE_DTB, "DTB_QueueInit: Failed to create device queue");
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_DTB);
    
    LogMessageEx(LOG_DEVICE_DTB, "DTB_QueueInit: Successfully created queue manager for %d devices", 
                 numSlaves);
    
    return mgr;
}

DTB_Handle* DTB_QueueGetHandle(DTBQueueManager *mgr, int slaveAddress) {
    if (!mgr) return NULL;
    
    DTBDeviceContext *context = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;
    
    return GetDeviceHandle(context, slaveAddress);
}

void DTB_QueueShutdown(DTBQueueManager *mgr) {
    if (!mgr) return;
    
    // Get and free the device context
    DTBDeviceContext *context = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    
    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);
    
    // Free our contexts
    if (context) free(context);
    // Note: Connection params are freed by the generic queue
}

bool DTB_QueueIsRunning(DTBQueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void DTB_QueueGetStats(DTBQueueManager *mgr, DTBQueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

static int DTB_QueueCommandBlocking(DTBQueueManager *mgr, DTBCommandType type,
                           DTBCommandParams *params, DevicePriority priority,
                           DTBCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

static CommandID DTB_QueueCommandAsync(DTBQueueManager *mgr, DTBCommandType type,
                              DTBCommandParams *params, DevicePriority priority,
                              DTBCommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
}

bool DTB_QueueHasCommandType(DTBQueueManager *mgr, DTBCommandType type) {
    return DeviceQueue_HasCommandType(mgr, type);
}

int DTB_QueueCancelAll(DTBQueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle DTB_QueueBeginTransaction(DTBQueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int DTB_QueueAddToTransaction(DTBQueueManager *mgr, TransactionHandle txn,
                            DTBCommandType type, DTBCommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int DTB_QueueCommitTransaction(DTBQueueManager *mgr, TransactionHandle txn,
                             DTBTransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

/******************************************************************************
 * Global Queue Manager Functions
 ******************************************************************************/

void DTB_SetGlobalQueueManager(DTBQueueManager *mgr) {
    g_dtbQueueManager = mgr;
}

DTBQueueManager* DTB_GetGlobalQueueManager(void) {
    return g_dtbQueueManager;
}

/******************************************************************************
 * Individual Device Wrapper Functions
 ******************************************************************************/

int DTB_SetRunStopQueued(int slaveAddress, int run, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.runStop = {slaveAddress, run}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_RUN_STOP,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetSetPointQueued(int slaveAddress, double temperature, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.setpoint = {slaveAddress, temperature}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_SETPOINT,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_StartAutoTuningQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.runStop = {slaveAddress}};  // Reusing struct with just slaveAddress
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_START_AUTO_TUNING,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_StopAutoTuningQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.runStop = {slaveAddress}};  // Reusing struct with just slaveAddress
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_STOP_AUTO_TUNING,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetControlMethodQueued(int slaveAddress, int method, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.controlMethod = {slaveAddress, method}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_CONTROL_METHOD,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetPIDModeQueued(int slaveAddress, int mode, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.pidMode = {slaveAddress, mode}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_PID_MODE,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetSensorTypeQueued(int slaveAddress, int sensorType, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.sensorType = {slaveAddress, sensorType}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_SENSOR_TYPE,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetTemperatureLimitsQueued(int slaveAddress, double upperLimit, double lowerLimit, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.temperatureLimits = {slaveAddress, upperLimit, lowerLimit}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_TEMPERATURE_LIMITS,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetAlarmLimitsQueued(int slaveAddress, double upperLimit, double lowerLimit, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.alarmLimits = {slaveAddress, upperLimit, lowerLimit}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_ALARM_LIMITS,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetHeatingCoolingQueued(int slaveAddress, int mode, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.heatingCooling = {slaveAddress, mode}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_HEATING_COOLING,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_ConfigureQueued(int slaveAddress, const DTB_Configuration *config, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!config) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.configure = {slaveAddress, *config}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CONFIGURE,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_ConfigureDefaultQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.configureDefault = {slaveAddress}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CONFIGURE_DEFAULT,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_FactoryResetQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.factoryReset = {slaveAddress}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_FACTORY_RESET,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_GetStatusQueued(int slaveAddress, DTB_Status *status, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!status) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getStatus = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_STATUS,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *status = result.data.status;
    }
    return error;
}

int DTB_GetProcessValueQueued(int slaveAddress, double *temperature, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!temperature) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getProcessValue = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_PROCESS_VALUE,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *temperature = result.data.temperature;
    }
    return error;
}

int DTB_GetSetPointQueued(int slaveAddress, double *setPoint, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!setPoint) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getSetpoint = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_SETPOINT,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *setPoint = result.data.setpoint;
    }
    return error;
}

int DTB_GetPIDParamsQueued(int slaveAddress, int pidNumber, DTB_PIDParams *pidParams, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!pidParams) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getPidParams = {slaveAddress, pidNumber}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_PID_PARAMS,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *pidParams = result.data.pidParams;
    }
    return error;
}

int DTB_GetAlarmStatusQueued(int slaveAddress, int *alarmActive, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!alarmActive) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getAlarmStatus = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_ALARM_STATUS,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *alarmActive = result.data.alarmActive;
    }
    return error;
}

int DTB_ClearAlarmQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.clearAlarm = {slaveAddress}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CLEAR_ALARM,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetFrontPanelLockQueued(int slaveAddress, int lockMode, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.frontPanelLock = {slaveAddress, lockMode}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_FRONT_PANEL_LOCK,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_GetFrontPanelLockQueued(int slaveAddress, int *lockMode, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!lockMode) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getFrontPanelLock = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_FRONT_PANEL_LOCK,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *lockMode = result.data.frontPanelLockMode;
    }
    return error;
}

int DTB_UnlockFrontPanelQueued(int slaveAddress, DevicePriority priority) {
    return DTB_SetFrontPanelLockQueued(slaveAddress, FRONT_PANEL_UNLOCKED, priority);
}

int DTB_LockFrontPanelQueued(int slaveAddress, int allowSetpointChange, DevicePriority priority) {
    int lockMode = allowSetpointChange ? FRONT_PANEL_LOCK_EXCEPT_SV : FRONT_PANEL_LOCK_ALL;
    return DTB_SetFrontPanelLockQueued(slaveAddress, lockMode, priority);
}

int DTB_EnableWriteAccessQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.enableWriteAccess = {slaveAddress}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_ENABLE_WRITE_ACCESS,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_DisableWriteAccessQueued(int slaveAddress, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {.disableWriteAccess = {slaveAddress}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_DISABLE_WRITE_ACCESS,
                                  &params, priority, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_GetWriteAccessStatusQueued(int slaveAddress, int *isEnabled, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!isEnabled) return ERR_NULL_POINTER;
    
    DTBCommandParams params = {.getWriteAccessStatus = {slaveAddress}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_WRITE_ACCESS_STATUS,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *isEnabled = result.data.writeAccessEnabled;
    }
    return error;
}

int DTB_SendRawModbusQueued(int slaveAddress, unsigned char functionCode,
                           unsigned short address, unsigned short data,
                           unsigned char *rxBuffer, int rxBufferSize, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBCommandParams params = {
        .rawModbus = {
            .slaveAddress = slaveAddress,
            .functionCode = functionCode,
            .address = address,
            .data = data,
            .rxBuffer = rxBuffer,
            .rxBufferSize = rxBufferSize
        }
    };
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_RAW_MODBUS,
                                       &params, priority, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Response data is already copied to rxBuffer in DTB_AdapterExecuteCommand
    
    return error;
}

/******************************************************************************
 * "All Devices" Convenience Functions
 ******************************************************************************/

int DTB_SetRunStopAllQueued(int run, DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBDeviceContext *ctx = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(g_dtbQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = DTB_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting run/stop to %s for all %d DTB devices...", 
                 run ? "RUN" : "STOP", ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = DTB_SetRunStopQueued(ctx->slaveAddresses[i], run, priority);
        if (result != DTB_SUCCESS) {
            LogErrorEx(LOG_DEVICE_DTB, "Failed to set run/stop for slave %d: %s", 
                       ctx->slaveAddresses[i], DTB_GetErrorString(result));
            if (allSuccess == DTB_SUCCESS) {
                allSuccess = result;  // Store first failure
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_DTB, "Successfully set run/stop for all DTB devices");
    } else {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to set run/stop for %d of %d DTB devices", 
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int DTB_ConfigureAllDefaultQueued(DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBDeviceContext *ctx = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(g_dtbQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = DTB_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_DTB, "Configuring all %d DTB devices...", ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = DTB_ConfigureDefaultQueued(ctx->slaveAddresses[i], priority);
        if (result != DTB_SUCCESS) {
            LogErrorEx(LOG_DEVICE_DTB, "Failed to configure slave %d: %s", 
                       ctx->slaveAddresses[i], DTB_GetErrorString(result));
            if (allSuccess == DTB_SUCCESS) {
                allSuccess = result;  // Store first failure
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_DTB, "Successfully configured all DTB devices");
    } else {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to configure %d of %d DTB devices", 
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

int DTB_EnableWriteAccessAllQueued(DevicePriority priority) {
    if (!g_dtbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    DTBDeviceContext *ctx = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(g_dtbQueueManager);
    if (!ctx) return ERR_QUEUE_NOT_INIT;
    
    int allSuccess = DTB_SUCCESS;
    int failureCount = 0;
    
    LogMessageEx(LOG_DEVICE_DTB, "Enabling write access for all %d DTB devices...", ctx->numDevices);
    
    for (int i = 0; i < ctx->numDevices; i++) {
        int result = DTB_EnableWriteAccessQueued(ctx->slaveAddresses[i], priority);
        if (result != DTB_SUCCESS) {
            LogErrorEx(LOG_DEVICE_DTB, "Failed to enable write access for slave %d: %s", 
                       ctx->slaveAddresses[i], DTB_GetErrorString(result));
            if (allSuccess == DTB_SUCCESS) {
                allSuccess = result;  // Store first failure
            }
            failureCount++;
        }
    }
    
    if (failureCount == 0) {
        LogMessageEx(LOG_DEVICE_DTB, "Successfully enabled write access for all DTB devices");
    } else {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to enable write access for %d of %d DTB devices", 
                   failureCount, ctx->numDevices);
    }
    
    return allSuccess;
}

/******************************************************************************
 * Async Command Function Implementations
 ******************************************************************************/

CommandID DTB_GetStatusAsync(int slaveAddress, DTBCommandCallback callback, void *userData, DevicePriority priority) {
    
    DTBQueueManager *mgr = DTB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    DTBCommandParams params = {.getStatus = {slaveAddress}};
    
    return DTB_QueueCommandAsync(mgr, DTB_CMD_GET_STATUS, &params,
                                priority, callback, userData);
}

CommandID DTB_SetRunStopAsync(int slaveAddress, int run, DTBCommandCallback callback, void *userData, DevicePriority priority) {
    
    DTBQueueManager *mgr = DTB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    DTBCommandParams params = {.runStop = {slaveAddress, run}};
    
    return DTB_QueueCommandAsync(mgr, DTB_CMD_SET_RUN_STOP, &params,
                                priority, callback, userData);
}

CommandID DTB_SetSetPointAsync(int slaveAddress, double temperature, DTBCommandCallback callback, void *userData, DevicePriority priority) {
    
    DTBQueueManager *mgr = DTB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    DTBCommandParams params = {.setpoint = {slaveAddress, temperature}};
    
    return DTB_QueueCommandAsync(mgr, DTB_CMD_SET_SETPOINT, &params,
                                priority, callback, userData);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* DTB_QueueGetCommandTypeName(DTBCommandType type) {
    if (type >= 0 && type < DTB_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int DTB_QueueGetCommandDelay(DTBCommandType type) {
    switch (type) {
        case DTB_CMD_SET_RUN_STOP:
            return DTB_DELAY_STATE_CHANGE;
            
        case DTB_CMD_SET_SETPOINT:
            return DTB_DELAY_SETPOINT_CHANGE;
            
        case DTB_CMD_START_AUTO_TUNING:
        case DTB_CMD_STOP_AUTO_TUNING:
            return DTB_DELAY_STATE_CHANGE;
            
        case DTB_CMD_SET_CONTROL_METHOD:
        case DTB_CMD_SET_PID_MODE:
        case DTB_CMD_SET_SENSOR_TYPE:
        case DTB_CMD_SET_HEATING_COOLING:
        case DTB_CMD_CONFIGURE:
        case DTB_CMD_CONFIGURE_DEFAULT:
            return DTB_DELAY_CONFIG_CHANGE;
            
        case DTB_CMD_SET_TEMPERATURE_LIMITS:
        case DTB_CMD_SET_ALARM_LIMITS:
        case DTB_CMD_SET_FRONT_PANEL_LOCK:
            return DTB_DELAY_AFTER_WRITE_REGISTER;
            
        case DTB_CMD_FACTORY_RESET:
            return 1000; // 1 second after factory reset
            
        case DTB_CMD_GET_STATUS:
        case DTB_CMD_GET_PROCESS_VALUE:
        case DTB_CMD_GET_SETPOINT:
        case DTB_CMD_GET_PID_PARAMS:
        case DTB_CMD_GET_ALARM_STATUS:
        case DTB_CMD_GET_FRONT_PANEL_LOCK:
            return DTB_DELAY_AFTER_READ;
            
        case DTB_CMD_CLEAR_ALARM:
            return DTB_DELAY_AFTER_WRITE_BIT;
        
        case DTB_CMD_RAW_MODBUS:
            return DTB_DELAY_RECOVERY;

        default:
            return DTB_DELAY_RECOVERY;
    }
}

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
 ******************************************************************************/

int DTB_QueueCancelCommand(DTBQueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int DTB_QueueCancelByType(DTBQueueManager *mgr, DTBCommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int DTB_QueueCancelByAge(DTBQueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int DTB_QueueCancelTransaction(DTBQueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}

/******************************************************************************
 * Advanced Transaction-Based Functions
 ******************************************************************************/

int DTB_ConfigureAtomic(int slaveAddress, const DTB_Configuration *config,
                       DTBTransactionCallback callback, void *userData, DevicePriority priority) {
    
    DTBQueueManager *queueMgr = DTB_GetGlobalQueueManager();
    if (!queueMgr) {
        LogErrorEx(LOG_DEVICE_DTB, "Queue manager not initialized for atomic configuration");
        return ERR_QUEUE_NOT_INIT;
    }
    if (!config) {
        return ERR_NULL_POINTER;
    }
    
    // Create transaction
    TransactionHandle txn = DTB_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to begin configuration transaction for slave %d", 
                   slaveAddress);
        return ERR_QUEUE_NOT_INIT;
    }
    
    // Set transaction priority
    int result = DeviceQueue_SetTransactionPriority(queueMgr, txn, priority);
    if (result != SUCCESS) {
        DTB_QueueCancelTransaction(queueMgr, txn);
        return result;
    }
    
    DTBCommandParams params;
    
    // Add all configuration commands to transaction
    
    // 1. Set sensor type
    params.sensorType.slaveAddress = slaveAddress;
    params.sensorType.sensorType = config->sensorType;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_SENSOR_TYPE, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 2. Set heating/cooling mode
    params.heatingCooling.slaveAddress = slaveAddress;
    params.heatingCooling.mode = config->heatingCoolingMode;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_HEATING_COOLING, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 3. Set temperature limits
    params.temperatureLimits.slaveAddress = slaveAddress;
    params.temperatureLimits.upperLimit = config->upperTempLimit;
    params.temperatureLimits.lowerLimit = config->lowerTempLimit;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_TEMPERATURE_LIMITS, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 4. Set control method
    params.controlMethod.slaveAddress = slaveAddress;
    params.controlMethod.method = config->controlMethod;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_CONTROL_METHOD, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 5. Set PID mode (if using PID control)
    if (config->controlMethod == CONTROL_METHOD_PID) {
        params.pidMode.slaveAddress = slaveAddress;
        params.pidMode.mode = config->pidMode;
        result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_PID_MODE, &params);
        if (result != SUCCESS) goto cleanup;
    }
    
    // 6. Configure alarm if enabled
    if (config->alarmType != ALARM_DISABLED) {
        params.alarmLimits.slaveAddress = slaveAddress;
        params.alarmLimits.upperLimit = config->alarmUpperLimit;
        params.alarmLimits.lowerLimit = config->alarmLowerLimit;
        result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_ALARM_LIMITS, &params);
        if (result != SUCCESS) goto cleanup;
        
        // Note: Setting alarm type requires a direct register write
        // This would need to be added as a separate command type if needed
        // For now, alarm type setting is not included in the atomic transaction
    }
    
    // Commit transaction
    result = DTB_QueueCommitTransaction(queueMgr, txn, callback, userData);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_DTB, "Configuration transaction committed for slave %d", 
                     slaveAddress);
        return SUCCESS;
    }
    
cleanup:
    DTB_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_DTB, "Failed to create configuration transaction for slave %d", 
               slaveAddress);
    return result;
}

int DTB_SetControlMethodWithParams(int slaveAddress, int method, int pidMode,
                                  const DTB_PIDParams *pidParams, DevicePriority priority) {
    
    DTBQueueManager *queueMgr = DTB_GetGlobalQueueManager();
    if (!queueMgr) {
        LogErrorEx(LOG_DEVICE_DTB, "Queue manager not initialized for control method change");
        return ERR_QUEUE_NOT_INIT;
    }
    
    // For non-PID methods, just set the control method
    if (method != CONTROL_METHOD_PID) {
        return DTB_SetControlMethodQueued(slaveAddress, method, priority);
    }
    
    // For PID control, use transaction to ensure consistency
    TransactionHandle txn = DTB_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to begin control method transaction for slave %d", 
                   slaveAddress);
        return ERR_QUEUE_NOT_INIT;
    }
    
    // Set transaction priority
    int result = DeviceQueue_SetTransactionPriority(queueMgr, txn, priority);
    if (result != SUCCESS) {
        DTB_QueueCancelTransaction(queueMgr, txn);
        return result;
    }
    
    DTBCommandParams params;
    
    // 1. Set control method to PID
    params.controlMethod.slaveAddress = slaveAddress;
    params.controlMethod.method = CONTROL_METHOD_PID;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_CONTROL_METHOD, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 2. Set PID mode
    params.pidMode.slaveAddress = slaveAddress;
    params.pidMode.mode = pidMode;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_PID_MODE, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 3. TODO: Add commands to set PID parameters if provided
    // Note: DTB4848 may require additional register writes for PID parameters
    // which are not exposed in the current API
    
    // Commit transaction
    result = DTB_QueueCommitTransaction(queueMgr, txn, NULL, NULL);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_DTB, "Control method changed to PID mode %d for slave %d", 
                     pidMode, slaveAddress);
        return SUCCESS;
    }
    
cleanup:
    DTB_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_DTB, "Failed to change control method for slave %d", slaveAddress);
    return result;
}