/******************************************************************************
 * biologic_queue.h
 * 
 * Thread-safe command queue implementation for BioLogic SP-150e
 * Updated to use high-level technique functions with state machine approach
 ******************************************************************************/

#ifndef BIOLOGIC_QUEUE_H
#define BIOLOGIC_QUEUE_H

#include "common.h"
#include "biologic_dll.h"
#include "device_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Device address
#define BIOLOGIC_DEFAULT_ADDRESS "USB0"

// Command delays (milliseconds)
#define BIO_DELAY_AFTER_CONNECT         500   // After connection
#define BIO_DELAY_AFTER_TECHNIQUE       200   // After technique completion
#define BIO_DELAY_AFTER_CONFIG          100   // After configuration change
#define BIO_DELAY_RECOVERY              50    // General recovery between commands

// Default technique timeouts (milliseconds)
#define BIO_DEFAULT_OCV_TIMEOUT_MS      300000  // 5 minutes default
#define BIO_DEFAULT_PEIS_TIMEOUT_MS     600000  // 10 minutes default

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Use generic types from device_queue.h
typedef DeviceQueueManager BioQueueManager;
typedef DeviceTransactionHandle BioTransactionHandle;
typedef DeviceCommandID BioCommandID;
typedef DevicePriority BioPriority;
typedef DeviceCommandCallback BioCommandCallback;
typedef DeviceTransactionCallback BioTransactionCallback;
typedef DeviceQueueStats BioQueueStats;

// Map priority levels
#define BIO_PRIORITY_HIGH    DEVICE_PRIORITY_HIGH
#define BIO_PRIORITY_NORMAL  DEVICE_PRIORITY_NORMAL
#define BIO_PRIORITY_LOW     DEVICE_PRIORITY_LOW

// Map transaction constants
#define BIO_MAX_TRANSACTION_COMMANDS  DEVICE_MAX_TRANSACTION_COMMANDS
#define BIO_QUEUE_COMMAND_TIMEOUT_MS  DEVICE_QUEUE_COMMAND_TIMEOUT_MS

// Error codes specific to partial data
#define BL_ERR_PARTIAL_DATA        -500  // Technique stopped with error but partial data available

// Progress callback for techniques
typedef void (*BioTechniqueProgressCallback)(double elapsedTime, int memFilled, void *userData);

// Command types - only high-level commands
typedef enum {
    BIO_CMD_NONE = 0,
    
    // Connection commands
    BIO_CMD_CONNECT,
    BIO_CMD_DISCONNECT,
    BIO_CMD_TEST_CONNECTION,
    
    // High-level technique commands
    BIO_CMD_RUN_OCV,
    BIO_CMD_RUN_PEIS,
    
    // Configuration commands
    BIO_CMD_SET_HARDWARE_CONFIG,
    BIO_CMD_GET_HARDWARE_CONFIG,
    
    BIO_CMD_TYPE_COUNT
} BioCommandType;

// Command parameters union
typedef union {
    // Connection parameters
    struct { 
        char address[64]; 
        uint8_t timeout; 
    } connect;
    
    // OCV parameters
    struct {
        uint8_t channel;
        double duration_s;
        double sample_interval_s;
        double record_every_dE;     // mV
        double record_every_dT;     // seconds
        int e_range;                // 0=2.5V, 1=5V, 2=10V, 3=Auto
        int timeout_ms;             // Command timeout
        BioTechniqueProgressCallback progressCallback;
        void *userData;
    } runOCV;
    
    // PEIS parameters
    struct {
        uint8_t channel;
        double e_dc;                // DC potential (V)
        double amplitude;           // AC amplitude (V)
        double initial_freq;        // Start frequency (Hz)
        double final_freq;          // End frequency (Hz)
        int points_per_decade;
        double i_range;             // Current range
        double e_range;             // Voltage range
        double bandwidth;           // Bandwidth setting
        int timeout_ms;             // Command timeout
        BioTechniqueProgressCallback progressCallback;
        void *userData;
    } runPEIS;
    
    // Hardware configuration
    struct {
        uint8_t channel;
        THardwareConf_t config;
    } hardwareConfig;
    
    struct {
        uint8_t channel;
    } channel;
    
} BioCommandParams;

// Command result structure
typedef struct {
    int errorCode;
    union {
        TDeviceInfos_t deviceInfo;
        THardwareConf_t hardwareConfig;
        
        // Technique results
        struct {
            BL_RawDataBuffer *rawData;  // Caller must free rawData->rawData and rawData
            double elapsedTime;         // Total measurement time
            int finalState;             // Final state of technique
            bool partialData;           // True if data is partial due to error
        } techniqueResult;
    } data;
} BioCommandResult;

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
 * High-Level Technique Functions (Blocking)
 ******************************************************************************/

// OCV measurement (blocking)
int BL_RunOCVQueued(int ID, uint8_t channel,
                    double duration_s,
                    double sample_interval_s,
                    double record_every_dE,
                    double record_every_dT,
                    int e_range,
                    BL_RawDataBuffer **data,
                    int timeout_ms,
                    BioTechniqueProgressCallback progressCallback,
                    void *userData);

// PEIS measurement (blocking)
int BL_RunPEISQueued(int ID, uint8_t channel,
                     double e_dc,
                     double amplitude,
                     double initial_freq,
                     double final_freq,
                     int points_per_decade,
                     double i_range,
                     double e_range,
                     double bandwidth,
                     BL_RawDataBuffer **data,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData);

/******************************************************************************
 * High-Level Technique Functions (Async)
 ******************************************************************************/

// OCV measurement (async)
BioCommandID BL_RunOCVAsync(int ID, uint8_t channel,
                            double duration_s,
                            double sample_interval_s,
                            double record_every_dE,
                            double record_every_dT,
                            int e_range,
                            BioCommandCallback callback,
                            void *userData);

// PEIS measurement (async)
BioCommandID BL_RunPEISAsync(int ID, uint8_t channel,
                             double e_dc,
                             double amplitude,
                             double initial_freq,
                             double final_freq,
                             int points_per_decade,
                             double i_range,
                             double e_range,
                             double bandwidth,
                             BioCommandCallback callback,
                             void *userData);

/******************************************************************************
 * Connection and Configuration Functions
 ******************************************************************************/

// Connection wrappers
int BL_ConnectQueued(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos);
int BL_DisconnectQueued(int ID);
int BL_TestConnectionQueued(int ID);

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

// Set/Get global queue manager
void BIO_SetGlobalQueueManager(BioQueueManager *mgr);
BioQueueManager* BIO_GetGlobalQueueManager(void);

// Get device ID from queue manager
int BIO_QueueGetDeviceID(BioQueueManager *mgr);

// Free technique result data
void BL_FreeTechniqueResult(BL_RawDataBuffer *data);

#endif // BIOLOGIC_QUEUE_H