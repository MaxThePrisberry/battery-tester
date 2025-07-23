/******************************************************************************
 * dtb4848_queue.c
 * 
 * Thread-safe command queue implementation for DTB 4848 Temperature Controller
 * Built on top of the generic device queue system
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
    "CONFIGURE",
    "CONFIGURE_DEFAULT",
    "FACTORY_RESET",
    "GET_STATUS",
    "GET_PROCESS_VALUE",
    "GET_SETPOINT",
    "GET_PID_PARAMS",
    "GET_ALARM_STATUS",
    "CLEAR_ALARM",
	"ENABLE_WRITE_ACCESS",
	"DISABLE_WRITE_ACCESS", 
	"GET_WRITE_ACCESS_STATUS",
    "RAW_MODBUS"
};

// Global queue manager pointer
static DTBQueueManager *g_dtbQueueManager = NULL;

/******************************************************************************
 * DTB Device Context Structure
 ******************************************************************************/

typedef struct {
    DTB_Handle handle;
    bool autoDiscovery;
    int specificPort;
    int specificBaudRate;
    int specificSlaveAddress;
} DTBDeviceContext;

/******************************************************************************
 * DTB Connection Parameters
 ******************************************************************************/

typedef struct {
    int comPort;
    int baudRate;
    int slaveAddress;
} DTBConnectionParams;

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
    .getErrorString = GetErrorString,
    
    // Raw command support
    .supportsRawCommands = NULL,
    .executeRawCommand = NULL
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int DTB_AdapterConnect(void *deviceContext, void *connectionParams) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    DTBConnectionParams *params = (DTBConnectionParams*)connectionParams;
    int result;
    
    // Use specific connection parameters
    LogMessageEx(LOG_DEVICE_DTB, "Connecting to DTB on COM%d...", params->comPort);
    result = DTB_Initialize(&ctx->handle, params->comPort, 
                          params->slaveAddress, params->baudRate);
    
    if (result == DTB_SUCCESS) {
        ctx->autoDiscovery = false;
        ctx->specificPort = params->comPort;
        ctx->specificBaudRate = params->baudRate;
        ctx->specificSlaveAddress = params->slaveAddress;
    }
    
    return result;
}

static int DTB_AdapterDisconnect(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    
    if (ctx->handle.isConnected) {
        // Stop output before disconnecting
        DTB_SetRunStop(&ctx->handle, 0);
        DTB_Close(&ctx->handle);
    }
    
    return DTB_SUCCESS;
}

static int DTB_AdapterTestConnection(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    return DTB_TestConnection(&ctx->handle);
}

static bool DTB_AdapterIsConnected(void *deviceContext) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    return ctx->handle.isConnected;
}

