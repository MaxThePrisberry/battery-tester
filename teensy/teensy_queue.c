/******************************************************************************
 * teensy_queue.c
 * 
 * Thread-safe command queue implementation for Teensy Microcontroller
 * Built on top of the generic device queue system
 ******************************************************************************/

#include "teensy_queue.h"
#include "logging.h"
#include <ansi_c.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* g_commandTypeNames[] = {
    "NONE",
    "SET_PIN",
    "SET_MULTIPLE_PINS",
    "SEND_RAW_COMMAND",
    "TEST_CONNECTION"
};

// Global queue manager pointer
static TNYQueueManager *g_tnyQueueManager = NULL;

/******************************************************************************
 * TNY Device Context Structure
 ******************************************************************************/

typedef struct {
    TNY_Handle handle;
    int specificPort;
    int specificBaudRate;
} TNYDeviceContext;

/******************************************************************************
 * TNY Connection Parameters
 ******************************************************************************/

typedef struct {
    int comPort;
    int baudRate;
} TNYConnectionParams;

/******************************************************************************
 * Device Adapter Implementation
 ******************************************************************************/

// Forward declarations for adapter functions
static int TNY_AdapterConnect(void *deviceContext, void *connectionParams);
static int TNY_AdapterDisconnect(void *deviceContext);
static int TNY_AdapterTestConnection(void *deviceContext);
static bool TNY_AdapterIsConnected(void *deviceContext);
static int TNY_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result);
static void* TNY_AdapterCreateCommandParams(int commandType, void *sourceParams);
static void TNY_AdapterFreeCommandParams(int commandType, void *params);
static void* TNY_AdapterCreateCommandResult(int commandType);
static void TNY_AdapterFreeCommandResult(int commandType, void *result);
static void TNY_AdapterCopyCommandResult(int commandType, void *dest, void *src);

// TNY device adapter
static const DeviceAdapter g_tnyAdapter = {
    .deviceName = "Teensy",
    
    // Connection management
    .connect = TNY_AdapterConnect,
    .disconnect = TNY_AdapterDisconnect,
    .testConnection = TNY_AdapterTestConnection,
    .isConnected = TNY_AdapterIsConnected,
    
    // Command execution
    .executeCommand = TNY_AdapterExecuteCommand,
    
    // Command management
    .createCommandParams = TNY_AdapterCreateCommandParams,
    .freeCommandParams = TNY_AdapterFreeCommandParams,
    .createCommandResult = TNY_AdapterCreateCommandResult,
    .freeCommandResult = TNY_AdapterFreeCommandResult,
    .copyCommandResult = TNY_AdapterCopyCommandResult,
    
    // Utility functions
    .getCommandTypeName = (const char* (*)(int))TNY_QueueGetCommandTypeName,
    .getCommandDelay = TNY_QueueGetCommandDelay,
    .getErrorString = GetErrorString,
    
    // Raw command support
    .supportsRawCommands = NULL,
    .executeRawCommand = NULL
};

/******************************************************************************
 * Adapter Function Implementations
 ******************************************************************************/

static int TNY_AdapterConnect(void *deviceContext, void *connectionParams) {
    TNYDeviceContext *ctx = (TNYDeviceContext*)deviceContext;
    TNYConnectionParams *params = (TNYConnectionParams*)connectionParams;
    int result;
    
    // Use specific connection parameters
    LogMessageEx(LOG_DEVICE_TNY, "Connecting to Teensy on COM%d...", params->comPort);
    result = TNY_Initialize(&ctx->handle, params->comPort, params->baudRate);
    
    if (result == TNY_SUCCESS) {
        ctx->specificPort = params->comPort;
        ctx->specificBaudRate = params->baudRate;
    }
    
    return result;
}

static int TNY_AdapterDisconnect(void *deviceContext) {
    TNYDeviceContext *ctx = (TNYDeviceContext*)deviceContext;
    
    if (ctx->handle.isConnected) {
        TNY_Close(&ctx->handle);
    }
    
    return TNY_SUCCESS;
}

static int TNY_AdapterTestConnection(void *deviceContext) {
    TNYDeviceContext *ctx = (TNYDeviceContext*)deviceContext;
    return TNY_TestConnection(&ctx->handle);
}

