/******************************************************************************
 * psb10000_queue.c
 * 
 * Thread-safe command queue implementation for PSB 10000 Series Power Supply
 * Built on top of the generic device queue system
 ******************************************************************************/

#include "psb10000_queue.h"
#include "logging.h"
#include <ansi_c.h>

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
    "RAW_MODBUS",
	"SET_SINK_CURRENT",
	"SET_SINK_POWER",
	"SET_SINK_CURRENT_LIMITS",
	"SET_SINK_POWER_LIMIT",
};

// Global queue manager pointer
static PSBQueueManager *g_psbQueueManager = NULL;

// Queue a command (blocking)
static int PSB_QueueCommandBlocking(PSBQueueManager *mgr, PSBCommandType type,
                           PSBCommandParams *params, PSBPriority priority,
                           PSBCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
static CommandID PSB_QueueCommandAsync(PSBQueueManager *mgr, PSBCommandType type,
                              PSBCommandParams *params, PSBPriority priority,
                              PSBCommandCallback callback, void *userData);

/******************************************************************************
 * PSB Device Context Structure
 ******************************************************************************/

typedef struct {
    PSB_Handle handle;
    char targetSerial[64];
    int specificPort;
    int specificBaudRate;
    int specificSlaveAddress;
} PSBDeviceContext;

/******************************************************************************
 * PSB Connection Parameters
 ******************************************************************************/

typedef struct {
    char targetSerial[64];
    int comPort;
    int baudRate;
    int slaveAddress;
} PSBConnectionParams;

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int PSB_AdapterConnect(void *deviceContext, void *connectionParams);
static int PSB_AdapterDisconnect(void *deviceContext);
static int PSB_AdapterTestConnection(void *deviceContext);
static bool PSB_AdapterIsConnected(void *deviceContext);
static int PSB_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* PSB_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void PSB_AdapterFreeCommandParams(int commandType, void *params);
static void* PSB_AdapterCreateCommandResult(int commandType);
static void PSB_AdapterFreeCommandResult(int commandType, void *result);
static void PSB_AdapterCopyCommandResult(int commandType, void *dest, void *src);

// PSB device adapter
static const DeviceAdapter g_psbAdapter = {
    .deviceName = "PSB 10000",
    
    // Connection management
    .connect = PSB_AdapterConnect,
    .disconnect = PSB_AdapterDisconnect,
    .testConnection = PSB_AdapterTestConnection,
    .isConnected = PSB_AdapterIsConnected,
    
    // Command execution
    .executeCommand = PSB_AdapterExecuteCommand,
    
    // Command management
    .createCommandParams = PSB_AdapterCreateCommandParams,
    .freeCommandParams = PSB_AdapterFreeCommandParams,
    .createCommandResult = PSB_AdapterCreateCommandResult,
    .freeCommandResult = PSB_AdapterFreeCommandResult,
    .copyCommandResult = PSB_AdapterCopyCommandResult,
    
    // Utility functions
    .getCommandTypeName = (const char* (*)(int))PSB_QueueGetCommandTypeName,
    .getCommandDelay = PSB_QueueGetCommandDelay,
    .getErrorString = GetErrorString
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int PSB_AdapterConnect(void *deviceContext, void *connectionParams) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    PSBConnectionParams *params = (PSBConnectionParams*)connectionParams;
    int result;
    
    // Use specific connection parameters
    LogMessageEx(LOG_DEVICE_PSB, "Connecting to PSB on COM%d...", params->comPort);
    result = PSB_InitializeSpecific(&ctx->handle, params->comPort, 
                                  params->slaveAddress, params->baudRate);
    
    if (result == PSB_SUCCESS) {
        ctx->specificPort = params->comPort;
        ctx->specificBaudRate = params->baudRate;
        ctx->specificSlaveAddress = params->slaveAddress;
        
        // Only set remote mode and disable output - minimal safe state
        PSB_SetRemoteMode(&ctx->handle, 1);
        PSB_SetOutputEnable(&ctx->handle, 0);
    }
    
    return result;
}

static int PSB_AdapterDisconnect(void *deviceContext) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    
    if (ctx->handle.isConnected) {
        // Disable output and remote mode before disconnecting
        PSB_SetOutputEnable(&ctx->handle, 0);
        PSB_SetRemoteMode(&ctx->handle, 0);
        PSB_Close(&ctx->handle);
    }
    
    return PSB_SUCCESS;
}

