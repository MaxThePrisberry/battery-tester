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
    "RAW_MODBUS"
};

// Global queue manager pointer
static PSBQueueManager *g_psbQueueManager = NULL;

/******************************************************************************
 * PSB Device Context Structure
 ******************************************************************************/

typedef struct {
    PSB_Handle handle;
    char targetSerial[64];
    bool autoDiscovery;
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
    bool autoDiscovery;
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
    .getErrorString = GetErrorString,
    
    // Raw command support
    .supportsRawCommands = NULL,
    .executeRawCommand = NULL
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int PSB_AdapterConnect(void *deviceContext, void *connectionParams) {
    PSBDeviceContext *ctx = (PSBDeviceContext*)deviceContext;
    PSBConnectionParams *params = (PSBConnectionParams*)connectionParams;
    int result;
    
    if (params->autoDiscovery) {
        // Use auto-discovery
        LogMessageEx(LOG_DEVICE_PSB, "Auto-discovering PSB with serial %s...", params->targetSerial);
        result = PSB_AutoDiscover(params->targetSerial, &ctx->handle);
        
        if (result == PSB_SUCCESS) {
            SAFE_STRCPY(ctx->targetSerial, params->targetSerial, sizeof(ctx->targetSerial));
            ctx->autoDiscovery = true;
            
            // Set initial state
            PSB_SetRemoteMode(&ctx->handle, 1);
            PSB_SetOutputEnable(&ctx->handle, 0);  // Start with output disabled
            
            // Get initial status
            PSB_Status status;
            if (PSB_GetStatus(&ctx->handle, &status) == PSB_SUCCESS) {
                LogMessageEx(LOG_DEVICE_PSB, "PSB Status: Output=%s, Remote=%s", 
                           status.outputEnabled ? "ON" : "OFF",
                           status.remoteMode ? "YES" : "NO");
            }
        }
    } else {
        // Use specific connection parameters
        LogMessageEx(LOG_DEVICE_PSB, "Connecting to PSB on COM%d...", params->comPort);
        result = PSB_InitializeSpecific(&ctx->handle, params->comPort, 
                                      params->slaveAddress, params->baudRate);
        
        if (result == PSB_SUCCESS) {
            ctx->autoDiscovery = false;
            ctx->specificPort = params->comPort;
            ctx->specificBaudRate = params->baudRate;
            ctx->specificSlaveAddress = params->slaveAddress;
            
            // Set initial state
            PSB_SetRemoteMode(&ctx->handle, 1);
            PSB_SetOutputEnable(&ctx->handle, 0);
        }
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
            // TODO: Implement raw Modbus command execution using PSB_SendRawModbus
            cmdResult->errorCode = PSB_ERROR_NOT_SUPPORTED;
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

PSBQueueManager* PSB_QueueInit(const char *targetSerial) {
    if (!targetSerial || strlen(targetSerial) == 0) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: No target serial number provided");
        return NULL;
    }
    
    // Create device context
    PSBDeviceContext *context = calloc(1, sizeof(PSBDeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: Failed to allocate device context");
        return NULL;
    }
    
    // Create connection parameters
    PSBConnectionParams *connParams = calloc(1, sizeof(PSBConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_PSB, "PSB_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    SAFE_STRCPY(connParams->targetSerial, targetSerial, sizeof(connParams->targetSerial));
    connParams->autoDiscovery = true;
    
    // Create the generic device queue
    PSBQueueManager *mgr = DeviceQueue_Create(&g_psbAdapter, context, connParams);
    
    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_PSB);
    
    return mgr;
}

PSBQueueManager* PSB_QueueInitSpecific(int comPort, int slaveAddress, int baudRate) {
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
    connParams->autoDiscovery = false;
    
    // Create the generic device queue
    PSBQueueManager *mgr = DeviceQueue_Create(&g_psbAdapter, context, connParams);
    
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

int PSB_QueueCommandBlocking(PSBQueueManager *mgr, PSBCommandType type,
                           PSBCommandParams *params, PSBPriority priority,
                           PSBCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

CommandID PSB_QueueCommandAsync(PSBQueueManager *mgr, PSBCommandType type,
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
 * Wrapper Functions
 ******************************************************************************/

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
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_REMOTE_MODE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetOutputEnableQueued(PSB_Handle *handle, int enable) {
    if (!g_psbQueueManager) return PSB_SetOutputEnable(handle, enable);
    
    PSBCommandParams params = {.outputEnable = {enable}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_OUTPUT_ENABLE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetVoltageQueued(PSB_Handle *handle, double voltage) {
    if (!g_psbQueueManager) return PSB_SetVoltage(handle, voltage);
    
    PSBCommandParams params = {.setVoltage = {voltage}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetCurrentQueued(PSB_Handle *handle, double current) {
    if (!g_psbQueueManager) return PSB_SetCurrent(handle, current);
    
    PSBCommandParams params = {.setCurrent = {current}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetPowerQueued(PSB_Handle *handle, double power) {
    if (!g_psbQueueManager) return PSB_SetPower(handle, power);
    
    PSBCommandParams params = {.setPower = {power}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetVoltageLimitsQueued(PSB_Handle *handle, double minVoltage, double maxVoltage) {
    if (!g_psbQueueManager) return PSB_SetVoltageLimits(handle, minVoltage, maxVoltage);
    
    PSBCommandParams params = {.voltageLimits = {minVoltage, maxVoltage}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_VOLTAGE_LIMITS,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetCurrentLimitsQueued(PSB_Handle *handle, double minCurrent, double maxCurrent) {
    if (!g_psbQueueManager) return PSB_SetCurrentLimits(handle, minCurrent, maxCurrent);
    
    PSBCommandParams params = {.currentLimits = {minCurrent, maxCurrent}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_CURRENT_LIMITS,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
}

int PSB_SetPowerLimitQueued(PSB_Handle *handle, double maxPower) {
    if (!g_psbQueueManager) return PSB_SetPowerLimit(handle, maxPower);
    
    PSBCommandParams params = {.powerLimit = {maxPower}};
    PSBCommandResult result;
    
    return PSB_QueueCommandBlocking(g_psbQueueManager, PSB_CMD_SET_POWER_LIMIT,
                                  &params, PSB_PRIORITY_HIGH, &result,
                                  PSB_QUEUE_COMMAND_TIMEOUT_MS);
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
 * Not Implemented Functions (delegate to generic queue)
 ******************************************************************************/

int PSB_QueueCancelCommand(PSBQueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int PSB_QueueCancelByType(PSBQueueManager *mgr, PSBCommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int PSB_QueueCancelByAge(PSBQueueManager *mgr, double ageSeconds) {
    // Not implemented in generic queue yet
    return ERR_NOT_SUPPORTED;
}

int PSB_QueueCancelTransaction(PSBQueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}