static bool TNY_AdapterIsConnected(void *deviceContext) {
    TNYDeviceContext *ctx = (TNYDeviceContext*)deviceContext;
    return ctx->handle.isConnected;
}

static int TNY_AdapterExecuteCommand(void *deviceContext, int commandType, void *params, void *result) {
    TNYDeviceContext *ctx = (TNYDeviceContext*)deviceContext;
    TNYCommandParams *cmdParams = (TNYCommandParams*)params;
    TNYCommandResult *cmdResult = (TNYCommandResult*)result;
    
    switch ((TNYCommandType)commandType) {
        case TNY_CMD_SET_PIN:
            cmdResult->errorCode = TNY_SetPin(&ctx->handle, 
                                            cmdParams->setPin.pin, 
                                            cmdParams->setPin.state);
            break;
            
        case TNY_CMD_SET_MULTIPLE_PINS:
            cmdResult->errorCode = TNY_SetMultiplePins(&ctx->handle,
                                                      cmdParams->setMultiplePins.pins,
                                                      cmdParams->setMultiplePins.states,
                                                      cmdParams->setMultiplePins.count);
            break;
			
		case TNY_CMD_SEND_RAW_COMMAND:
			cmdResult->errorCode = TNY_SendCommand(&ctx->handle,
												   cmdParams->sendRawCommand.command,
												   cmdParams->sendRawCommand.response,
												   cmdParams->sendRawCommand.responseSize);
			
			break;
            
        case TNY_CMD_TEST_CONNECTION:
            cmdResult->errorCode = TNY_TestConnection(&ctx->handle);
            cmdResult->data.testResult = (cmdResult->errorCode == TNY_SUCCESS) ? 1 : 0;
            break;
            
        default:
            cmdResult->errorCode = TNY_ERROR_INVALID_PARAM;
            break;
    }
    
    // Log errors appropriately
    if (cmdResult->errorCode != TNY_SUCCESS) {
        switch (cmdResult->errorCode) {
            case TNY_ERROR_TIMEOUT:
            case TNY_ERROR_COMM:
            case TNY_ERROR_NOT_CONNECTED:
                LogErrorEx(LOG_DEVICE_TNY, "Communication error: %s", 
                         TNY_GetErrorString(cmdResult->errorCode));
                break;
            default:
                LogErrorEx(LOG_DEVICE_TNY, "Command %s failed: %s",
                         TNY_QueueGetCommandTypeName(commandType),
                         TNY_GetErrorString(cmdResult->errorCode));
                break;
        }
    }
    
    return cmdResult->errorCode;
}

static void* TNY_AdapterCreateCommandParams(int commandType, void *sourceParams) {
    if (!sourceParams) return NULL;
    
    TNYCommandParams *params = malloc(sizeof(TNYCommandParams));
    if (!params) return NULL;
    
    *params = *(TNYCommandParams*)sourceParams;
    
    // Handle special cases that need deep copying
    if (commandType == TNY_CMD_SET_MULTIPLE_PINS && sourceParams) {
        TNYCommandParams *src = (TNYCommandParams*)sourceParams;
        if (src->setMultiplePins.count > 0) {
            // Allocate arrays for pins and states
            int size = src->setMultiplePins.count * sizeof(int);
            params->setMultiplePins.pins = malloc(size);
            params->setMultiplePins.states = malloc(size);
            
            if (params->setMultiplePins.pins && params->setMultiplePins.states) {
                memcpy(params->setMultiplePins.pins, src->setMultiplePins.pins, size);
                memcpy(params->setMultiplePins.states, src->setMultiplePins.states, size);
            } else {
                // Allocation failed - clean up
                if (params->setMultiplePins.pins) free(params->setMultiplePins.pins);
                if (params->setMultiplePins.states) free(params->setMultiplePins.states);
                free(params);
                return NULL;
            }
        }
    }
    
    return params;
}

static void TNY_AdapterFreeCommandParams(int commandType, void *params) {
    if (!params) return;
    
    TNYCommandParams *cmdParams = (TNYCommandParams*)params;
    
    // Free arrays for multiple pins command
    if (commandType == TNY_CMD_SET_MULTIPLE_PINS) {
        if (cmdParams->setMultiplePins.pins) free(cmdParams->setMultiplePins.pins);
        if (cmdParams->setMultiplePins.states) free(cmdParams->setMultiplePins.states);
    }
    
    free(params);
}