static int PSB_AdapterTestConnection(void *deviceContext) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    PSB_Status status;
    return PSB_GetStatus(&ctx->handle, &status);
}

static bool PSB_AdapterIsConnected(void *deviceContext) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    return ctx->handle.isConnected;
}

static int PSB_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    PSBCommandParams *cmdParams = (PSBCommandParams*)params;
    PSBCommandResult *cmdResult = (PSBCommandResult*)result;
    
    switch ((PSBCommandType)commandType) {
        case PSB_CMD_SET_REMOTE_MODE:
            cmdResult->errorCode = PSB_SetRemoteMode(&ctx->handle, cmdParams->remoteMode.enable);
            break;
            
        case PSB_CMD_SET_OUTPUT_ENABLE:
            cmdResult->errorCode = PSB_SetOutputEnable(&ctx->handle, cmdParams->outputEnable.enable);
            break;
            
        case PSB_CMD_SET_VOLTAGE:
            cmdResult->errorCode = PSB_SetVoltage(&ctx->handle, cmdParams->setVoltage.voltage);
            break;
            
        case PSB_CMD_SET_CURRENT:
            cmdResult->errorCode = PSB_SetCurrent(&ctx->handle, cmdParams->setCurrent.current);
            break;
            
        case PSB_CMD_SET_POWER:
            cmdResult->errorCode = PSB_SetPower(&ctx->handle, cmdParams->setPower.power);
            break;
            
        case PSB_CMD_SET_VOLTAGE_LIMITS:
            cmdResult->errorCode = PSB_SetVoltageLimits(&ctx->handle, 
                cmdParams->voltageLimits.minVoltage,
                cmdParams->voltageLimits.maxVoltage);
            break;
            
        case PSB_CMD_SET_CURRENT_LIMITS:
            cmdResult->errorCode = PSB_SetCurrentLimits(&ctx->handle,
                cmdParams->currentLimits.minCurrent,
                cmdParams->currentLimits.maxCurrent);
            break;
            
        case PSB_CMD_SET_POWER_LIMIT:
            cmdResult->errorCode = PSB_SetPowerLimit(&ctx->handle, cmdParams->powerLimit.maxPower);
            break;
            
        case PSB_CMD_GET_STATUS:
            cmdResult->errorCode = PSB_GetStatus(&ctx->handle, &cmdResult->data.status);
            break;
            
        case PSB_CMD_GET_ACTUAL_VALUES:
            cmdResult->errorCode = PSB_GetActualValues(&ctx->handle,
                &cmdResult->data.actualValues.voltage,
                &cmdResult->data.actualValues.current,
                &cmdResult->data.actualValues.power);
            break;
            
        case PSB_CMD_RAW_MODBUS:
		    if (!cmdParams->rawModbus.txBuffer || !cmdParams->rawModbus.rxBuffer) {
		        cmdResult->errorCode = PSB_ERROR_INVALID_PARAM;
		        break;
		    }
		    
		    // Allocate space for response data
		    cmdResult->data.rawResponse.rxData = malloc(cmdParams->rawModbus.rxBufferSize);
		    if (!cmdResult->data.rawResponse.rxData) {
		        cmdResult->errorCode = PSB_ERROR_INVALID_PARAM;
		        break;
		    }
		    
		    // Execute the raw command
		    cmdResult->errorCode = PSB_SendRawModbus(&ctx->handle, 
		                                           cmdParams->rawModbus.txBuffer,
		                                           cmdParams->rawModbus.txLength,
		                                           cmdResult->data.rawResponse.rxData,
		                                           cmdParams->rawModbus.rxBufferSize,
		                                           cmdParams->rawModbus.expectedRxLength);
		    
		    if (cmdResult->errorCode == PSB_SUCCESS) {
		        cmdResult->data.rawResponse.rxLength = cmdParams->rawModbus.expectedRxLength;
		    } else {
		        // Clean up on failure
		        free(cmdResult->data.rawResponse.rxData);
		        cmdResult->data.rawResponse.rxData = NULL;
		        cmdResult->data.rawResponse.rxLength = 0;
		    }
		    break;
			
		case PSB_CMD_SET_SINK_CURRENT:
		    cmdResult->errorCode = PSB_SetSinkCurrent(&ctx->handle, cmdParams->setSinkCurrent.current);
		    break;
		    
		case PSB_CMD_SET_SINK_POWER:
		    cmdResult->errorCode = PSB_SetSinkPower(&ctx->handle, cmdParams->setSinkPower.power);
		    break;
        
		case PSB_CMD_SET_SINK_CURRENT_LIMITS:
		    cmdResult->errorCode = PSB_SetSinkCurrentLimits(&ctx->handle,
		        cmdParams->sinkCurrentLimits.minCurrent,
		        cmdParams->sinkCurrentLimits.maxCurrent);
		    break;
		    
		case PSB_CMD_SET_SINK_POWER_LIMIT:
		    cmdResult->errorCode = PSB_SetSinkPowerLimit(&ctx->handle, 
		        cmdParams->sinkPowerLimit.maxPower);
		    break;
	
        default:
            cmdResult->errorCode = PSB_ERROR_INVALID_PARAM;
            break;
    }
    
    return cmdResult->errorCode;
}

