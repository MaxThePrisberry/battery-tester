/******************************************************************************
 * status.h
 * 
 * Device Status Monitoring Module Header
 * Monitors queue manager states and updates UI periodically
 ******************************************************************************/
#ifndef STATUS_H
#define STATUS_H

#include "common.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/
// Device enable flags - set to 1 to enable monitoring, 0 to disable
#define STATUS_MONITOR_PSB         1    // Enable PSB 10000 monitoring
#define STATUS_MONITOR_BIOLOGIC    0    // Enable BioLogic SP-150e monitoring

// Update rates
#define STATUS_UPDATE_RATE_HZ      5    // Status update frequency in Hz
#define STATUS_UPDATE_PERIOD_MS    (1000 / STATUS_UPDATE_RATE_HZ)  // 200ms

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
 * Stop status monitoring
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
 * Set/clear the remote mode change pending flag
 * @param pending - 1 if change is pending, 0 if complete
 * @param value - The pending value (only used if pending=1)
 */
void Status_SetRemoteModeChangePending(int pending, int value);

/**
 * Check if a remote mode change is pending
 * @return 1 if pending, 0 otherwise
 */
int Status_IsRemoteModeChangePending(void);

/**
 * Update the remote mode LED state
 * @param isOn - 1 to turn LED on, 0 to turn off
 */
void Status_UpdateRemoteLED(int isOn);

#endif // STATUS_H