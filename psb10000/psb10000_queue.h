/******************************************************************************
 * psb10000_queue.h
 * 
 * Thread-safe command queue implementation for PSB 10000 Series Power Supply
 * Built on top of the generic device queue system
 ******************************************************************************/

#ifndef PSB10000_QUEUE_H
#define PSB10000_QUEUE_H

#include "common.h"
#include "psb10000_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Command delays (milliseconds)
#define PSB_DELAY_AFTER_WRITE_COIL      50    // After write single coil
#define PSB_DELAY_AFTER_WRITE_REGISTER  50    // After write register
#define PSB_DELAY_AFTER_READ            50    // After read registers
#define PSB_DELAY_STATE_CHANGE          500   // After remote mode, output enable
#define PSB_DELAY_PARAM_CHANGE          200   // After voltage/current/power set
#define PSB_DELAY_RECOVERY              50    // General recovery between commands

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager PSBQueueManager;
typedef DeviceTransactionHandle TransactionHandle;
typedef DeviceCommandID CommandID;
typedef DevicePriority PSBPriority;
typedef DeviceCommandCallback PSBCommandCallback;
typedef DeviceTransactionCallback PSBTransactionCallback;
typedef DeviceQueueStats PSBQueueStats;

// Map priority levels
#define PSB_PRIORITY_HIGH    DEVICE_PRIORITY_HIGH
#define PSB_PRIORITY_NORMAL  DEVICE_PRIORITY_NORMAL
#define PSB_PRIORITY_LOW     DEVICE_PRIORITY_LOW

// Map transaction constants
#define PSB_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define PSB_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Command types
typedef enum {
    PSB_CMD_NONE = 0,
    
    // Control commands
    PSB_CMD_SET_REMOTE_MODE,
    PSB_CMD_SET_OUTPUT_ENABLE,
    
    // Parameter commands
    PSB_CMD_SET_VOLTAGE,
    PSB_CMD_SET_CURRENT,
    PSB_CMD_SET_POWER,
    PSB_CMD_SET_VOLTAGE_LIMITS,
    PSB_CMD_SET_CURRENT_LIMITS,
    PSB_CMD_SET_POWER_LIMIT,
    
    // Query commands
    PSB_CMD_GET_STATUS,
    PSB_CMD_GET_ACTUAL_VALUES,
    
    // Raw Modbus commands
    PSB_CMD_RAW_MODBUS,
    
	PSB_CMD_SET_SINK_CURRENT,
	PSB_CMD_SET_SINK_POWER,
	PSB_CMD_SET_SINK_CURRENT_LIMITS,
	PSB_CMD_SET_SINK_POWER_LIMIT,
	
    PSB_CMD_TYPE_COUNT
} PSBCommandType;

// Command parameters union
typedef union {
    struct { int enable; } remoteMode;
    struct { int enable; } outputEnable;
    struct { double voltage; } setVoltage;
    struct { double current; } setCurrent;
    struct { double power; } setPower;
    struct { double minVoltage; double maxVoltage; } voltageLimits;
    struct { double minCurrent; double maxCurrent; } currentLimits;
    struct { double maxPower; } powerLimit;
	struct { double current; } setSinkCurrent;
	struct { double power; } setSinkPower;
	struct { double minCurrent; double maxCurrent; } sinkCurrentLimits;
	struct { double maxPower; } sinkPowerLimit;
    struct { 
        unsigned char *txBuffer; 
        int txLength; 
        unsigned char *rxBuffer; 
        int rxBufferSize;
        int expectedRxLength;
    } rawModbus;
} PSBCommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        PSB_Status status;
        struct { double voltage; double current; double power; } actualValues;
        struct { unsigned char *rxData; int rxLength; } rawResponse;
    } data;
} PSBCommandResult;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

/**
 * Initialize the queue manager with auto-discovery for a PSB device
 * @param targetSerial - Serial number of the PSB to find (e.g., "2872380001")
 * @return Queue manager instance or NULL on failure
 */
PSBQueueManager* PSB_QueueInit(const char *targetSerial);