static void* TNY_AdapterCreateCommandResult(int commandType) {
    TNYCommandResult *result = calloc(1, sizeof(TNYCommandResult));
    return result;
}

static void TNY_AdapterFreeCommandResult(int commandType, void *result) {
    if (!result) return;
    free(result);
}

static void TNY_AdapterCopyCommandResult(int commandType, void *dest, void *src) {
    if (!dest || !src) return;
    
    TNYCommandResult *destResult = (TNYCommandResult*)dest;
    TNYCommandResult *srcResult = (TNYCommandResult*)src;
    
    *destResult = *srcResult;
}

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

TNYQueueManager* TNY_QueueInit(int comPort, int baudRate) {
    // Create device context
    TNYDeviceContext *context = calloc(1, sizeof(TNYDeviceContext));
    if (!context) {
        LogErrorEx(LOG_DEVICE_TNY, "TNY_QueueInit: Failed to allocate device context");
        return NULL;
    }
    
    // Create connection parameters
    TNYConnectionParams *connParams = calloc(1, sizeof(TNYConnectionParams));
    if (!connParams) {
        free(context);
        LogErrorEx(LOG_DEVICE_TNY, "TNY_QueueInit: Failed to allocate connection params");
        return NULL;
    }
    
    connParams->comPort = comPort;
    connParams->baudRate = (baudRate > 0) ? baudRate : TNY_DEFAULT_BAUD_RATE;
    
    // Create the generic device queue
    TNYQueueManager *mgr = DeviceQueue_Create(&g_tnyAdapter, context, connParams, 0);
    
    if (!mgr) {
        free(context);
        free(connParams);
        return NULL;
    }
    
    // Set logging device
    DeviceQueue_SetLogDevice(mgr, LOG_DEVICE_TNY);
    
    return mgr;
}

