/******************************************************************************
 * biologic_queue.h
 * 
 * Thread-safe command queue implementation for BioLogic SP-150e
 * Provides priority-based command queuing with blocking/async operation support
 ******************************************************************************/

#ifndef BIOLOGIC_QUEUE_H
#define BIOLOGIC_QUEUE_H

#include "common.h"
#include "biologic_dll.h"
#include <utility.h>

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Device address
#define BIOLOGIC_DEFAULT_ADDRESS USB0

// Queue depths
#define BIO_QUEUE_HIGH_PRIORITY_SIZE    50
#define BIO_QUEUE_NORMAL_PRIORITY_SIZE  20
#define BIO_QUEUE_LOW_PRIORITY_SIZE     10

// Command delays (milliseconds)
#define BIO_DELAY_AFTER_CONNECT         500   // After connection
#define BIO_DELAY_AFTER_LOAD_TECHNIQUE  200   // After loading technique
#define BIO_DELAY_AFTER_START           200   // After starting channel
#define BIO_DELAY_AFTER_STOP            200   // After stopping channel
#define BIO_DELAY_AFTER_PARAMETER       100   // After parameter update
#define BIO_DELAY_AFTER_DATA_READ       50    // After reading data
#define BIO_DELAY_RECOVERY              50    // General recovery between commands

// Reconnection parameters
#define BIO_QUEUE_RECONNECT_DELAY_MS    1000  // Initial reconnection delay
#define BIO_QUEUE_MAX_RECONNECT_DELAY   30000 // Max reconnection delay
#define BIO_QUEUE_COMMAND_TIMEOUT_MS    30000 // Timeout for queued commands

// Transaction limits
#define BIO_MAX_TRANSACTION_COMMANDS    20    // Max commands per transaction

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Forward declarations
typedef struct BioQueueManager BioQueueManager;
typedef struct BioQueuedCommand BioQueuedCommand;
typedef uint32_t BioTransactionHandle;
typedef uint32_t BioCommandID;

// Command types
typedef enum {
    BIO_CMD_NONE = 0,
    
    // Connection commands
    BIO_CMD_CONNECT,
    BIO_CMD_DISCONNECT,
    BIO_CMD_TEST_CONNECTION,
    
    // Channel commands
    BIO_CMD_START_CHANNEL,
    BIO_CMD_STOP_CHANNEL,
    BIO_CMD_GET_CHANNEL_INFO,
    
    // Technique commands
    BIO_CMD_LOAD_TECHNIQUE,
    BIO_CMD_UPDATE_PARAMETERS,
    
    // Data commands
    BIO_CMD_GET_CURRENT_VALUES,
    BIO_CMD_GET_DATA,
    
    // Configuration commands
    BIO_CMD_SET_HARDWARE_CONFIG,
    BIO_CMD_GET_HARDWARE_CONFIG,
    
    BIO_CMD_TYPE_COUNT
} BioCommandType;

// Priority levels
typedef enum {
    BIO_PRIORITY_HIGH = 0,    // User-initiated commands
    BIO_PRIORITY_NORMAL = 1,  // Status queries
    BIO_PRIORITY_LOW = 2      // Background tasks
} BioPriority;

// Command parameters union
typedef union {
    struct { 
        char address[64]; 
        uint8_t timeout; 
    } connect;
    
    struct { 
        uint8_t channel; 
    } channel;
    
    struct {
        uint8_t channel;
        char techniquePath[MAX_PATH_LENGTH];
        TEccParams_t params;
        bool firstTechnique;
        bool lastTechnique;
        bool displayParams;
    } loadTechnique;
    
    struct {
        uint8_t channel;
        int techniqueIndex;
        TEccParams_t params;
        char eccFileName[MAX_PATH_LENGTH];
    } updateParams;
    
    struct {
        uint8_t channel;
        THardwareConf_t config;
    } hardwareConfig;
    
} BioCommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        TDeviceInfos_t deviceInfo;
        TChannelInfos_t channelInfo;
        TCurrentValues_t currentValues;
        THardwareConf_t hardwareConfig;
        struct {
            TDataBuffer_t buffer;
            TDataInfos_t info;
        } data;
    } data;
} BioCommandResult;

// Command callback
typedef void (*BioCommandCallback)(BioCommandID cmdId, BioCommandType type, 
                                  BioCommandResult *result, void *userData);

