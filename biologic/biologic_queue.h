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
#define BIO_ERR_PARTIAL_DATA        -500  // Technique stopped with error but partial data available

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
	BIO_CMD_RUN_SPEIS,
	BIO_CMD_RUN_GEIS,
	BIO_CMD_RUN_SGEIS,
    
    // Configuration commands
    BIO_CMD_SET_HARDWARE_CONFIG,
    BIO_CMD_GET_HARDWARE_CONFIG,
	
    BIO_CMD_STOP_CHANNEL,
    BIO_CMD_GET_CURRENT_VALUES,
    BIO_CMD_GET_CHANNEL_INFOS,
    BIO_CMD_IS_CHANNEL_PLUGGED,
    BIO_CMD_GET_CHANNELS_PLUGGED,
    BIO_CMD_START_CHANNEL,
    BIO_CMD_GET_DATA,
    BIO_CMD_GET_EXPERIMENT_INFOS,
    BIO_CMD_SET_EXPERIMENT_INFOS,
    BIO_CMD_LOAD_FIRMWARE,
    BIO_CMD_GET_LIB_VERSION,
    BIO_CMD_GET_MESSAGE,
    
    BIO_CMD_TYPE_COUNT
} BioCommandType;

// Base command structure
typedef struct {
    BioCommandType type;
    uint8_t channel;
    int timeout_ms;
    BioTechniqueProgressCallback progressCallback;
    void *userData;
} BioCommandParams;

// Connection command
typedef struct {
    BioCommandParams base;
    char address[64];
    uint8_t timeout;
} BioConnectCommand;

// OCV command
typedef struct {
    BioCommandParams base;
    double duration_s;
    double sample_interval_s;
    double record_every_dE;     // mV
    double record_every_dT;     // seconds
    int e_range;                // 0=2.5V, 1=5V, 2=10V, 3=Auto
	bool processData;
} BioOCVCommand;

// PEIS command
typedef struct {
    BioCommandParams base;
    bool vs_initial;               // Voltage step vs initial
    double initial_voltage_step;   // Initial voltage step (V)
    double duration_step;          // Step duration (s)
    double record_every_dT;        // Record every dt (s)
    double record_every_dI;        // Record every dI (A)
    double initial_freq;           // Initial frequency (Hz)
    double final_freq;             // Final frequency (Hz)
    bool sweep_linear;             // TRUE for linear, FALSE for logarithmic
    double amplitude_voltage;      // Sine amplitude (V)
    int frequency_number;          // Number of frequencies
    int average_n_times;           // Number of repeat times
    bool correction;               // Non-stationary correction
    double wait_for_steady;        // Number of periods to wait
	bool processData;
} BioPEISCommand;

// SPEIS command
typedef struct {
    BioCommandParams base;
    bool vs_initial;               // Voltage step vs initial
    bool vs_final;                 // Voltage step vs final
    double initial_voltage_step;   // Initial voltage step (V)
    double final_voltage_step;     // Final voltage step (V)
    double duration_step;          // Step duration (s)
    int step_number;               // Number of voltage steps [0..98]
    double record_every_dT;        // Record every dt (s)
    double record_every_dI;        // Record every dI (A)
    double initial_freq;           // Initial frequency (Hz)
    double final_freq;             // Final frequency (Hz)
    bool sweep_linear;             // TRUE for linear, FALSE for logarithmic
    double amplitude_voltage;      // Sine amplitude (V)
    int frequency_number;          // Number of frequencies
    int average_n_times;           // Number of repeat times
    bool correction;               // Non-stationary correction
    double wait_for_steady;        // Number of periods to wait
	bool processData;
} BioSPEISCommand;

// GEIS command
typedef struct {
    BioCommandParams base;
    bool vs_initial;               // Current step vs initial
    double initial_current_step;   // Initial current step (A)
    double duration_step;          // Step duration (s)
    double record_every_dT;        // Record every dt (s)
    double record_every_dE;        // Record every dE (V)
    double initial_freq;           // Initial frequency (Hz)
    double final_freq;             // Final frequency (Hz)
    bool sweep_linear;             // TRUE for linear, FALSE for logarithmic
    double amplitude_current;      // Sine amplitude (A)
    int frequency_number;          // Number of frequencies
    int average_n_times;           // Number of repeat times
    bool correction;               // Non-stationary correction
    double wait_for_steady;        // Number of periods to wait
    int i_range;                   // Current range (cannot be auto)
	bool processData;
} BioGEISCommand;

// SGEIS command
typedef struct {
    BioCommandParams base;
    bool vs_initial;               // Current step vs initial
    bool vs_final;                 // Current step vs final
    double initial_current_step;   // Initial current step (A)
    double final_current_step;     // Final current step (A)
    double duration_step;          // Step duration (s)
    int step_number;               // Number of current steps [0..98]
    double record_every_dT;        // Record every dt (s)
    double record_every_dE;        // Record every dE (V)
    double initial_freq;           // Initial frequency (Hz)
    double final_freq;             // Final frequency (Hz)
    bool sweep_linear;             // TRUE for linear, FALSE for logarithmic
    double amplitude_current;      // Sine amplitude (A)
    int frequency_number;          // Number of frequencies
    int average_n_times;           // Number of repeat times
    bool correction;               // Non-stationary correction
    double wait_for_steady;        // Number of periods to wait
    int i_range;                   // Current range (cannot be auto)
	bool processData;
} BioSGEISCommand;

