/******************************************************************************
 * controls.h
 * 
 * UI Controls Module Header
 * Handles control callbacks and state management for Battery Tester
 ******************************************************************************/

#ifndef CONTROLS_H
#define CONTROLS_H

#include "common.h"

/******************************************************************************
 * Public Function Declarations
 ******************************************************************************/

/**
 * Initialize the controls module
 * Sets up initial control states based on device states
 * @param panelHandle - Main panel handle
 * @return SUCCESS or error code
 */
int Controls_Initialize(int panelHandle);

/**
 * Clean up controls module resources
 */
void Controls_Cleanup(void);

/**
 * Update control states based on device status changes
 * Called by status module when device states change
 */
void Controls_UpdateFromDeviceStates(void);

/**
 * Remote Mode Toggle Callback
 * Handles PSB remote mode on/off toggle
 */
int CVICALLBACK RemoteModeToggle(int panel, int control, int event,
                                 void *callbackData, int eventData1, int eventData2);

/**
 * DTB Run/Stop Button Callback
 * Handles starting/stopping temperature control
 */
int CVICALLBACK DTBRunStopCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2);

/**
 * Check if remote mode change is pending
 * @return 1 if pending, 0 otherwise
 */
int Controls_IsRemoteModeChangePending(void);

/**
 * Check if DTB run state change is pending
 * @return 1 if pending, 0 otherwise
 */
int Controls_IsDTBRunStateChangePending(void);

/**
 * Notify controls module of PSB remote mode state
 * Used for state synchronization
 * @param remoteMode - Current remote mode state (0 or 1)
 */
void Controls_NotifyRemoteModeState(int remoteMode);

/**
 * Notify controls module of DTB run state
 * Used for state synchronization
 * @param isRunning - Current DTB output enabled state (0 or 1)
 * @param setpoint - Current setpoint value (tracked but not displayed)
 * 
 * Note: The setpoint value is tracked internally but NOT written to the 
 *       setpoint control to avoid overwriting user edits
 */
void Controls_NotifyDTBRunState(int isRunning, double setpoint);

#endif // CONTROLS_H