// Transaction callback
typedef void (*BioTransactionCallback)(BioTransactionHandle txn, 
                                      int successCount, int failureCount,
                                      BioCommandResult *results, void *userData);

/******************************************************************************
 * Queue Manager Functions
 ******************************************************************************/

// Initialize the queue manager for a BioLogic device
BioQueueManager* BIO_QueueInit(const char *address);

// Shutdown the queue manager
void BIO_QueueShutdown(BioQueueManager *mgr);

// Check if queue manager is running
bool BIO_QueueIsRunning(BioQueueManager *mgr);

// Get queue statistics
typedef struct {
    int highPriorityQueued;
    int normalPriorityQueued;
    int lowPriorityQueued;
    int totalProcessed;
    int totalErrors;
    int reconnectAttempts;
    bool isConnected;
    bool isProcessing;
} BioQueueStats;

void BIO_QueueGetStats(BioQueueManager *mgr, BioQueueStats *stats);

/******************************************************************************
 * Command Queueing Functions
 ******************************************************************************/

// Queue a command (blocking)
int BIO_QueueCommandBlocking(BioQueueManager *mgr, BioCommandType type,
                           BioCommandParams *params, BioPriority priority,
                           BioCommandResult *result, int timeoutMs);

// Queue a command (async with callback)
BioCommandID BIO_QueueCommandAsync(BioQueueManager *mgr, BioCommandType type,
                                 BioCommandParams *params, BioPriority priority,
                                 BioCommandCallback callback, void *userData);

// Cancel commands
int BIO_QueueCancelCommand(BioQueueManager *mgr, BioCommandID cmdId);
int BIO_QueueCancelByType(BioQueueManager *mgr, BioCommandType type);
int BIO_QueueCancelByAge(BioQueueManager *mgr, double ageSeconds);
int BIO_QueueCancelAll(BioQueueManager *mgr);

// Check if a command type is already queued
bool BIO_QueueHasCommandType(BioQueueManager *mgr, BioCommandType type);

/******************************************************************************
 * Transaction Functions
 ******************************************************************************/

// Begin a transaction
BioTransactionHandle BIO_QueueBeginTransaction(BioQueueManager *mgr);

// Add command to transaction
int BIO_QueueAddToTransaction(BioQueueManager *mgr, BioTransactionHandle txn,
                            BioCommandType type, BioCommandParams *params);

// Commit transaction (async)
int BIO_QueueCommitTransaction(BioQueueManager *mgr, BioTransactionHandle txn,
                             BioTransactionCallback callback, void *userData);

// Cancel transaction
int BIO_QueueCancelTransaction(BioQueueManager *mgr, BioTransactionHandle txn);

/******************************************************************************
 * Wrapper Functions (Direct replacements for existing BioLogic functions)
 ******************************************************************************/

// Connection wrappers
int BL_ConnectQueued(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos);
int BL_DisconnectQueued(int ID);
int BL_TestConnectionQueued(int ID);

// Channel control wrappers
int BL_StartChannelQueued(int ID, uint8_t channel);
int BL_StopChannelQueued(int ID, uint8_t channel);
int BL_GetChannelInfosQueued(int ID, uint8_t ch, TChannelInfos_t* pInfos);

// Technique wrappers
int BL_LoadTechniqueQueued(int ID, uint8_t channel, const char* pFName, 
                         TEccParams_t Params, bool FirstTechnique, 
                         bool LastTechnique, bool DisplayParams);
int BL_UpdateParametersQueued(int ID, uint8_t channel, int TechIndx, 
                            TEccParams_t Params, const char* EccFileName);

// Data acquisition wrappers
int BL_GetCurrentValuesQueued(int ID, uint8_t channel, TCurrentValues_t* pValues);
int BL_GetDataQueued(int ID, uint8_t channel, TDataBuffer_t* pBuf, 
                   TDataInfos_t* pInfos, TCurrentValues_t* pValues);

// Configuration wrappers
int BL_GetHardConfQueued(int ID, uint8_t ch, THardwareConf_t* pHardConf);
int BL_SetHardConfQueued(int ID, uint8_t ch, THardwareConf_t HardConf);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

// Get command type name for logging
const char* BIO_QueueGetCommandTypeName(BioCommandType type);

// Get delay for command type
int BIO_QueueGetCommandDelay(BioCommandType type);

// Set global queue manager (used by wrapper functions)
void BIO_SetGlobalQueueManager(BioQueueManager *mgr);

#endif // BIOLOGIC_QUEUE_H