// Get channels plugged command
typedef struct {
    BioCommandParams base;
    uint8_t maxChannels;  // Size of array to fill
} BioGetChannelsPluggedCommand;

// Set experiment info command
typedef struct {
    BioCommandParams base;
    TExperimentInfos_t expInfo;
} BioSetExperimentInfosCommand;

// Load firmware command
typedef struct {
    BioCommandParams base;
    uint8_t channels[16];
    uint8_t numChannels;
    bool showGauge;
    bool forceReload;
    char binFile[MAX_PATH_LENGTH];
    char xlxFile[MAX_PATH_LENGTH];
} BioLoadFirmwareCommand;

// Get message command
typedef struct {
    BioCommandParams base;
    unsigned int maxSize;
} BioGetMessageCommand;

// Hardware config command
typedef struct {
    BioCommandParams base;
    THardwareConf_t config;
} BioHardwareConfigCommand;

// Simple channel command
typedef struct {
    BioCommandParams base;
} BioChannelCommand;

// Command result structure
typedef struct {
    int errorCode;
    union {
        TDeviceInfos_t deviceInfo;
        THardwareConf_t hardwareConfig;
        
        // Technique results
        struct {
            BIO_TechniqueData *techniqueData;      // Combined raw and converted data
            double elapsedTime;                   // Total measurement time
            int finalState;                       // Final state of technique
            bool partialData;                     // True if data is partial due to error
        } techniqueResult;
		
        TCurrentValues_t currentValues;
        TChannelInfos_t channelInfos;
        bool isPlugged;
        uint8_t channelsPlugged[16];
        struct {
            TDataInfos_t dataInfo;
            TCurrentValues_t currentValues;
            unsigned int *rawData;  // Must be freed
        } dataResult;
        TExperimentInfos_t experimentInfos;
        int firmwareResults[16];
        char version[256];
        char *message;  // Must be freed
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
 * 
 * These functions will use the queue if initialized, otherwise return
 * ERR_QUEUE_NOT_INIT
 ******************************************************************************/

// OCV measurement (blocking)
int BIO_RunOCVQueued(int ID, uint8_t channel,
                    double duration_s,
                    double sample_interval_s,
                    double record_every_dE,
                    double record_every_dT,
                    int e_range,
					bool processData,
                    BIO_TechniqueData **result,
                    int timeout_ms,
                    BioTechniqueProgressCallback progressCallback,
                    void *userData);

// PEIS measurement (blocking)
int BIO_RunPEISQueued(int ID, uint8_t channel,
                     bool vs_initial,
                     double initial_voltage_step,
                     double duration_step,
                     double record_every_dT,
                     double record_every_dI,
                     double initial_freq,
                     double final_freq,
                     bool sweep_linear,
                     double amplitude_voltage,
                     int frequency_number,
                     int average_n_times,
                     bool correction,
                     double wait_for_steady,
					 bool processData,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData);

// SPEIS measurement (blocking)
int BIO_RunSPEISQueued(int ID, uint8_t channel,
                      bool vs_initial,
                      bool vs_final,
                      double initial_voltage_step,
                      double final_voltage_step,
                      double duration_step,
                      int step_number,
                      double record_every_dT,
                      double record_every_dI,
                      double initial_freq,
                      double final_freq,
                      bool sweep_linear,
                      double amplitude_voltage,
                      int frequency_number,
                      int average_n_times,
                      bool correction,
                      double wait_for_steady,
					  bool processData,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData);

// GEIS measurement (blocking)
int BIO_RunGEISQueued(int ID, uint8_t channel,
                     bool vs_initial,
                     double initial_current_step,
                     double duration_step,
                     double record_every_dT,
                     double record_every_dE,
                     double initial_freq,
                     double final_freq,
                     bool sweep_linear,
                     double amplitude_current,
                     int frequency_number,
                     int average_n_times,
                     bool correction,
                     double wait_for_steady,
                     int i_range,
					 bool processData,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData);

// SGEIS measurement (blocking)
int BIO_RunSGEISQueued(int ID, uint8_t channel,
                      bool vs_initial,
                      bool vs_final,
                      double initial_current_step,
                      double final_current_step,
                      double duration_step,
                      int step_number,
                      double record_every_dT,
                      double record_every_dE,
                      double initial_freq,
                      double final_freq,
                      bool sweep_linear,
                      double amplitude_current,
                      int frequency_number,
                      int average_n_times,
                      bool correction,
                      double wait_for_steady,
                      int i_range,
					  bool processData,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData);

/******************************************************************************
 * High-Level Technique Functions (Async)
 * 
 * These functions return ERR_QUEUE_NOT_INIT if the queue is not initialized
 ******************************************************************************/

// OCV measurement (async)
BioCommandID BIO_RunOCVAsync(int ID, uint8_t channel,
                            double duration_s,
                            double sample_interval_s,
                            double record_every_dE,
                            double record_every_dT,
                            int e_range,
							bool processData,
                            BioCommandCallback callback,
                            void *userData);

// PEIS measurement (async)
BioCommandID BIO_RunPEISAsync(int ID, uint8_t channel,
                             bool vs_initial,
                             double initial_voltage_step,
                             double duration_step,
                             double record_every_dT,
                             double record_every_dI,
                             double initial_freq,
                             double final_freq,
                             bool sweep_linear,
                             double amplitude_voltage,
                             int frequency_number,
                             int average_n_times,
                             bool correction,
                             double wait_for_steady,
							 bool processData,
                             BioCommandCallback callback,
                             void *userData);

// SPEIS measurement (async)
BioCommandID BIO_RunSPEISAsync(int ID, uint8_t channel,
                              bool vs_initial,
                              bool vs_final,
                              double initial_voltage_step,
                              double final_voltage_step,
                              double duration_step,
                              int step_number,
                              double record_every_dT,
                              double record_every_dI,
                              double initial_freq,
                              double final_freq,
                              bool sweep_linear,
                              double amplitude_voltage,
                              int frequency_number,
                              int average_n_times,
                              bool correction,
                              double wait_for_steady,
							  bool processData,
                              BioCommandCallback callback,
                              void *userData);

// GEIS measurement (async)
BioCommandID BIO_RunGEISAsync(int ID, uint8_t channel,
                             bool vs_initial,
                             double initial_current_step,
                             double duration_step,
                             double record_every_dT,
                             double record_every_dE,
                             double initial_freq,
                             double final_freq,
                             bool sweep_linear,
                             double amplitude_current,
                             int frequency_number,
                             int average_n_times,
                             bool correction,
                             double wait_for_steady,
                             int i_range,
							 bool processData,
                             BioCommandCallback callback,
                             void *userData);

// SGEIS measurement (async)
BioCommandID BIO_RunSGEISAsync(int ID, uint8_t channel,
                              bool vs_initial,
                              bool vs_final,
                              double initial_current_step,
                              double final_current_step,
                              double duration_step,
                              int step_number,
                              double record_every_dT,
                              double record_every_dE,
                              double initial_freq,
                              double final_freq,
                              bool sweep_linear,
                              double amplitude_current,
                              int frequency_number,
                              int average_n_times,
                              bool correction,
                              double wait_for_steady,
                              int i_range,
							  bool processData,
                              BioCommandCallback callback,
                              void *userData);

/******************************************************************************
 * Connection and Configuration Functions
 * 
 * These functions will use the queue if initialized, otherwise return
 * ERR_QUEUE_NOT_INIT
 ******************************************************************************/

// Connection wrappers
int BIO_ConnectQueued(const char* address, uint8_t timeout, int* pID, TDeviceInfos_t* pInfos);
int BIO_DisconnectQueued(int ID);
int BIO_TestConnectionQueued(int ID);

// Configuration wrappers
int BIO_GetHardConfQueued(int ID, uint8_t ch, THardwareConf_t* pHardConf);
int BIO_SetHardConfQueued(int ID, uint8_t ch, THardwareConf_t HardConf);

/******************************************************************************
 * General queued functions
 ******************************************************************************/

// Channel control
int BIO_StopChannelQueued(int ID, uint8_t channel);
int BIO_StartChannelQueued(int ID, uint8_t channel);

// Channel status
int BIO_GetCurrentValuesQueued(int ID, uint8_t channel, TCurrentValues_t* pValues);
int BIO_GetChannelInfosQueued(int ID, uint8_t channel, TChannelInfos_t* pInfos);
bool BIO_IsChannelPluggedQueued(int ID, uint8_t channel);
int BIO_GetChannelsPluggedQueued(int ID, uint8_t* pChPlugged, uint8_t Size);

// Data retrieval
int BIO_GetDataQueued(int ID, uint8_t channel, TDataBuffer_t* pBuf, TDataInfos_t* pInfos, TCurrentValues_t* pValues);

// Experiment info
int BIO_GetExperimentInfosQueued(int ID, uint8_t channel, TExperimentInfos_t* pExpInfos);
int BIO_SetExperimentInfosQueued(int ID, uint8_t channel, TExperimentInfos_t ExpInfos);

// System functions
int BIO_LoadFirmwareQueued(int ID, uint8_t* pChannels, int* pResults, uint8_t Length, 
                          bool ShowGauge, bool ForceReload, const char* BinFile, const char* XlxFile);
int BIO_GetLibVersionQueued(char* pVersion, unsigned int* psize);
int BIO_GetMessageQueued(int ID, uint8_t channel, char* msg, unsigned int* size);

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

#endif // BIOLOGIC_QUEUE_H