/******************************************************************************
 * dtb4848_queue.h
 * 
 * Thread-safe command queue implementation for DTB 4848 Temperature Controller
 * Built on top of the generic device queue system
 ******************************************************************************/

#ifndef DTB4848_QUEUE_H
#define DTB4848_QUEUE_H

#include "common.h"
#include "dtb4848_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Command delays (milliseconds)
#define DTB_DELAY_AFTER_WRITE_BIT       50    // After write single bit (run/stop, auto-tune)
#define DTB_DELAY_AFTER_WRITE_REGISTER  50    // After write register
#define DTB_DELAY_AFTER_READ            50    // After read operations
#define DTB_DELAY_STATE_CHANGE          500   // After run/stop state change
#define DTB_DELAY_SETPOINT_CHANGE       200   // After temperature setpoint change
#define DTB_DELAY_CONFIG_CHANGE         300   // After configuration changes (PID mode, control method)
#define DTB_DELAY_RECOVERY              50    // General recovery between commands

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager DTBQueueManager;
typedef DeviceTransactionHandle TransactionHandle;
typedef DeviceCommandID CommandID;
typedef DevicePriority DTBPriority;
typedef DeviceCommandCallback DTBCommandCallback;
typedef DeviceTransactionCallback DTBTransactionCallback;
typedef DeviceQueueStats DTBQueueStats;

// Map priority levels
#define DTB_PRIORITY_HIGH    DEVICE_PRIORITY_HIGH
#define DTB_PRIORITY_NORMAL  DEVICE_PRIORITY_NORMAL
#define DTB_PRIORITY_LOW     DEVICE_PRIORITY_LOW

// Map transaction constants
#define DTB_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define DTB_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Command types
typedef enum {
    DTB_CMD_NONE = 0,
    
    // Control commands
    DTB_CMD_SET_RUN_STOP,
    DTB_CMD_SET_SETPOINT,
    DTB_CMD_START_AUTO_TUNING,
    DTB_CMD_STOP_AUTO_TUNING,
    
    // Configuration commands
    DTB_CMD_SET_CONTROL_METHOD,
    DTB_CMD_SET_PID_MODE,
    DTB_CMD_SET_SENSOR_TYPE,
    DTB_CMD_SET_TEMPERATURE_LIMITS,
    DTB_CMD_SET_ALARM_LIMITS,
    DTB_CMD_CONFIGURE,
    DTB_CMD_CONFIGURE_DEFAULT,
    DTB_CMD_FACTORY_RESET,
    
    // Query commands
    DTB_CMD_GET_STATUS,
    DTB_CMD_GET_PROCESS_VALUE,
    DTB_CMD_GET_SETPOINT,
    DTB_CMD_GET_PID_PARAMS,
    DTB_CMD_GET_ALARM_STATUS,
    
    // Alarm commands
    DTB_CMD_CLEAR_ALARM,
	
	// Front panel lock commands
    DTB_CMD_SET_FRONT_PANEL_LOCK,
    DTB_CMD_GET_FRONT_PANEL_LOCK,
	
	// Write access commands
	DTB_CMD_ENABLE_WRITE_ACCESS,
	DTB_CMD_DISABLE_WRITE_ACCESS,
	DTB_CMD_GET_WRITE_ACCESS_STATUS,
    
    // Raw Modbus commands
    DTB_CMD_RAW_MODBUS,
    
    DTB_CMD_TYPE_COUNT
} DTBCommandType;

// Command parameters union
typedef union {
    struct { int run; } runStop;
    struct { double temperature; } setpoint;
    struct { int method; } controlMethod;
    struct { int mode; } pidMode;
    struct { int sensorType; } sensorType;
    struct { double upperLimit; double lowerLimit; } temperatureLimits;
    struct { double upperLimit; double lowerLimit; } alarmLimits;
    struct { DTB_Configuration config; } configure;
    struct { int pidNumber; } getPidParams;
	struct { int lockMode; } frontPanelLock;
    struct { 
        unsigned char functionCode;
        unsigned short address;
        unsigned short data;
        unsigned char *rxBuffer;
        int rxBufferSize;
    } rawModbus;
} DTBCommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        DTB_Status status;
        double temperature;
        double setpoint;
        DTB_PIDParams pidParams;
        int alarmActive;
		int frontPanelLockMode;
		int writeAccessEnabled;
        struct { unsigned char *rxData; int rxLength; } rawResponse;
    } data;
} DTBCommandResult;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

/**
 * Initialize the queue manager with specific connection parameters
 * @param comPort - COM port number (1-16)
 * @param slaveAddress - Modbus slave address
 * @param baudRate - Baud rate (e.g., 9600)
 * @return Queue manager instance or NULL on failure
 */
DTBQueueManager* DTB_QueueInit(int comPort, int slaveAddress, int baudRate);

/**
 * Get the DTB handle from the queue manager
 * @param mgr - Queue manager instance
 * @return DTB handle or NULL if not connected
 */
DTB_Handle* DTB_QueueGetHandle(DTBQueueManager *mgr);

// Shutdown the queue manager
void DTB_QueueShutdown(DTBQueueManager *mgr);

// Check if queue manager is running
bool DTB_QueueIsRunning(DTBQueueManager *mgr);

