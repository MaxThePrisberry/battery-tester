/******************************************************************************
 * device_queue.h
 * 
 * Generic thread-safe command queue implementation for device control
 * Provides priority-based command queuing with blocking/async operation support
 * and guaranteed sequential transaction execution
 ******************************************************************************/

#ifndef DEVICE_QUEUE_H
#define DEVICE_QUEUE_H

#include "common.h"
#include "logging.h"
#include <utility.h>

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Queue depths
#define DEVICE_QUEUE_HIGH_PRIORITY_SIZE    50
#define DEVICE_QUEUE_NORMAL_PRIORITY_SIZE  20
#define DEVICE_QUEUE_LOW_PRIORITY_SIZE     10

// Default timeouts
#define DEVICE_QUEUE_COMMAND_TIMEOUT_MS    30000
#define DEVICE_QUEUE_RECONNECT_DELAY_MS    1000
#define DEVICE_QUEUE_MAX_RECONNECT_DELAY   30000

// Transaction limits
#define DEVICE_MAX_TRANSACTION_COMMANDS    20
#define DEVICE_DEFAULT_TRANSACTION_TIMEOUT_MS  60000

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Forward declarations
typedef struct DeviceQueueManager DeviceQueueManager;
typedef struct DeviceQueuedCommand DeviceQueuedCommand;
typedef uint32_t DeviceTransactionHandle;
typedef uint32_t DeviceCommandID;

// Priority levels
typedef enum {
    DEVICE_PRIORITY_HIGH = 0,    // User-initiated commands
    DEVICE_PRIORITY_NORMAL = 1,  // Status queries
    DEVICE_PRIORITY_LOW = 2      // Background tasks
} DevicePriority;

// Transaction behavior flags
typedef enum {
    DEVICE_TXN_CONTINUE_ON_ERROR = 0x00,  // Continue executing commands even if one fails
    DEVICE_TXN_ABORT_ON_ERROR    = 0x01,  // Stop transaction if any command fails
} DeviceTransactionFlags;

// Individual transaction command result
typedef struct {
    int commandType;
    int errorCode;
    void *result;  // Command-specific result data (caller should not free)
} TransactionCommandResult;

// Generic command callback
typedef void (*DeviceCommandCallback)(DeviceCommandID cmdId, int commandType, 
                                    void *result, void *userData);

// Enhanced transaction callback with detailed results
typedef void (*DeviceTransactionCallback)(DeviceTransactionHandle txn, 
                                        int successCount, int failureCount,
                                        TransactionCommandResult *results,
                                        int resultCount,
                                        void *userData);

/******************************************************************************
 * Device Adapter Interface
 ******************************************************************************/

typedef struct DeviceAdapter {
    // Device identification
    const char *deviceName;
    
    // Connection management
    int (*connect)(void *deviceContext, void *connectionParams);
    int (*disconnect)(void *deviceContext);
    int (*testConnection)(void *deviceContext);
    bool (*isConnected)(void *deviceContext);
    
    // Command execution
    int (*executeCommand)(void *deviceContext, int commandType, void *params, void *result);
    
    // Command management
    void* (*createCommandParams)(int commandType, void *sourceParams);
    void (*freeCommandParams)(int commandType, void *params);
    void* (*createCommandResult)(int commandType);
    void (*freeCommandResult)(int commandType, void *result);
    void (*copyCommandResult)(int commandType, void *dest, void *src);
    
    // Utility functions
    const char* (*getCommandTypeName)(int commandType);
    int (*getCommandDelay)(int commandType);
    const char* (*getErrorString)(int errorCode);
    
    // Optional: Raw command support
    bool (*supportsRawCommands)(void);
    int (*executeRawCommand)(void *deviceContext, void *rawParams, void *rawResult);
} DeviceAdapter;

/******************************************************************************
 * Queue Statistics
 ******************************************************************************/

typedef struct {
    int highPriorityQueued;
    int normalPriorityQueued;
    int lowPriorityQueued;
    int totalProcessed;
    int totalErrors;
    int reconnectAttempts;
    int isConnected;
    int isProcessing;
    int activeTransactionId;     // Non-zero if transaction is executing
    int isInTransactionMode;     // 1 if processing thread is in transaction mode
} DeviceQueueStats;

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

// Create a queue manager for a device
DeviceQueueManager* DeviceQueue_Create(const DeviceAdapter *adapter, 
                                      void *deviceContext,
                                      void *connectionParams,
									  CmtThreadPoolHandle threadPool);

// Destroy the queue manager
void DeviceQueue_Destroy(DeviceQueueManager *mgr);

// Get the device context
void* DeviceQueue_GetDeviceContext(DeviceQueueManager *mgr);

// Check if queue manager is running
bool DeviceQueue_IsRunning(DeviceQueueManager *mgr);

// Get queue statistics
void DeviceQueue_GetStats(DeviceQueueManager *mgr, DeviceQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Queue a command (blocking)
int DeviceQueue_CommandBlocking(DeviceQueueManager *mgr, int commandType,
                              void *params, DevicePriority priority,
                              void *result, int timeoutMs);

// Queue a command (async with callback)
DeviceCommandID DeviceQueue_CommandAsync(DeviceQueueManager *mgr, int commandType,
                                       void *params, DevicePriority priority,
                                       DeviceCommandCallback callback, void *userData);

// Cancel commands
int DeviceQueue_CancelCommand(DeviceQueueManager *mgr, DeviceCommandID cmdId);
int DeviceQueue_CancelByType(DeviceQueueManager *mgr, int commandType);
int DeviceQueue_CancelByAge(DeviceQueueManager *mgr, double seconds);
int DeviceQueue_CancelAll(DeviceQueueManager *mgr);

// Check if a command type is already queued
bool DeviceQueue_HasCommandType(DeviceQueueManager *mgr, int commandType);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
DeviceTransactionHandle DeviceQueue_BeginTransaction(DeviceQueueManager *mgr);

// Configure transaction behavior
int DeviceQueue_SetTransactionFlags(DeviceQueueManager *mgr, 
                                   DeviceTransactionHandle txn,
                                   DeviceTransactionFlags flags);

// Set transaction priority (default is HIGH)
int DeviceQueue_SetTransactionPriority(DeviceQueueManager *mgr,
                                     DeviceTransactionHandle txn,
                                     DevicePriority priority);

// Set transaction timeout (default is DEVICE_DEFAULT_TRANSACTION_TIMEOUT_MS)
int DeviceQueue_SetTransactionTimeout(DeviceQueueManager *mgr,
                                    DeviceTransactionHandle txn,
                                    int timeoutMs);

// Add command to transaction
int DeviceQueue_AddToTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txn,
                               int commandType, void *params);

// Commit transaction (async) - guarantees sequential execution
int DeviceQueue_CommitTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txn,
                                DeviceTransactionCallback callback, void *userData);

// Cancel transaction (only works if not yet executing)
int DeviceQueue_CancelTransaction(DeviceQueueManager *mgr, DeviceTransactionHandle txn);

// Check if currently in a transaction
bool DeviceQueue_IsInTransaction(DeviceQueueManager *mgr);

/******************************************************************************
 * Logging Support
 ******************************************************************************/

// Set the logging device type for a queue manager
void DeviceQueue_SetLogDevice(DeviceQueueManager *mgr, LogDevice device);

#endif // DEVICE_QUEUE_H