TNY_Handle* TNY_QueueGetHandle(TNYQueueManager *mgr) {
    TNYDeviceContext *context = (TNYDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    if (!context) return NULL;
    
    return &context->handle;
}

void TNY_QueueShutdown(TNYQueueManager *mgr) {
    if (!mgr) return;
    
    // Get and free the device context
    TNYDeviceContext *context = (TNYDeviceContext*)DeviceQueue_GetDeviceContext(mgr);
    
    // Destroy the generic queue (this will call disconnect)
    DeviceQueue_Destroy(mgr);
    
    // Free our contexts
    if (context) free(context);
    // Note: Connection params are freed by the generic queue
}

bool TNY_QueueIsRunning(TNYQueueManager *mgr) {
    return DeviceQueue_IsRunning(mgr);
}

void TNY_QueueGetStats(TNYQueueManager *mgr, TNYQueueStats *stats) {
    DeviceQueue_GetStats(mgr, stats);
}

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

int TNY_QueueCommandBlocking(TNYQueueManager *mgr, TNYCommandType type,
                           TNYCommandParams *params, TNYPriority priority,
                           TNYCommandResult *result, int timeoutMs) {
    return DeviceQueue_CommandBlocking(mgr, type, params, priority, result, timeoutMs);
}

CommandID TNY_QueueCommandAsync(TNYQueueManager *mgr, TNYCommandType type,
                              TNYCommandParams *params, TNYPriority priority,
                              TNYCommandCallback callback, void *userData) {
    return DeviceQueue_CommandAsync(mgr, type, params, priority, callback, userData);
}

bool TNY_QueueHasCommandType(TNYQueueManager *mgr, TNYCommandType type) {
    return DeviceQueue_HasCommandType(mgr, type);
}

int TNY_QueueCancelAll(TNYQueueManager *mgr) {
    return DeviceQueue_CancelAll(mgr);
}

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

TransactionHandle TNY_QueueBeginTransaction(TNYQueueManager *mgr) {
    return DeviceQueue_BeginTransaction(mgr);
}

int TNY_QueueAddToTransaction(TNYQueueManager *mgr, TransactionHandle txn,
                            TNYCommandType type, TNYCommandParams *params) {
    return DeviceQueue_AddToTransaction(mgr, txn, type, params);
}

int TNY_QueueCommitTransaction(TNYQueueManager *mgr, TransactionHandle txn,
                             TNYTransactionCallback callback, void *userData) {
    return DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
}

/******************************************************************************
 * Wrapper Functions
 ******************************************************************************/

void TNY_SetGlobalQueueManager(TNYQueueManager *mgr) {
    g_tnyQueueManager = mgr;
}

TNYQueueManager* TNY_GetGlobalQueueManager(void) {
    return g_tnyQueueManager;
}

int TNY_SetPinQueued(TNY_Handle *handle, int pin, int state) {
    if (!g_tnyQueueManager) return TNY_SetPin(handle, pin, state);
    
    TNYCommandParams params = {.setPin = {pin, state}};
    TNYCommandResult result;
    
    return TNY_QueueCommandBlocking(g_tnyQueueManager, TNY_CMD_SET_PIN,
                                  &params, TNY_PRIORITY_HIGH, &result,
                                  TNY_QUEUE_COMMAND_TIMEOUT_MS);
}

int TNY_SetMultiplePinsQueued(TNY_Handle *handle, const int *pins, const int *states, int count) {
    if (!g_tnyQueueManager) return TNY_SetMultiplePins(handle, pins, states, count);
    
    TNYCommandParams params = {.setMultiplePins = {(int*)pins, (int*)states, count}};
    TNYCommandResult result;
    
    return TNY_QueueCommandBlocking(g_tnyQueueManager, TNY_CMD_SET_MULTIPLE_PINS,
                                  &params, TNY_PRIORITY_HIGH, &result,
                                  TNY_QUEUE_COMMAND_TIMEOUT_MS);
}

int TNY_SendRawCommandQueued(TNY_Handle *handle, char *command, char *response, int responseSize) {
	if (!g_tnyQueueManager) return TNY_SendCommand(handle, command, response, responseSize);
	
	TNYCommandParams params = {.sendRawCommand = {command, response, responseSize}};
	TNYCommandResult result;
	
	return TNY_QueueCommandBlocking(g_tnyQueueManager, TNY_CMD_SEND_RAW_COMMAND, 
									&params, TNY_PRIORITY_HIGH, &result, 
									TNY_QUEUE_COMMAND_TIMEOUT_MS);
}

int TNY_TestConnectionQueued(TNY_Handle *handle) {
    if (!g_tnyQueueManager) return TNY_TestConnection(handle);
    
    TNYCommandParams params = {0};
    TNYCommandResult result;
    
    return TNY_QueueCommandBlocking(g_tnyQueueManager, TNY_CMD_TEST_CONNECTION,
                                  &params, TNY_PRIORITY_NORMAL, &result,
                                  TNY_QUEUE_COMMAND_TIMEOUT_MS);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* TNY_QueueGetCommandTypeName(TNYCommandType type) {
    if (type >= 0 && type < TNY_CMD_TYPE_COUNT) {
        return g_commandTypeNames[type];
    }
    return "UNKNOWN";
}

int TNY_QueueGetCommandDelay(TNYCommandType type) {
    switch (type) {
        case TNY_CMD_SET_PIN:
        case TNY_CMD_SET_MULTIPLE_PINS:
		case TNY_CMD_SEND_RAW_COMMAND:
            return TNY_DELAY_AFTER_PIN_SET;
            
        case TNY_CMD_TEST_CONNECTION:
            return TNY_DELAY_RECOVERY;
            
        default:
            return TNY_DELAY_RECOVERY;
    }
}

/******************************************************************************
 * Cancel Functions (delegate to generic queue)
 ******************************************************************************/

int TNY_QueueCancelCommand(TNYQueueManager *mgr, CommandID cmdId) {
    return DeviceQueue_CancelCommand(mgr, cmdId);
}

int TNY_QueueCancelByType(TNYQueueManager *mgr, TNYCommandType type) {
    return DeviceQueue_CancelByType(mgr, type);
}

int TNY_QueueCancelByAge(TNYQueueManager *mgr, double ageSeconds) {
    return DeviceQueue_CancelByAge(mgr, ageSeconds);
}

int TNY_QueueCancelTransaction(TNYQueueManager *mgr, TransactionHandle txn) {
    return DeviceQueue_CancelTransaction(mgr, txn);
}

/******************************************************************************
 * Advanced Transaction-Based Functions
 ******************************************************************************/

int TNY_SetPinsAtomic(TNY_Handle *handle, const TNYPinState *pinStates, int count,
                     TNYTransactionCallback callback, void *userData) {
    TNYQueueManager *queueMgr = TNY_GetGlobalQueueManager();
    if (!queueMgr || !pinStates || count <= 0) {
        return TNY_ERROR_INVALID_PARAM;
    }
    
    // Create transaction
    TransactionHandle txn = TNY_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_TNY, "Failed to begin atomic pin set transaction");
        return ERR_OPERATION_FAILED;
    }
    
    // Set transaction to high priority for atomic operations
    DeviceQueue_SetTransactionPriority(queueMgr, txn, TNY_PRIORITY_HIGH);
    
    TNYCommandParams params;
    int result = SUCCESS;
    
    // Add all pin commands to transaction
    for (int i = 0; i < count; i++) {
        params.setPin.pin = pinStates[i].pin;
        params.setPin.state = pinStates[i].state;
        
        result = TNY_QueueAddToTransaction(queueMgr, txn, TNY_CMD_SET_PIN, &params);
        if (result != SUCCESS) {
            LogErrorEx(LOG_DEVICE_TNY, "Failed to add pin %d to transaction", 
                      pinStates[i].pin);
            goto cleanup;
        }
    }
    
    // Commit transaction
    result = TNY_QueueCommitTransaction(queueMgr, txn, callback, userData);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_TNY, "Atomic pin set transaction committed (%d pins)", count);
        return SUCCESS;
    }
    
