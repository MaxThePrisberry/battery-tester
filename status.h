/******************************************************************************
 * status.h
 * 
 * Device Status Monitoring Module Header
 * Manages connection status and periodic updates for PSB and BioLogic devices
 ******************************************************************************/

#ifndef STATUS_H
#define STATUS_H

#include "common.h"
#include "psb10000_dll.h"  // For PSB_Handle type
#include "biologic_dll.h"  // For BioLogic types
#include <stdint.h>        // For int32_t type

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Device enable flags - set to 1 to enable monitoring, 0 to disable
#define STATUS_MONITOR_PSB         1    // Enable PSB 10000 monitoring
#define STATUS_MONITOR_BIOLOGIC    1    // Enable BioLogic SP-150e monitoring

// Update rates
#define STATUS_UPDATE_RATE_HZ      5    // Status update frequency in Hz
#define STATUS_UPDATE_PERIOD_MS    (1000 / STATUS_UPDATE_RATE_HZ)  // 200ms

// Connection retry parameters
#define RECONNECT_BASE_DELAY_MS    1000     // Initial reconnection delay (1 second)
#define RECONNECT_MAX_DELAY_MS     60000    // Maximum reconnection delay (60 seconds)
#define RECONNECT_MULTIPLIER       2        // Exponential backoff multiplier

// PSB configuration (from main file)
#define PSB_TARGET_SERIAL          "2872380001"

// BioLogic configuration
#define BIOLOGIC_DEFAULT_ADDRESS   "USB0"

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Connection state for each device
typedef enum {
    CONN_STATE_IDLE = 0,          // Not started
    CONN_STATE_DISCOVERING,       // Searching for device
    CONN_STATE_CONNECTING,        // Found, connecting
    CONN_STATE_CONNECTED,         // Successfully connected
    CONN_STATE_ERROR,             // Error state
    CONN_STATE_RECONNECTING       // Attempting reconnection
} ConnectionState;

// Device status structure
typedef struct {
    ConnectionState state;
    int retryCount;
    double nextRetryTime;         // Timer() value for next retry
    char lastError[MAX_ERROR_MSG_LENGTH];
    double lastUpdateTime;        // Last successful update
    
    // Device-specific handles/IDs
    union {
        PSB_Handle* psbHandle;    // For PSB device
        int32_t biologicID;       // For BioLogic device
    };
} DeviceStatus;

// Overall status module state
typedef struct {
    // Device statuses
    DeviceStatus psbStatus;
    DeviceStatus biologicStatus;
    
    // Timer management
    int timerActive;
    double lastTimerUpdate;
    
    // Thread management
    CmtThreadFunctionID discoveryThreadID;
    CmtThreadFunctionID timerThreadID;
    int shutdownRequested;
    
    // UI panel handle (copy from main)
    int panelHandle;
    
    // PSB handle storage
    PSB_Handle psbDevice;
    
} StatusModuleState;

/******************************************************************************
 * Public Function Declarations
 ******************************************************************************/

/**
 * Initialize the status monitoring module
 * @param panelHandle - Main panel handle for UI updates
 * @return SUCCESS or error code
 */
int Status_Initialize(int panelHandle);

/**
 * Start status monitoring for all enabled devices
 * @return SUCCESS or error code
 */
int Status_Start(void);

/**
 * Stop status monitoring and disconnect devices
 * @return SUCCESS or error code
 */
int Status_Stop(void);

/**
 * Clean up and release all resources
 */
void Status_Cleanup(void);

/**
 * Pause status monitoring (for exclusive access during tests)
 * @return SUCCESS or error code
 */
int Status_Pause(void);

/**
 * Resume status monitoring after pause
 * @return SUCCESS or error code
 */
int Status_Resume(void);

/**
 * Get current connection state for a device
 * @param isPSB - 1 for PSB, 0 for BioLogic
 * @return Current connection state
 */
ConnectionState Status_GetDeviceState(int isPSB);

/**
 * Force a reconnection attempt for a device
 * @param isPSB - 1 for PSB, 0 for BioLogic
 * @return SUCCESS or error code
 */
int Status_ForceReconnect(int isPSB);

/**
 * Get PSB handle for external use (e.g., remote mode control, test suite)
 * @return Pointer to PSB handle or NULL if not connected
 */
PSB_Handle* Status_GetPSBHandle(void);

/**
 * Set PSB handle from external source (e.g., queue manager)
 * @param handle - PSB handle from queue manager
 */
void Status_SetPSBHandle(PSB_Handle* handle);

/**
 * Get BioLogic device ID for external use
 * @return Device ID or -1 if not connected
 */
int32_t Status_GetBioLogicID(void);

/******************************************************************************
 * Callback Functions (used internally but declared here)
 ******************************************************************************/

/**
 * Thread function for initial device discovery
 */
int CVICALLBACK Status_DiscoveryThread(void *functionData);

#endif // STATUS_H