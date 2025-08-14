/******************************************************************************
 * dtb4848_queue.h
 * 
 * Thread-safe command queue implementation for DTB 4848 Temperature Controller
 * Built on top of the generic device queue system
 * 
 * Supports multiple DTB devices on the same COM port with different slave addresses
 ******************************************************************************/

#ifndef DTB4848_QUEUE_H
#define DTB4848_QUEUE_H

#include "common.h"
#include "dtb4848_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Maximum number of DTB devices supported on one COM port
#define MAX_DTB_DEVICES             4

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
typedef DeviceCommandCallback DTBCommandCallback;
typedef DeviceTransactionCallback DTBTransactionCallback;
typedef DeviceQueueStats DTBQueueStats;

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
    DTB_CMD_SET_HEATING_COOLING,
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
    struct { int slaveAddress; int run; } runStop;
    struct { int slaveAddress; double temperature; } setpoint;
    struct { int slaveAddress; int method; } controlMethod;
    struct { int slaveAddress; int mode; } pidMode;
    struct { int slaveAddress; int sensorType; } sensorType;
    struct { int slaveAddress; double upperLimit; double lowerLimit; } temperatureLimits;
    struct { int slaveAddress; double upperLimit; double lowerLimit; } alarmLimits;
    struct { int slaveAddress; DTB_Configuration config; } configure;
    struct { int slaveAddress; } configureDefault;
    struct { int slaveAddress; } factoryReset;
    struct { int slaveAddress; int pidNumber; } getPidParams;
    struct { int slaveAddress; int lockMode; } frontPanelLock;
    struct { int slaveAddress; int mode; } heatingCooling;
    struct { int slaveAddress; } getStatus;
    struct { int slaveAddress; } getProcessValue;
    struct { int slaveAddress; } getSetpoint;
    struct { int slaveAddress; } getAlarmStatus;
    struct { int slaveAddress; } clearAlarm;
    struct { int slaveAddress; } getFrontPanelLock;
    struct { int slaveAddress; } enableWriteAccess;
    struct { int slaveAddress; } disableWriteAccess;
    struct { int slaveAddress; } getWriteAccessStatus;
    struct { 
        int slaveAddress;
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
 * Initialize the queue manager with multiple devices on the same COM port
 * @param comPort - COM port number (1-16)
 * @param baudRate - Baud rate (e.g., 9600)
 * @param slaveAddresses - Array of Modbus slave addresses
 * @param numSlaves - Number of slave addresses (1 to MAX_DTB_DEVICES)
 * @return Queue manager instance or NULL on failure
 */
DTBQueueManager* DTB_QueueInit(int comPort, int baudRate, int *slaveAddresses, int numSlaves);

/**
 * Get the DTB handle for a specific slave address
 * @param mgr - Queue manager instance
 * @param slaveAddress - Modbus slave address
 * @return DTB handle or NULL if slave address not found or not connected
 */
DTB_Handle* DTB_QueueGetHandle(DTBQueueManager *mgr, int slaveAddress);

// Shutdown the queue manager
void DTB_QueueShutdown(DTBQueueManager *mgr);

// Check if queue manager is running
bool DTB_QueueIsRunning(DTBQueueManager *mgr);

// Get queue statistics
void DTB_QueueGetStats(DTBQueueManager *mgr, DTBQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

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
 * Individual Device Functions (Global queue manager required)
 * Note: These functions require DTB_SetGlobalQueueManager() to be called first
 ******************************************************************************/

// Control functions
int DTB_SetRunStopQueued(int slaveAddress, int run, DevicePriority priority);
int DTB_SetSetPointQueued(int slaveAddress, double temperature, DevicePriority priority);
int DTB_StartAutoTuningQueued(int slaveAddress, DevicePriority priority);
int DTB_StopAutoTuningQueued(int slaveAddress, DevicePriority priority);

// Configuration functions
int DTB_SetControlMethodQueued(int slaveAddress, int method, DevicePriority priority);
int DTB_SetPIDModeQueued(int slaveAddress, int mode, DevicePriority priority);
int DTB_SetSensorTypeQueued(int slaveAddress, int sensorType, DevicePriority priority);
int DTB_SetTemperatureLimitsQueued(int slaveAddress, double upperLimit, double lowerLimit, DevicePriority priority);
int DTB_SetAlarmLimitsQueued(int slaveAddress, double upperLimit, double lowerLimit, DevicePriority priority);
int DTB_SetHeatingCoolingQueued(int slaveAddress, int mode, DevicePriority priority);
int DTB_ConfigureQueued(int slaveAddress, const DTB_Configuration *config, DevicePriority priority);
int DTB_ConfigureDefaultQueued(int slaveAddress, DevicePriority priority);
int DTB_FactoryResetQueued(int slaveAddress, DevicePriority priority);

// Read functions
int DTB_GetStatusQueued(int slaveAddress, DTB_Status *status, DevicePriority priority);
int DTB_GetProcessValueQueued(int slaveAddress, double *temperature, DevicePriority priority);
int DTB_GetSetPointQueued(int slaveAddress, double *setPoint, DevicePriority priority);
int DTB_GetPIDParamsQueued(int slaveAddress, int pidNumber, DTB_PIDParams *params, DevicePriority priority);
int DTB_GetAlarmStatusQueued(int slaveAddress, int *alarmActive, DevicePriority priority);

// Alarm functions
int DTB_ClearAlarmQueued(int slaveAddress, DevicePriority priority);

// Front panel lock functions
int DTB_SetFrontPanelLockQueued(int slaveAddress, int lockMode, DevicePriority priority);
int DTB_GetFrontPanelLockQueued(int slaveAddress, int *lockMode, DevicePriority priority);
int DTB_UnlockFrontPanelQueued(int slaveAddress, DevicePriority priority);
int DTB_LockFrontPanelQueued(int slaveAddress, int allowSetpointChange, DevicePriority priority);

// Write protection functions
int DTB_EnableWriteAccessQueued(int slaveAddress, DevicePriority priority);
int DTB_DisableWriteAccessQueued(int slaveAddress, DevicePriority priority);
int DTB_GetWriteAccessStatusQueued(int slaveAddress, int *isEnabled, DevicePriority priority);

// Raw command support
int DTB_SendRawModbusQueued(int slaveAddress, unsigned char functionCode,
                           unsigned short address, unsigned short data,
                           unsigned char *rxBuffer, int rxBufferSize, DevicePriority priority);

/******************************************************************************
 * "All Devices" Convenience Functions
 ******************************************************************************/

/**
 * Set run/stop state for all initialized DTB devices
 * @param run - 1 to run, 0 to stop
 * @param priority - Command priority for all device commands
 * @return DTB_SUCCESS if all devices succeeded, error code of first failure otherwise
 */
int DTB_SetRunStopAllQueued(int run, DevicePriority priority);

/**
 * Configure all initialized DTB devices with the same default configuration
 * @param priority - Command priority for all device commands
 * @return DTB_SUCCESS if all devices succeeded, error code of first failure otherwise
 */
int DTB_ConfigureAllDefaultQueued(DevicePriority priority);

/**
 * Enable write access for all initialized DTB devices
 * @param priority - Command priority for all device commands
 * @return DTB_SUCCESS if all devices succeeded, error code of first failure otherwise
 */
int DTB_EnableWriteAccessAllQueued(DevicePriority priority);

/******************************************************************************
 * Async Command Functions
 * 
 * These functions return CommandID on success or ERR_QUEUE_NOT_INIT if the 
 * queue is not initialized
 ******************************************************************************/

/**
 * Get DTB status asynchronously
 * @param slaveAddress - Modbus slave address of target device
 * @param callback - Callback function to be called when command completes
 * @param userData - User data passed to callback
 * @param priority - Command priority
 * @return Command ID on success or ERR_QUEUE_NOT_INIT if queue not initialized
 */
CommandID DTB_GetStatusAsync(int slaveAddress, DTBCommandCallback callback, void *userData, DevicePriority priority);

/**
 * Set DTB run/stop state asynchronously
 * @param slaveAddress - Modbus slave address of target device
 * @param run - 1 to run, 0 to stop
 * @param callback - Callback function to be called when command completes
 * @param userData - User data passed to callback
 * @param priority - Command priority
 * @return Command ID on success or ERR_QUEUE_NOT_INIT if queue not initialized
 */
CommandID DTB_SetRunStopAsync(int slaveAddress, int run, DTBCommandCallback callback, void *userData, DevicePriority priority);

/**
 * Set DTB temperature setpoint asynchronously
 * @param slaveAddress - Modbus slave address of target device
 * @param temperature - Target temperature in degrees Celsius
 * @param callback - Callback function to be called when command completes
 * @param userData - User data passed to callback
 * @param priority - Command priority
 * @return Command ID on success or ERR_QUEUE_NOT_INIT if queue not initialized
 */
CommandID DTB_SetSetPointAsync(int slaveAddress, double temperature, DTBCommandCallback callback, void *userData, DevicePriority priority);

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
 * @param slaveAddress - Modbus slave address of target device
 * @param config - Complete configuration structure
 * @param callback - Optional callback for transaction completion
 * @param userData - User data for callback
 * @param priority - Priority for all commands in the transaction
 * @return SUCCESS or error code
 */
int DTB_ConfigureAtomic(int slaveAddress, const DTB_Configuration *config,
                       DTBTransactionCallback callback, void *userData, DevicePriority priority);

/**
 * Safely change control method with PID parameters
 * Uses transaction to ensure consistent state
 * 
 * @param slaveAddress - Modbus slave address of target device
 * @param method - Control method (PID, ON/OFF, etc.)
 * @param pidMode - PID mode (if method is PID)
 * @param pidParams - PID parameters (if method is PID, can be NULL)
 * @param priority - Priority for all commands in the transaction
 * @return SUCCESS or error code
 */
int DTB_SetControlMethodWithParams(int slaveAddress, int method, int pidMode,
                                  const DTB_PIDParams *pidParams, DevicePriority priority);

#endif // DTB4848_QUEUE_H