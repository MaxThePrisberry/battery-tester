/******************************************************************************
 * status.h
 * 
 * Enhanced Device Status Monitoring Module Header
 * Monitors queue manager states with independent device timing and async updates
 ******************************************************************************/
#ifndef STATUS_H
#define STATUS_H

#include "common.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/
// Update rates
#define STATUS_UPDATE_RATE_HZ           1    // Status update frequency in Hz
#define STATUS_UPDATE_PERIOD_MS         (1000 / STATUS_UPDATE_RATE_HZ)  // 1000ms
#define STATUS_CALLBACK_TIMEOUT_MS      5000 // Timeout for async status callbacks

#define DEVICE_PSB 0
#define DEVICE_BIOLOGIC 1
#define DEVICE_DTB_BASE 2
#define DEVICE_COUNT (DEVICE_DTB_BASE + DTB_NUM_DEVICES)

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Module state enumeration
typedef enum {
    STATUS_STATE_UNINITIALIZED = 0,
    STATUS_STATE_INITIALIZING,
    STATUS_STATE_RUNNING,
    STATUS_STATE_PAUSED,
    STATUS_STATE_STOPPING,
    STATUS_STATE_STOPPED
} StatusModuleState;

// Connection state for each device
typedef enum {
    CONN_STATE_IDLE = 0,          // Not started/stopped
    CONN_STATE_DISCOVERING,       // Searching for device
    CONN_STATE_CONNECTING,        // Found, connecting
    CONN_STATE_CONNECTED,         // Successfully connected and running
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
 * Update the remote mode LED state
 * @param isOn - 1 to turn LED on, 0 to turn off
 */
void Status_UpdateRemoteLED(int isOn);

#endif // STATUS_H