static void* PSB_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    PSBCommandParams *params = malloc(sizeof(PSBCommandParams));
    if (!params) return NULL;
    
    *params = *(PSBCommandParams*)sourceParams;
    
    // Handle special cases (like raw Modbus buffers)
    if (commandType == PSB_CMD_RAW_MODBUS && sourceParams) {
        PSBCommandParams *src = (PSBCommandParams*)sourceParams;
        if (src->rawModbus.txBuffer && src->rawModbus.txLength > 0) {
            params->rawModbus.txBuffer = malloc(src->rawModbus.txLength);
            if (params->rawModbus.txBuffer) {
                memcpy(params->rawModbus.txBuffer, src->rawModbus.txBuffer, src->rawModbus.txLength);
            }
            
            if (src->rawModbus.rxBufferSize > 0) {
                params->rawModbus.rxBuffer = malloc(src->rawModbus.rxBufferSize);
            }
        }
    }
    
    return params;
}

static void PSB_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    
    PSBCommandParams *cmdParams = (PSBCommandParams*)params;
    
    // Free raw Modbus buffers
    if (commandType == PSB_CMD_RAW_MODBUS) {
        if (cmdParams->rawModbus.txBuffer) free(cmdParams->rawModbus.txBuffer);
        if (cmdParams->rawModbus.rxBuffer) free(cmdParams->rawModbus.rxBuffer);
    }
    
    free(params);
}

static void* PSB_AdapterCreateCommandResult(int commandType) {
    PSBCommandResult *result = calloc(1, sizeof(PSBCommandResult));
    
    // Initialize any command-specific result fields if needed
    
    return result;
}

static void PSB_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    
    PSBCommandResult *cmdResult = (PSBCommandResult*)result;
    
    // Free any allocated result data
	if (commandType == PSB_CMD_RAW_MODBUS && cmdResult->data.rawResponse.rxData) {
	    free(cmdResult->data.rawResponse.rxData);
	    cmdResult->data.rawResponse.rxData = NULL;
	}
    
    free(result);
}