/**
 * Initialize the queue manager with specific connection parameters
 * @param comPort - COM port number (1-16)
 * @param slaveAddress - Modbus slave address
 * @param baudRate - Baud rate (e.g., 115200)
 * @return Queue manager instance or NULL on failure
 */
PSBQueueManager* PSB_QueueInitSpecific(int comPort, int slaveAddress, int baudRate);

/**
 * Get the PSB handle from the queue manager
 * @param mgr - Queue manager instance
 * @return PSB handle or NULL if not connected
 */
PSB_Handle* PSB_QueueGetHandle(PSBQueueManager *mgr);

// Shutdown the queue manager
void PSB_QueueShutdown(PSBQueueManager *mgr);

// Check if queue manager is running
bool PSB_QueueIsRunning(PSBQueueManager *mgr);

// Get queue statistics
void PSB_QueueGetStats(PSBQueueManager *mgr, PSBQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Queue a command (blocking)
int PSB_QueueCommandBlocking(PSBQueueManager *mgr, PSBCommandType type,
                           PSBCommandParams *params, PSBPriority priority,
                           PSBCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
CommandID PSB_QueueCommandAsync(PSBQueueManager *mgr, PSBCommandType type,
                              PSBCommandParams *params, PSBPriority priority,
                              PSBCommandCallback callback, void *userData);

// Cancel commands
int PSB_QueueCancelCommand(PSBQueueManager *mgr, CommandID cmdId);
int PSB_QueueCancelByType(PSBQueueManager *mgr, PSBCommandType type);
int PSB_QueueCancelByAge(PSBQueueManager *mgr, double ageSeconds);
int PSB_QueueCancelAll(PSBQueueManager *mgr);

// Check if a command type is already queued
bool PSB_QueueHasCommandType(PSBQueueManager *mgr, PSBCommandType type);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
TransactionHandle PSB_QueueBeginTransaction(PSBQueueManager *mgr);

// Add command to transaction
int PSB_QueueAddToTransaction(PSBQueueManager *mgr, TransactionHandle txn,
                            PSBCommandType type, PSBCommandParams *params);

// Commit transaction (async)
int PSB_QueueCommitTransaction(PSBQueueManager *mgr, TransactionHandle txn,
                             PSBTransactionCallback callback, void *userData);

// Cancel transaction
int PSB_QueueCancelTransaction(PSBQueueManager *mgr, TransactionHandle txn);

/******************************************************************************
 * Wrapper Functions (Direct replacements for existing PSB functions)
 ******************************************************************************/

// These maintain the existing API but use the queue internally
int PSB_SetRemoteModeQueued(PSB_Handle *handle, int enable);
int PSB_SetOutputEnableQueued(PSB_Handle *handle, int enable);
int PSB_SetVoltageQueued(PSB_Handle *handle, double voltage);
int PSB_SetCurrentQueued(PSB_Handle *handle, double current);
int PSB_SetPowerQueued(PSB_Handle *handle, double power);
int PSB_SetVoltageLimitsQueued(PSB_Handle *handle, double minVoltage, double maxVoltage);
int PSB_SetCurrentLimitsQueued(PSB_Handle *handle, double minCurrent, double maxCurrent);
int PSB_SetPowerLimitQueued(PSB_Handle *handle, double maxPower);
int PSB_SetSinkCurrentQueued(PSB_Handle *handle, double current);
int PSB_SetSinkPowerQueued(PSB_Handle *handle, double power);
int PSB_SetSinkCurrentLimitsQueued(PSB_Handle *handle, double minCurrent, double maxCurrent);
int PSB_SetSinkPowerLimitQueued(PSB_Handle *handle, double maxPower);
int PSB_GetStatusQueued(PSB_Handle *handle, PSB_Status *status);
int PSB_GetActualValuesQueued(PSB_Handle *handle, double *voltage, double *current, double *power);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* PSB_QueueGetCommandTypeName(PSBCommandType type);

// Get delay for command type
int PSB_QueueGetCommandDelay(PSBCommandType type);

// Set/Get global queue manager
void PSB_SetGlobalQueueManager(PSBQueueManager *mgr);
PSBQueueManager* PSB_GetGlobalQueueManager(void);

#endif // PSB10000_QUEUE_H