// Get queue statistics
void DTB_QueueGetStats(DTBQueueManager *mgr, DTBQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Queue a command (blocking)
int DTB_QueueCommandBlocking(DTBQueueManager *mgr, DTBCommandType type,
                           DTBCommandParams *params, DTBPriority priority,
                           DTBCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
CommandID DTB_QueueCommandAsync(DTBQueueManager *mgr, DTBCommandType type,
                              DTBCommandParams *params, DTBPriority priority,
                              DTBCommandCallback callback, void *userData);

// Cancel commands
int DTB_QueueCancelCommand(DTBQueueManager *mgr, CommandID cmdId);
int DTB_QueueCancelByType(DTBQueueManager *mgr, DTBCommandType type);
int DTB_QueueCancelByAge(DTBQueueManager *mgr, double ageSeconds);
int DTB_QueueCancelAll(DTBQueueManager *mgr);

// Check if a command type is already queued
bool DTB_QueueHasCommandType(DTBQueueManager *mgr, DTBCommandType type);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
TransactionHandle DTB_QueueBeginTransaction(DTBQueueManager *mgr);

// Add command to transaction
int DTB_QueueAddToTransaction(DTBQueueManager *mgr, TransactionHandle txn,
                            DTBCommandType type, DTBCommandParams *params);

// Commit transaction (async)
int DTB_QueueCommitTransaction(DTBQueueManager *mgr, TransactionHandle txn,
                             DTBTransactionCallback callback, void *userData);

// Cancel transaction
int DTB_QueueCancelTransaction(DTBQueueManager *mgr, TransactionHandle txn);

/******************************************************************************
 * Wrapper Functions (Direct replacements for existing DTB functions)
 ******************************************************************************/

// Control functions
int DTB_SetRunStopQueued(DTB_Handle *handle, int run);
int DTB_SetSetPointQueued(DTB_Handle *handle, double temperature);
int DTB_StartAutoTuningQueued(DTB_Handle *handle);
int DTB_StopAutoTuningQueued(DTB_Handle *handle);

// Configuration functions
int DTB_SetControlMethodQueued(DTB_Handle *handle, int method);
int DTB_SetPIDModeQueued(DTB_Handle *handle, int mode);
int DTB_SetSensorTypeQueued(DTB_Handle *handle, int sensorType);
int DTB_SetTemperatureLimitsQueued(DTB_Handle *handle, double upperLimit, double lowerLimit);
int DTB_SetAlarmLimitsQueued(DTB_Handle *handle, double upperLimit, double lowerLimit);
int DTB_ConfigureQueued(DTB_Handle *handle, const DTB_Configuration *config);
int DTB_ConfigureDefaultQueued(DTB_Handle *handle);
int DTB_FactoryResetQueued(DTB_Handle *handle);

// Read functions
int DTB_GetStatusQueued(DTB_Handle *handle, DTB_Status *status);
int DTB_GetProcessValueQueued(DTB_Handle *handle, double *temperature);
int DTB_GetSetPointQueued(DTB_Handle *handle, double *setPoint);
int DTB_GetPIDParamsQueued(DTB_Handle *handle, int pidNumber, DTB_PIDParams *params);
int DTB_GetAlarmStatusQueued(DTB_Handle *handle, int *alarmActive);

// Alarm functions
int DTB_ClearAlarmQueued(DTB_Handle *handle);

// Front panel lock functions
int DTB_SetFrontPanelLockQueued(DTB_Handle *handle, int lockMode);
int DTB_GetFrontPanelLockQueued(DTB_Handle *handle, int *lockMode);
int DTB_UnlockFrontPanelQueued(DTB_Handle *handle);
int DTB_LockFrontPanelQueued(DTB_Handle *handle, int allowSetpointChange);

// Write protection functions
int DTB_EnableWriteAccessQueued(DTB_Handle *handle);
int DTB_DisableWriteAccessQueued(DTB_Handle *handle);
int DTB_GetWriteAccessStatusQueued(DTB_Handle *handle, int *isEnabled);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* DTB_QueueGetCommandTypeName(DTBCommandType type);

// Get delay for command type
int DTB_QueueGetCommandDelay(DTBCommandType type);

// Set/Get global queue manager
void DTB_SetGlobalQueueManager(DTBQueueManager *mgr);
DTBQueueManager* DTB_GetGlobalQueueManager(void);

/**
 * Safely configure temperature controller with atomic transaction
 * This ensures all configuration parameters are set together
 * 
 * @param handle - DTB handle (can be NULL to use global queue manager)
 * @param config - Complete configuration structure
 * @param callback - Optional callback for transaction completion
 * @param userData - User data for callback
 * @return SUCCESS or error code
 */
int DTB_ConfigureAtomic(DTB_Handle *handle, const DTB_Configuration *config,
                       DTBTransactionCallback callback, void *userData);

/**
 * Safely change control method with PID parameters
 * Uses transaction to ensure consistent state
 * 
 * @param handle - DTB handle (can be NULL to use global queue manager)
 * @param method - Control method (PID, ON/OFF, etc.)
 * @param pidMode - PID mode (if method is PID)
 * @param pidParams - PID parameters (if method is PID, can be NULL)
 * @return SUCCESS or error code
 */
int DTB_SetControlMethodWithParams(DTB_Handle *handle, int method, int pidMode,
                                  const DTB_PIDParams *pidParams);

#endif // DTB4848_QUEUE_H