static void PSB_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    PSBCommandResult *destResult = (PSBCommandResult*)dest;
    PSBCommandResult *srcResult = (PSBCommandResult*)src;
    
    *destResult = *srcResult;
    
    // Handle deep copies for pointer fields
    if (commandType == PSB_CMD_RAW_MODBUS && srcResult->data.rawResponse.rxData) {
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

PSBQueueManager* PSB_QueueInit(int comPort, int slaveAddress, int baudRate) {
    // Create device context
    PSBDeviceContext *context = calloc(1, sizeof(PSBDeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: Failed to allocate device context");
        return NULL;
    }
    
    // Create connection parameters
    PSBConnectionParams *connParams = calloc(1, sizeof(PSBConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInitSpecific: Failed to allocate connection params");
        return NULL;
    }
    
    connParams->comPort = comPort;
    connParams->slaveAddress = slaveAddress;
    connParams->baudRate = baudRate;
    
    // Create the generic device queue
    PSBQueueManager *mgr = DeviceQueue_Create(&g_psbAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_PSB);
    
    return mgr;
}

PSB_Handle* PSB_QueueGetHandle(PSBQueueManager *mgr) {
    PSBDeviceContext *context = (PSBDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;
    
    return &context->handle;
}

void PSB_QueueShutdown(PSBQueueManager *mgr) {
    if (!mgr) return;
    
    // Get and free the device context
    PSBDeviceContext *context = (PSBDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    
    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);
    
    // Free our contexts
    if (context) free(context);
    // Note: Connection params are freed by the generic queue
}

bool PSB_QueueIsRunning(PSBQueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void PSB_QueueGetStats(PSBQueueManager *mgr, PSBQueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

static int PSB_QueueCommandBlocking(PSBQueueManager *mgr, PSBCommandType type,
                           PSBCommandParams *params, PSBPriority priority,
                           PSBCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

static CommandID PSB_QueueCommandAsync(PSBQueueManager *mgr, PSBCommandType type,
                              PSBCommandParams *params, PSBPriority priority,
                              PSBCommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
}

bool PSB_QueueHasCommandType(PSBQueueManager *mgr, PSBCommandType type) {
    return DeviceQueue_HasCommandType(mgr, type);
}

int PSB_QueueCancelAll(PSBQueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle PSB_QueueBeginTransaction(PSBQueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int PSB_QueueAddToTransaction(PSBQueueManager *mgr, TransactionHandle txn,
                            PSBCommandType type, PSBCommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int PSB_QueueCommitTransaction(PSBQueueManager *mgr, TransactionHandle txn,
                             PSBTransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

/******************************************************************************
 * Wrapper Functions - All return ERR_QUEUE_NOT_INIT if queue not initialized
 ******************************************************************************/

void PSB_SetGlobalQueueManager(PSBQueueManager *mgr) {
    g_psbQueueManager = mgr;
}

PSBQueueManager* PSB_GetGlobalQueueManager(void) {
    return g_psbQueueManager;
}

int PSB_SetRemoteModeQueued(int enable) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.remoteMode = {enable}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_REMOTE_MODE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetOutputEnableQueued(int enable) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.outputEnable = {enable}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_OUTPUT_ENABLE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetVoltageQueued(double voltage) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.setVoltage = {voltage}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetCurrentQueued(double current) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.setCurrent = {current}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetPowerQueued(double power) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.setPower = {power}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetVoltageLimitsQueued(double minVoltage, double maxVoltage) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.voltageLimits = {minVoltage, maxVoltage}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE_LIMITS,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetCurrentLimitsQueued(double minCurrent, double maxCurrent) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.currentLimits = {minCurrent, maxCurrent}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT_LIMITS,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetPowerLimitQueued(double maxPower) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.powerLimit = {maxPower}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER_LIMIT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_GetStatusQueued(PSB_Status *status) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    if (!status) return ERR_NULL_POINTER;
    
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

int PSB_GetActualValuesQueued(double *voltage, double *current, double *power) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
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

int PSB_SetSinkCurrentQueued(double current) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.setSinkCurrent = {current}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_SINK_CURRENT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetSinkPowerQueued(double power) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.setSinkPower = {power}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_SINK_POWER,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetSinkCurrentLimitsQueued(double minCurrent, double maxCurrent) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.sinkCurrentLimits = {minCurrent, maxCurrent}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_SINK_CURRENT_LIMITS,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetSinkPowerLimitQueued(double maxPower) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    PSBCommandParams params = {.sinkPowerLimit = {maxPower}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_SINK_POWER_LIMIT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetSafeLimitsQueued(void) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    int result;
    int overallResult = PSB_SUCCESS;
    
    LogMessageEx(LOG_DEVICE_PSB, "Setting PSB safe limits...");
    
    // Set voltage limits to maximum safe range
    result = PSB_SetVoltageLimitsQueued(PSB_SAFE_VOLTAGE_MIN, PSB_SAFE_VOLTAGE_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage limits: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set current limits to maximum safe range
    result = PSB_SetCurrentLimitsQueued(PSB_SAFE_CURRENT_MIN, PSB_SAFE_CURRENT_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set current limits: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set sink current limits to maximum safe range
    result = PSB_SetSinkCurrentLimitsQueued(PSB_SAFE_SINK_CURRENT_MIN, PSB_SAFE_SINK_CURRENT_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set sink current limits: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set power limit to maximum safe value
    result = PSB_SetPowerLimitQueued(PSB_SAFE_POWER_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set power limit: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set sink power limit to maximum safe value
    result = PSB_SetSinkPowerLimitQueued(PSB_SAFE_SINK_POWER_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set sink power limit: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    if (overallResult == PSB_SUCCESS) {
        LogMessageEx(LOG_DEVICE_PSB, "PSB safe limits set successfully");
    } else {
        LogWarningEx(LOG_DEVICE_PSB, "PSB safe limits set with some warnings");
    }
    
    return overallResult;
}

int PSB_ZeroAllValuesQueued(void) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    int result;
    int overallResult = PSB_SUCCESS;
    
    LogMessageEx(LOG_DEVICE_PSB, "Zeroing all PSB values...");
    
    // Disable output first
    result = PSB_SetOutputEnableQueued(0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set voltage to 0V
    result = PSB_SetVoltageQueued(0.0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage to 0V: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set current to 0A
    result = PSB_SetCurrentQueued(0.0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set current to 0A: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set power to 0W
    result = PSB_SetPowerQueued(0.0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set power to 0W: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set sink current to 0A
    result = PSB_SetSinkCurrentQueued(0.0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set sink current to 0A: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    // Set sink power to 0W
    result = PSB_SetSinkPowerQueued(0.0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set sink power to 0W: %s", PSB_GetErrorString(result));
        overallResult = result;
    }
    
    if (overallResult == PSB_SUCCESS) {
        LogMessageEx(LOG_DEVICE_PSB, "All PSB values zeroed successfully");
    } else {
        LogWarningEx(LOG_DEVICE_PSB, "PSB values zeroed with some warnings");
    }
    
    return overallResult;
}

int PSB_SendRawModbusQueued(unsigned char *txBuffer, int txLength,
                            unsigned char *rxBuffer, int rxBufferSize, int expectedRxLength) {
    if (!g_psbQueueManager) return ERR_QUEUE_NOT_INIT;
    
    if (!txBuffer || !rxBuffer || txLength <= 0 || rxBufferSize <= 0) {
        return PSB_ERROR_INVALID_PARAM;
    }
    
    PSBCommandParams params = {0};
    PSBCommandResult result = {0};
    
    // Set up the raw Modbus parameters
    params.rawModbus.txBuffer = txBuffer;
    params.rawModbus.txLength = txLength;
    params.rawModbus.rxBuffer = rxBuffer;
    params.rawModbus.rxBufferSize = rxBufferSize;
    params.rawModbus.expectedRxLength = expectedRxLength;
    
    int error = PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_RAW_MODBUS,
                                       &params, PSB_PRIORITY_NORMAL, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (error == PSB_SUCCESS && result.data.rawResponse.rxData) {
        // Copy the response data back to the caller's buffer
        memcpy(rxBuffer, result.data.rawResponse.rxData, result.data.rawResponse.rxLength);
        
        // Free the allocated response data
        free(result.data.rawResponse.rxData);
    }
    
    return error;
}

/******************************************************************************
 * Async Command Function Implementations
 ******************************************************************************/

CommandID PSB_GetStatusAsync(PSBCommandCallback callback, void *userData) {
    PSBQueueManager *mgr = PSB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    PSBCommandParams params = {0};
    
    return PSB_QueueCommandAsync(mgr, PSB_CMD_GET_STATUS, &params,
                                PSB_PRIORITY_NORMAL, callback, userData);
}

CommandID PSB_SetRemoteModeAsync(int enable, PSBCommandCallback callback, void *userData) {
    PSBQueueManager *mgr = PSB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    PSBCommandParams params = {.remoteMode = {enable}};
    
    return PSB_QueueCommandAsync(mgr, PSB_CMD_SET_REMOTE_MODE, &params,
                                PSB_PRIORITY_HIGH, callback, userData);
}

CommandID PSB_SetOutputEnableAsync(int enable, PSBCommandCallback callback, void *userData) {
    PSBQueueManager *mgr = PSB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    PSBCommandParams params = {.outputEnable = {enable}};
    
    return PSB_QueueCommandAsync(mgr, PSB_CMD_SET_OUTPUT_ENABLE, &params,
                                PSB_PRIORITY_HIGH, callback, userData);
}

CommandID PSB_GetActualValuesAsync(PSBCommandCallback callback, void *userData) {
    PSBQueueManager *mgr = PSB_GetGlobalQueueManager();
    if (!mgr) {
        return ERR_QUEUE_NOT_INIT;
    }
    
    PSBCommandParams params = {0};
    
    return PSB_QueueCommandAsync(mgr, PSB_CMD_GET_ACTUAL_VALUES, &params,
                                PSB_PRIORITY_NORMAL, callback, userData);
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

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
 ******************************************************************************/

int PSB_QueueCancelCommand(PSBQueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int PSB_QueueCancelByType(PSBQueueManager *mgr, PSBCommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int PSB_QueueCancelByAge(PSBQueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int PSB_QueueCancelTransaction(PSBQueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}