static int DTB_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    DTBDeviceContext *ctx = (DTBDeviceContext*)deviceContext;
    DTBCommandParams *cmdParams = (DTBCommandParams*)params;
    DTBCommandResult *cmdResult = (DTBCommandResult*)result;
    
    switch ((DTBCommandType)commandType) {
        case DTB_CMD_SET_RUN_STOP:
            cmdResult->errorCode = DTB_SetRunStop(&ctx->handle, cmdParams->runStop.run);
            break;
            
        case DTB_CMD_SET_SETPOINT:
            cmdResult->errorCode = DTB_SetSetPoint(&ctx->handle, cmdParams->setpoint.temperature);
            break;
            
        case DTB_CMD_START_AUTO_TUNING:
            cmdResult->errorCode = DTB_StartAutoTuning(&ctx->handle);
            break;
            
        case DTB_CMD_STOP_AUTO_TUNING:
            cmdResult->errorCode = DTB_StopAutoTuning(&ctx->handle);
            break;
            
        case DTB_CMD_SET_CONTROL_METHOD:
            cmdResult->errorCode = DTB_SetControlMethod(&ctx->handle, cmdParams->controlMethod.method);
            break;
            
        case DTB_CMD_SET_PID_MODE:
            cmdResult->errorCode = DTB_SetPIDMode(&ctx->handle, cmdParams->pidMode.mode);
            break;
            
        case DTB_CMD_SET_SENSOR_TYPE:
            cmdResult->errorCode = DTB_SetSensorType(&ctx->handle, cmdParams->sensorType.sensorType);
            break;
            
        case DTB_CMD_SET_TEMPERATURE_LIMITS:
            cmdResult->errorCode = DTB_SetTemperatureLimits(&ctx->handle,
                cmdParams->temperatureLimits.upperLimit,
                cmdParams->temperatureLimits.lowerLimit);
            break;
            
        case DTB_CMD_SET_ALARM_LIMITS:
            cmdResult->errorCode = DTB_SetAlarmLimits(&ctx->handle,
                cmdParams->alarmLimits.upperLimit,
                cmdParams->alarmLimits.lowerLimit);
            break;
            
        case DTB_CMD_CONFIGURE:
            cmdResult->errorCode = DTB_Configure(&ctx->handle, &cmdParams->configure.config);
            break;
            
        case DTB_CMD_CONFIGURE_DEFAULT:
            cmdResult->errorCode = DTB_ConfigureDefault(&ctx->handle);
            break;
            
        case DTB_CMD_FACTORY_RESET:
            cmdResult->errorCode = DTB_FactoryReset(&ctx->handle);
            break;
            
        case DTB_CMD_GET_STATUS:
            cmdResult->errorCode = DTB_GetStatus(&ctx->handle, &cmdResult->data.status);
            break;
            
        case DTB_CMD_GET_PROCESS_VALUE:
            cmdResult->errorCode = DTB_GetProcessValue(&ctx->handle, &cmdResult->data.temperature);
            break;
            
        case DTB_CMD_GET_SETPOINT:
            cmdResult->errorCode = DTB_GetSetPoint(&ctx->handle, &cmdResult->data.setpoint);
            break;
            
        case DTB_CMD_GET_PID_PARAMS:
            cmdResult->errorCode = DTB_GetPIDParams(&ctx->handle, 
                cmdParams->getPidParams.pidNumber,
                &cmdResult->data.pidParams);
            break;
            
        case DTB_CMD_GET_ALARM_STATUS:
            cmdResult->errorCode = DTB_GetAlarmStatus(&ctx->handle, &cmdResult->data.alarmActive);
            break;
            
        case DTB_CMD_CLEAR_ALARM:
            cmdResult->errorCode = DTB_ClearAlarm(&ctx->handle);
            break;
			
		case DTB_CMD_ENABLE_WRITE_ACCESS:
		    cmdResult->errorCode = DTB_EnableWriteAccess(&ctx->handle);
		    break;
		    
		case DTB_CMD_DISABLE_WRITE_ACCESS:
		    cmdResult->errorCode = DTB_DisableWriteAccess(&ctx->handle);
		    break;
		    
		case DTB_CMD_GET_WRITE_ACCESS_STATUS:
		    cmdResult->errorCode = DTB_GetWriteAccessStatus(&ctx->handle, 
		        &cmdResult->data.writeAccessEnabled);
		    break;
            
        case DTB_CMD_RAW_MODBUS:
            // TODO: Implement raw Modbus command execution
            cmdResult->errorCode = DTB_ERROR_NOT_SUPPORTED;
            break;
            
        default:
            cmdResult->errorCode = DTB_ERROR_INVALID_PARAM;
            break;
    }
    
    // Log errors appropriately
    if (cmdResult->errorCode != DTB_SUCCESS) {
        switch (cmdResult->errorCode) {
            case DTB_ERROR_BUSY:
                LogWarningEx(LOG_DEVICE_DTB, "Device busy: %s", 
                           DTB_GetErrorString(cmdResult->errorCode));
                break;
            case DTB_ERROR_TIMEOUT:
            case DTB_ERROR_COMM:
            case DTB_ERROR_NOT_CONNECTED:
                LogErrorEx(LOG_DEVICE_DTB, "Communication error: %s", 
                         DTB_GetErrorString(cmdResult->errorCode));
                break;
            default:
                LogErrorEx(LOG_DEVICE_DTB, "Command %s failed: %s",
                         DTB_QueueGetCommandTypeName(commandType),
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

DTBQueueManager* DTB_QueueInit(int comPort, int slaveAddress, int baudRate) {
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
    
    connParams->comPort = comPort;
    connParams->slaveAddress = slaveAddress;
    connParams->baudRate = baudRate;
    
    // Create the generic device queue
    DTBQueueManager *mgr = DeviceQueue_Create(&g_dtbAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_DTB);
    
    return mgr;
}

DTB_Handle* DTB_QueueGetHandle(DTBQueueManager *mgr) {
    DTBDeviceContext *context = (DTBDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;
    
    return &context->handle;
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

int DTB_QueueCommandBlocking(DTBQueueManager *mgr, DTBCommandType type,
                           DTBCommandParams *params, DTBPriority priority,
                           DTBCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

CommandID DTB_QueueCommandAsync(DTBQueueManager *mgr, DTBCommandType type,
                              DTBCommandParams *params, DTBPriority priority,
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
 * Wrapper Functions
 ******************************************************************************/

void DTB_SetGlobalQueueManager(DTBQueueManager *mgr) {
    g_dtbQueueManager = mgr;
}

DTBQueueManager* DTB_GetGlobalQueueManager(void) {
    return g_dtbQueueManager;
}

int DTB_SetRunStopQueued(DTB_Handle *handle, int run) {
    if (!g_dtbQueueManager) return DTB_SetRunStop(handle, run);
    
    DTBCommandParams params = {.runStop = {run}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_RUN_STOP,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetSetPointQueued(DTB_Handle *handle, double temperature) {
    if (!g_dtbQueueManager) return DTB_SetSetPoint(handle, temperature);
    
    DTBCommandParams params = {.setpoint = {temperature}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_SETPOINT,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_StartAutoTuningQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_StartAutoTuning(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_START_AUTO_TUNING,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_StopAutoTuningQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_StopAutoTuning(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_STOP_AUTO_TUNING,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetControlMethodQueued(DTB_Handle *handle, int method) {
    if (!g_dtbQueueManager) return DTB_SetControlMethod(handle, method);
    
    DTBCommandParams params = {.controlMethod = {method}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_CONTROL_METHOD,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetPIDModeQueued(DTB_Handle *handle, int mode) {
    if (!g_dtbQueueManager) return DTB_SetPIDMode(handle, mode);
    
    DTBCommandParams params = {.pidMode = {mode}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_PID_MODE,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetSensorTypeQueued(DTB_Handle *handle, int sensorType) {
    if (!g_dtbQueueManager) return DTB_SetSensorType(handle, sensorType);
    
    DTBCommandParams params = {.sensorType = {sensorType}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_SENSOR_TYPE,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetTemperatureLimitsQueued(DTB_Handle *handle, double upperLimit, double lowerLimit) {
    if (!g_dtbQueueManager) return DTB_SetTemperatureLimits(handle, upperLimit, lowerLimit);
    
    DTBCommandParams params = {.temperatureLimits = {upperLimit, lowerLimit}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_TEMPERATURE_LIMITS,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_SetAlarmLimitsQueued(DTB_Handle *handle, double upperLimit, double lowerLimit) {
    if (!g_dtbQueueManager) return DTB_SetAlarmLimits(handle, upperLimit, lowerLimit);
    
    DTBCommandParams params = {.alarmLimits = {upperLimit, lowerLimit}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_SET_ALARM_LIMITS,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_ConfigureQueued(DTB_Handle *handle, const DTB_Configuration *config) {
    if (!g_dtbQueueManager) return DTB_Configure(handle, config);
    
    DTBCommandParams params = {.configure = {*config}};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CONFIGURE,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_ConfigureDefaultQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_ConfigureDefault(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CONFIGURE_DEFAULT,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_FactoryResetQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_FactoryReset(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_FACTORY_RESET,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_GetStatusQueued(DTB_Handle *handle, DTB_Status *status) {
    if (!g_dtbQueueManager || !status) return DTB_GetStatus(handle, status);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_STATUS,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *status = result.data.status;
    }
    return error;
}

int DTB_GetProcessValueQueued(DTB_Handle *handle, double *temperature) {
    if (!g_dtbQueueManager || !temperature) return DTB_GetProcessValue(handle, temperature);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_PROCESS_VALUE,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *temperature = result.data.temperature;
    }
    return error;
}

int DTB_GetSetPointQueued(DTB_Handle *handle, double *setPoint) {
    if (!g_dtbQueueManager || !setPoint) return DTB_GetSetPoint(handle, setPoint);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_SETPOINT,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *setPoint = result.data.setpoint;
    }
    return error;
}

int DTB_GetPIDParamsQueued(DTB_Handle *handle, int pidNumber, DTB_PIDParams *pidParams) {
    if (!g_dtbQueueManager || !pidParams) return DTB_GetPIDParams(handle, pidNumber, pidParams);
    
    DTBCommandParams params = {.getPidParams = {pidNumber}};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_PID_PARAMS,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *pidParams = result.data.pidParams;
    }
    return error;
}

int DTB_GetAlarmStatusQueued(DTB_Handle *handle, int *alarmActive) {
    if (!g_dtbQueueManager || !alarmActive) return DTB_GetAlarmStatus(handle, alarmActive);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_ALARM_STATUS,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *alarmActive = result.data.alarmActive;
    }
    return error;
}

int DTB_ClearAlarmQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_ClearAlarm(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_CLEAR_ALARM,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_EnableWriteAccessQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_EnableWriteAccess(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_ENABLE_WRITE_ACCESS,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_DisableWriteAccessQueued(DTB_Handle *handle) {
    if (!g_dtbQueueManager) return DTB_DisableWriteAccess(handle);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    return DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_DISABLE_WRITE_ACCESS,
                                  &params, DTB_PRIORITY_HIGH, &result,
                                  DTB_QUEUE_COMMAND_TIMEOUT_MS);
}

int DTB_GetWriteAccessStatusQueued(DTB_Handle *handle, int *isEnabled) {
    if (!g_dtbQueueManager || !isEnabled) return DTB_GetWriteAccessStatus(handle, isEnabled);
    
    DTBCommandParams params = {0};
    DTBCommandResult result;
    
    int error = DTB_QueueCommandBlocking(g_dtbQueueManager, DTB_CMD_GET_WRITE_ACCESS_STATUS,
                                       &params, DTB_PRIORITY_NORMAL, &result,
                                       DTB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == DTB_SUCCESS) {
        *isEnabled = result.data.writeAccessEnabled;
    }
    return error;
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
        case DTB_CMD_CONFIGURE:
        case DTB_CMD_CONFIGURE_DEFAULT:
            return DTB_DELAY_CONFIG_CHANGE;
            
        case DTB_CMD_SET_TEMPERATURE_LIMITS:
        case DTB_CMD_SET_ALARM_LIMITS:
            return DTB_DELAY_AFTER_WRITE_REGISTER;
            
        case DTB_CMD_FACTORY_RESET:
            return 1000; // 1 second after factory reset
            
        case DTB_CMD_GET_STATUS:
        case DTB_CMD_GET_PROCESS_VALUE:
        case DTB_CMD_GET_SETPOINT:
        case DTB_CMD_GET_PID_PARAMS:
        case DTB_CMD_GET_ALARM_STATUS:
            return DTB_DELAY_AFTER_READ;
            
        case DTB_CMD_CLEAR_ALARM:
            return DTB_DELAY_AFTER_WRITE_BIT;
            
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

int DTB_ConfigureAtomic(DTB_Handle *handle, const DTB_Configuration *config,
                       DTBTransactionCallback callback, void *userData) {
    DTBQueueManager *queueMgr = DTB_GetGlobalQueueManager();
    if (!queueMgr || !config) {
        return DTB_ERROR_INVALID_PARAM;
    }
    
    // Create transaction
    TransactionHandle txn = DTB_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to begin configuration transaction");
        return ERR_OPERATION_FAILED;
    }
    
    DTBCommandParams params;
    int result = SUCCESS;
    
    // Add all configuration commands to transaction
    
    // 1. Set sensor type
    params.sensorType.sensorType = config->sensorType;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_SENSOR_TYPE, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 2. Set temperature limits
    params.temperatureLimits.upperLimit = config->upperTempLimit;
    params.temperatureLimits.lowerLimit = config->lowerTempLimit;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_TEMPERATURE_LIMITS, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 3. Set control method
    params.controlMethod.method = config->controlMethod;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_CONTROL_METHOD, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 4. Set PID mode (if using PID control)
    if (config->controlMethod == CONTROL_METHOD_PID) {
        params.pidMode.mode = config->pidMode;
        result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_PID_MODE, &params);
        if (result != SUCCESS) goto cleanup;
    }
    
    // 5. Configure alarm if enabled
    if (config->alarmType != ALARM_DISABLED) {
        params.alarmLimits.upperLimit = config->alarmUpperLimit;
        params.alarmLimits.lowerLimit = config->alarmLowerLimit;
        result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_ALARM_LIMITS, &params);
        if (result != SUCCESS) goto cleanup;
    }
    
    // Commit transaction
    result = DTB_QueueCommitTransaction(queueMgr, txn, callback, userData);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_DTB, "Configuration transaction committed");
        return SUCCESS;
    }
    
cleanup:
    DTB_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_DTB, "Failed to create configuration transaction");
    return result;
}

int DTB_SetControlMethodWithParams(DTB_Handle *handle, int method, int pidMode,
                                  const DTB_PIDParams *pidParams) {
    DTBQueueManager *queueMgr = DTB_GetGlobalQueueManager();
    if (!queueMgr) {
        return DTB_ERROR_INVALID_PARAM;
    }
    
    // For non-PID methods, just set the control method
    if (method != CONTROL_METHOD_PID) {
        return DTB_SetControlMethodQueued(handle, method);
    }
    
    // For PID control, use transaction to ensure consistency
    TransactionHandle txn = DTB_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to begin control method transaction");
        return ERR_OPERATION_FAILED;
    }
    
    DTBCommandParams params;
    int result = SUCCESS;
    
    // 1. Set control method to PID
    params.controlMethod.method = CONTROL_METHOD_PID;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_CONTROL_METHOD, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 2. Set PID mode
    params.pidMode.mode = pidMode;
    result = DTB_QueueAddToTransaction(queueMgr, txn, DTB_CMD_SET_PID_MODE, &params);
    if (result != SUCCESS) goto cleanup;
    
    // 3. TODO: Add commands to set PID parameters if provided
    // Note: DTB4848 may require additional register writes for PID parameters
    // which are not exposed in the current API
    
    // Commit transaction
    result = DTB_QueueCommitTransaction(queueMgr, txn, NULL, NULL);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_DTB, "Control method changed to PID mode %d", pidMode);
        return SUCCESS;
    }
    
cleanup:
    DTB_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_DTB, "Failed to change control method");
    return result;
}