cleanup:
    TNY_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_TNY, "Failed to create atomic pin set transaction");
    return result;
}

int TNY_InitializePins(TNY_Handle *handle, 
                      const int *lowPins, int lowCount,
                      const int *highPins, int highCount) {
    TNYQueueManager *queueMgr = TNY_GetGlobalQueueManager();
    if (!queueMgr) {
        return TNY_ERROR_INVALID_PARAM;
    }
    
    int totalPins = (lowPins ? lowCount : 0) + (highPins ? highCount : 0);
    if (totalPins == 0) {
        return SUCCESS;  // Nothing to do
    }
    
    LogMessageEx(LOG_DEVICE_TNY, "Initializing %d pins (%d low, %d high)", 
                totalPins, lowCount, highCount);
    
    // Create transaction
    TransactionHandle txn = TNY_QueueBeginTransaction(queueMgr);
    if (txn == 0) {
        LogErrorEx(LOG_DEVICE_TNY, "Failed to begin pin initialization transaction");
        return ERR_OPERATION_FAILED;
    }
    
    // Set high priority for initialization
    DeviceQueue_SetTransactionPriority(queueMgr, txn, TNY_PRIORITY_HIGH);
    
    TNYCommandParams params;
    int result = SUCCESS;
    
    // Add commands to set pins low
    if (lowPins && lowCount > 0) {
        for (int i = 0; i < lowCount; i++) {
            params.setPin.pin = lowPins[i];
            params.setPin.state = TNY_PIN_STATE_LOW;
            
            result = TNY_QueueAddToTransaction(queueMgr, txn, TNY_CMD_SET_PIN, &params);
            if (result != SUCCESS) goto cleanup;
        }
    }
    
    // Add commands to set pins high
    if (highPins && highCount > 0) {
        for (int i = 0; i < highCount; i++) {
            params.setPin.pin = highPins[i];
            params.setPin.state = TNY_PIN_STATE_HIGH;
            
            result = TNY_QueueAddToTransaction(queueMgr, txn, TNY_CMD_SET_PIN, &params);
            if (result != SUCCESS) goto cleanup;
        }
    }
    
    // Commit transaction
    result = TNY_QueueCommitTransaction(queueMgr, txn, NULL, NULL);
    if (result == SUCCESS) {
        LogMessageEx(LOG_DEVICE_TNY, "Pin initialization transaction committed");
        return SUCCESS;
    }
    
cleanup:
    TNY_QueueCancelTransaction(queueMgr, txn);
    LogErrorEx(LOG_DEVICE_TNY, "Failed to initialize pins");
    return result;
}