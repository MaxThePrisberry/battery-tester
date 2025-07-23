/******************************************************************************
 * teensy_queue.h
 * 
 * Thread-safe command queue implementation for Teensy Microcontroller
 * Built on top of the generic device queue system
 ******************************************************************************/

#ifndef TEENSY_QUEUE_H
#define TEENSY_QUEUE_H

#include "common.h"
#include "teensy_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Command delays (milliseconds)
#define TNY_DELAY_AFTER_PIN_SET      20    // After setting a pin
#define TNY_DELAY_RECOVERY           10    // General recovery between commands

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager TNYQueueManager;
typedef DeviceTransactionHandle TransactionHandle;
typedef DeviceCommandID CommandID;
typedef DevicePriority TNYPriority;
typedef DeviceCommandCallback TNYCommandCallback;
typedef DeviceTransactionCallback TNYTransactionCallback;
typedef DeviceQueueStats TNYQueueStats;

// Map priority levels
#define TNY_PRIORITY_HIGH    DEVICE_PRIORITY_HIGH
#define TNY_PRIORITY_NORMAL  DEVICE_PRIORITY_NORMAL
#define TNY_PRIORITY_LOW     DEVICE_PRIORITY_LOW

// Map transaction constants
#define TNY_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define TNY_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Command types
typedef enum {
    TNY_CMD_NONE = 0,
    
    // Pin control commands
    TNY_CMD_SET_PIN,
    TNY_CMD_SET_MULTIPLE_PINS,
    
    // Test command
    TNY_CMD_TEST_CONNECTION,
    
    TNY_CMD_TYPE_COUNT
} TNYCommandType;

// Command parameters union
typedef union {
    struct { 
        int pin; 
        int state; 
    } setPin;
    
    struct { 
        int *pins;
        int *states;
        int count;
    } setMultiplePins;
    
} TNYCommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        int testResult;  // For test connection
    } data;
} TNYCommandResult;

// Pin state structure for batch operations
typedef struct {
    int pin;
    int state;
} TNYPinState;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

/**
 * Initialize the queue manager with specific connection parameters
 * @param comPort - COM port number (1-16)
 * @param baudRate - Baud rate (default: 9600)
 * @return Queue manager instance or NULL on failure
 */
TNYQueueManager* TNY_QueueInit(int comPort, int baudRate);

/**
 * Get the Teensy handle from the queue manager
 * @param mgr - Queue manager instance
 * @return Teensy handle or NULL if not connected
 */
TNY_Handle* TNY_QueueGetHandle(TNYQueueManager *mgr);

// Shutdown the queue manager
void TNY_QueueShutdown(TNYQueueManager *mgr);

// Check if queue manager is running
bool TNY_QueueIsRunning(TNYQueueManager *mgr);

// Get queue statistics
void TNY_QueueGetStats(TNYQueueManager *mgr, TNYQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Queue a command (blocking)
int TNY_QueueCommandBlocking(TNYQueueManager *mgr, TNYCommandType type,
                           TNYCommandParams *params, TNYPriority priority,
                           TNYCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
CommandID TNY_QueueCommandAsync(TNYQueueManager *mgr, TNYCommandType type,
                              TNYCommandParams *params, TNYPriority priority,
                              TNYCommandCallback callback, void *userData);

// Cancel commands
int TNY_QueueCancelCommand(TNYQueueManager *mgr, CommandID cmdId);
int TNY_QueueCancelByType(TNYQueueManager *mgr, TNYCommandType type);
int TNY_QueueCancelByAge(TNYQueueManager *mgr, double ageSeconds);
int TNY_QueueCancelAll(TNYQueueManager *mgr);

// Check if a command type is already queued
bool TNY_QueueHasCommandType(TNYQueueManager *mgr, TNYCommandType type);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
TransactionHandle TNY_QueueBeginTransaction(TNYQueueManager *mgr);

// Add command to transaction
int TNY_QueueAddToTransaction(TNYQueueManager *mgr, TransactionHandle txn,
                            TNYCommandType type, TNYCommandParams *params);

// Commit transaction (async)
int TNY_QueueCommitTransaction(TNYQueueManager *mgr, TransactionHandle txn,
                             TNYTransactionCallback callback, void *userData);

// Cancel transaction
int TNY_QueueCancelTransaction(TNYQueueManager *mgr, TransactionHandle txn);

/******************************************************************************
 * Wrapper Functions (Direct replacements for existing Teensy functions)
 ******************************************************************************/

// Pin control functions
int TNY_SetPinQueued(TNY_Handle *handle, int pin, int state);
int TNY_SetMultiplePinsQueued(TNY_Handle *handle, const int *pins, const int *states, int count);

// Test function
int TNY_TestConnectionQueued(TNY_Handle *handle);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* TNY_QueueGetCommandTypeName(TNYCommandType type);

// Get delay for command type
int TNY_QueueGetCommandDelay(TNYCommandType type);

// Set/Get global queue manager
void TNY_SetGlobalQueueManager(TNYQueueManager *mgr);
TNYQueueManager* TNY_GetGlobalQueueManager(void);

/******************************************************************************
 * Advanced Functions
 ******************************************************************************/

/**
 * Set multiple pins atomically using a transaction
 * All pins are set in sequence without interruption
 * 
 * @param handle - Teensy handle (can be NULL to use global queue manager)
 * @param pinStates - Array of pin/state pairs
 * @param count - Number of pins to set
 * @param callback - Optional callback for transaction completion
 * @param userData - User data for callback
 * @return SUCCESS or error code
 */
int TNY_SetPinsAtomic(TNY_Handle *handle, const TNYPinState *pinStates, int count,
                     TNYTransactionCallback callback, void *userData);

/**
 * Initialize pins to a known state using a transaction
 * 
 * @param handle - Teensy handle (can be NULL to use global queue manager)
 * @param lowPins - Array of pins to set LOW
 * @param lowCount - Number of pins to set LOW
 * @param highPins - Array of pins to set HIGH
 * @param highCount - Number of pins to set HIGH
 * @return SUCCESS or error code
 */
int TNY_InitializePins(TNY_Handle *handle, 
                      const int *lowPins, int lowCount,
                      const int *highPins, int highCount);

#endif // TEENSY_QUEUE_H