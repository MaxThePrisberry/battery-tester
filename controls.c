/******************************************************************************
 * controls.c
 * 
 * UI Controls Module Implementation
 * Handles control callbacks and state management for Battery Tester
 ******************************************************************************/

#include "common.h"
#include "controls.h"
#include "BatteryTester.h"
#include "psb10000_queue.h"
#include "dtb4848_queue.h"
#include "teensy_queue.h"
#include "status.h"
#include "logging.h"

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static struct {
    int panelHandle;
    
    // Remote mode state tracking
    volatile int remoteModeChangePending;
    volatile int pendingRemoteModeValue;
    int lastKnownRemoteMode;
    
    // DTB run state tracking
    volatile int dtbRunStateChangePending;
    volatile int pendingDTBRunState;
    int lastKnownDTBRunState;
    double lastKnownDTBSetpoint;
    
} g_controls = {0};

static int g_initialized = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static void CVICALLBACK DeferredControlUpdate(void* data);
static void CVICALLBACK DeferredButtonTextUpdate(void* data);
static void RemoteModeCallback(CommandID cmdId, PSBCommandType type, 
                              void *result, void *userData);
static void DTBSetpointCallback(CommandID cmdId, DTBCommandType type,
                               void *result, void *userData);
static void DTBRunStopQueueCallback(CommandID cmdId, DTBCommandType type,
                                   void *result, void *userData);
static void UpdateDTBButtonState(int isRunning);
static void UpdateRemoteToggleState(int remoteMode);

/******************************************************************************
 * UI Update Data Structures
 ******************************************************************************/

typedef struct {
    int control;
    int intValue;    // For dimming/enabling
    char strValue[MEDIUM_BUFFER_SIZE]; // For button text
} ControlUpdateData;

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int Controls_Initialize(int panelHandle) {
    if (g_initialized) {
        LogWarning("Controls module already initialized");
        return SUCCESS;
    }
    
    // Initialize state - internal setup only
    memset(&g_controls, 0, sizeof(g_controls));
    g_controls.panelHandle = panelHandle;
    
    // Initialize pending flags to false
    g_controls.remoteModeChangePending = 0;
    g_controls.pendingRemoteModeValue = 0;
    g_controls.lastKnownRemoteMode = 0;
    
    g_controls.dtbRunStateChangePending = 0;
    g_controls.pendingDTBRunState = 0;
    g_controls.lastKnownDTBRunState = 0;
    g_controls.lastKnownDTBSetpoint = 0.0;
    
    g_initialized = 1;
    LogMessage("Controls module initialized");
    
    return SUCCESS;
}

int Controls_Start(void) {
    if (!g_initialized) {
        LogError("Controls module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    LogMessage("Starting controls module - syncing with device states...");
    
    // Use the existing function to sync with device states
    // At startup, there won't be any pending operations, so this is safe
    Controls_UpdateFromDeviceStates();
    
    LogMessage("Controls module started");
    return SUCCESS;
}

void Controls_Cleanup(void) {
    if (!g_initialized) {
        return;
    }
    
    g_initialized = 0;
    LogMessage("Controls module cleaned up");
}

void Controls_UpdateFromDeviceStates(void) {
    if (!g_initialized) {
        LogWarning("Controls module not initialized");
        return;
    }
    
    // Check PSB state if no pending changes
    if (ENABLE_PSB && !g_controls.remoteModeChangePending) {
        PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
        if (psbQueueMgr) {
            PSB_Status status;
            if (PSB_GetStatusQueued(&status) == PSB_SUCCESS) {
                if (status.remoteMode != g_controls.lastKnownRemoteMode) {
                    g_controls.lastKnownRemoteMode = status.remoteMode;
                    UpdateRemoteToggleState(status.remoteMode);
                    Status_UpdateRemoteLED(status.remoteMode);
                    LogMessage("PSB remote mode: %s", 
                             status.remoteMode ? "ON" : "OFF");
                }
            }
        }
    }
    
    // Check DTB state if no pending changes
    if (ENABLE_DTB && !g_controls.dtbRunStateChangePending) {
        DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
        if (dtbQueueMgr) {
            DTB_Status status;
            if (DTB_GetStatusQueued(&status) == DTB_SUCCESS) {
                int stateChanged = (status.outputEnabled != g_controls.lastKnownDTBRunState);
                int setpointChanged = (fabs(status.setPoint - g_controls.lastKnownDTBSetpoint) >= 0.1);
                
                if (stateChanged) {
                    g_controls.lastKnownDTBRunState = status.outputEnabled;
                    UpdateDTBButtonState(status.outputEnabled);
                }
                
                if (setpointChanged) {
                    SetCtrlVal(g_controls.panelHandle, PANEL_NUM_DTB_SETPOINT, status.setPoint);
                }
                
                // Always update internal tracking
                g_controls.lastKnownDTBSetpoint = status.setPoint;
                
                if (stateChanged || (setpointChanged && g_controls.lastKnownDTBSetpoint == 0.0)) {
                    LogMessage("DTB state: %s, setpoint: %.1f°C", 
                             status.outputEnabled ? "Running" : "Stopped",
                             status.setPoint);
                }
            }
        }
    }
}

/******************************************************************************
 * Remote Mode Toggle Implementation
 ******************************************************************************/

int CVICALLBACK RemoteModeToggle(int panel, int control, int event,
                                void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if system is busy
            CmtGetLock(g_busyLock);
            if (g_systemBusy || g_controls.remoteModeChangePending) {
                CmtReleaseLock(g_busyLock);
                LogWarning("System is busy - please wait for current operation to complete");
                
                // Reset toggle to last known state
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
                return 0;
            }
            
            // Mark system as busy
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);
            
            // Get PSB queue manager
            PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
            if (!psbQueueMgr) {
                LogWarning("PSB queue manager not available");
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
                return 0;
            }
            
            // Check if PSB is connected
            PSBQueueStats stats;
            PSB_QueueGetStats(psbQueueMgr, &stats);
            if (!stats.isConnected) {
                LogWarning("PSB not connected - cannot change remote mode");
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
                return 0;
            }
            
            // Get toggle value
            int enable;
            GetCtrlVal(panel, control, &enable);
            
            // Set pending flags
            g_controls.remoteModeChangePending = 1;
            g_controls.pendingRemoteModeValue = enable;
            
            LogMessage("Changing remote mode to %s...", enable ? "ON" : "OFF");
            
            // Queue the command asynchronously
			CommandID cmdId = PSB_SetRemoteModeAsync(enable, RemoteModeCallback, NULL);
            
            if (cmdId == 0) {
                LogError("Failed to queue remote mode command");
                
                // Clear pending flags
                g_controls.remoteModeChangePending = 0;
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                
                // Reset toggle
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
            }
            
            break;
    }
    return 0;
}

static void RemoteModeCallback(CommandID cmdId, PSBCommandType type, 
                              void *result, void *userData) {
    PSBCommandResult *cmdResult = (PSBCommandResult *)result;
    
    if (cmdResult && cmdResult->errorCode == PSB_SUCCESS) {
        // Success - update last known state
        g_controls.lastKnownRemoteMode = g_controls.pendingRemoteModeValue;
        Status_UpdateRemoteLED(g_controls.pendingRemoteModeValue);
        LogMessage("Remote mode changed to %s", 
                  g_controls.pendingRemoteModeValue ? "ON" : "OFF");
    } else {
        // Failed - revert toggle to last known state
        const char *errorStr = cmdResult ? PSB_GetErrorString(cmdResult->errorCode) : "Unknown error";
        LogError("Failed to set remote mode: %s", errorStr);
        
        UpdateRemoteToggleState(g_controls.lastKnownRemoteMode);
        Status_UpdateRemoteLED(g_controls.lastKnownRemoteMode);
    }
    
    // Clear pending flags
    g_controls.remoteModeChangePending = 0;
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
}

/******************************************************************************
 * DTB Run/Stop Implementation
 ******************************************************************************/

int CVICALLBACK DTBRunStopCallback(int panel, int control, int event,
                                  void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if system is busy
            CmtGetLock(g_busyLock);
            if (g_systemBusy || g_controls.dtbRunStateChangePending) {
                CmtReleaseLock(g_busyLock);
                LogWarning("System is busy - please wait for current operation to complete");
                return 0;
            }
            
            // Mark system as busy
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);
            
            // Get DTB queue manager
            DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
            if (!dtbQueueMgr) {
                LogWarning("DTB queue manager not available");
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }
            
            // Check if DTB is connected
            DTBQueueStats stats;
            DTB_QueueGetStats(dtbQueueMgr, &stats);
            if (!stats.isConnected) {
                LogWarning("DTB not connected");
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }
            
            // Determine action based on current state
            if (g_controls.lastKnownDTBRunState) {
                // Currently running - stop it
                g_controls.dtbRunStateChangePending = 1;
                g_controls.pendingDTBRunState = 0;
                
                LogMessage("Stopping DTB temperature control...");
                
                // Queue stop command
				CommandID cmdId = DTB_SetRunStopAsync(0, DTBRunStopQueueCallback, NULL);
                
                if (cmdId == 0) {
                    LogError("Failed to queue DTB stop command");
                    g_controls.dtbRunStateChangePending = 0;
                    CmtGetLock(g_busyLock);
                    g_systemBusy = 0;
                    CmtReleaseLock(g_busyLock);
                }
                
            } else {
                // Currently stopped - start it
                // First get the setpoint value
                double setpoint;
                GetCtrlVal(panel, PANEL_NUM_DTB_SETPOINT, &setpoint);
                
                g_controls.dtbRunStateChangePending = 1;
                g_controls.pendingDTBRunState = 1;
                
                LogMessage("Setting DTB setpoint to %.1f°C...", setpoint);
                
                // Store the setpoint we're sending
                g_controls.lastKnownDTBSetpoint = setpoint;
                
                // Queue setpoint command first
				CommandID cmdId = DTB_SetSetPointAsync(setpoint, DTBSetpointCallback, NULL);
                
                if (cmdId == 0) {
                    LogError("Failed to queue DTB setpoint command");
                    g_controls.dtbRunStateChangePending = 0;
                    CmtGetLock(g_busyLock);
                    g_systemBusy = 0;
                    CmtReleaseLock(g_busyLock);
                }
            }
            
            break;
    }
    return 0;
}

static void DTBSetpointCallback(CommandID cmdId, DTBCommandType type,
                               void *result, void *userData) {
    DTBCommandResult *cmdResult = (DTBCommandResult *)result;
    
    if (cmdResult && cmdResult->errorCode == DTB_SUCCESS) {
        // Setpoint set successfully, now start the controller
        DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
        if (dtbQueueMgr) {
            LogMessage("Starting DTB temperature control...");
            
			CommandID cmdId = DTB_SetRunStopAsync(1, DTBRunStopQueueCallback, NULL);
            
            if (cmdId == 0) {
                LogError("Failed to queue DTB start command");
                g_controls.dtbRunStateChangePending = 0;
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
        }
    } else {
        // Failed to set setpoint
        const char *errorStr = cmdResult ? DTB_GetErrorString(cmdResult->errorCode) : "Unknown error";
        LogError("Failed to set DTB setpoint: %s", errorStr);
        
        g_controls.dtbRunStateChangePending = 0;
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
    }
}

static void DTBRunStopQueueCallback(CommandID cmdId, DTBCommandType type,
                                   void *result, void *userData) {
    DTBCommandResult *cmdResult = (DTBCommandResult *)result;
    
    if (cmdResult && cmdResult->errorCode == DTB_SUCCESS) {
        // Success - update state
        g_controls.lastKnownDTBRunState = g_controls.pendingDTBRunState;
        UpdateDTBButtonState(g_controls.pendingDTBRunState);
        
        LogMessage("DTB temperature control %s", 
                  g_controls.pendingDTBRunState ? "started" : "stopped");
    } else {
        // Failed - revert to last known state
        const char *errorStr = cmdResult ? DTB_GetErrorString(cmdResult->errorCode) : "Unknown error";
        LogError("Failed to %s DTB: %s", 
                g_controls.pendingDTBRunState ? "start" : "stop", errorStr);
        
        UpdateDTBButtonState(g_controls.lastKnownDTBRunState);
    }
    
    // Clear pending flags
    g_controls.dtbRunStateChangePending = 0;
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
}

/******************************************************************************
 * State Notification Functions
 ******************************************************************************/

void Controls_NotifyRemoteModeState(int remoteMode) {
    if (!g_controls.remoteModeChangePending) {
        // Only update if no change is pending
        if (remoteMode != g_controls.lastKnownRemoteMode) {
            g_controls.lastKnownRemoteMode = remoteMode;
            UpdateRemoteToggleState(remoteMode);
        }
    }
}

void Controls_NotifyDTBRunState(int isRunning, double setpoint) {
    if (!g_controls.dtbRunStateChangePending) {
        // Only update if no change is pending
        if (isRunning != g_controls.lastKnownDTBRunState) {
            g_controls.lastKnownDTBRunState = isRunning;
            UpdateDTBButtonState(isRunning);
        }
        
        // Update internal tracking of setpoint, but DON'T update the control
        // The user might be editing it
        g_controls.lastKnownDTBSetpoint = setpoint;
    }
}

/******************************************************************************
 * Query Functions
 ******************************************************************************/

int Controls_IsRemoteModeChangePending(void) {
    return g_controls.remoteModeChangePending;
}

int Controls_IsDTBRunStateChangePending(void) {
    return g_controls.dtbRunStateChangePending;
}

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static void UpdateDTBButtonState(int isRunning) {
    // Update button text
    ControlUpdateData* textData = malloc(sizeof(ControlUpdateData));
    if (textData) {
        textData->control = PANEL_BTN_DTB_RUN_STOP;
        strcpy(textData->strValue, isRunning ? "Stop" : "Run");
        PostDeferredCall(DeferredButtonTextUpdate, textData);
    }
    
    // Update setpoint control dimming
    ControlUpdateData* dimData = malloc(sizeof(ControlUpdateData));
    if (dimData) {
        dimData->control = PANEL_NUM_DTB_SETPOINT;
        dimData->intValue = isRunning ? 1 : 0; // 1 = dim, 0 = enable
        PostDeferredCall(DeferredControlUpdate, dimData);
    }
}

static void UpdateRemoteToggleState(int remoteMode) {
    ControlUpdateData* data = malloc(sizeof(ControlUpdateData));
    if (data) {
        data->control = PANEL_TOGGLE_REMOTE_MODE;
        data->intValue = remoteMode;
        PostDeferredCall(DeferredControlUpdate, data);
    }
}

/******************************************************************************
 * Deferred Callback Functions
 ******************************************************************************/

static void CVICALLBACK DeferredControlUpdate(void* data) {
    ControlUpdateData* updateData = (ControlUpdateData*)data;
    if (updateData && g_controls.panelHandle > 0) {
        if (updateData->control == PANEL_NUM_DTB_SETPOINT) {
            // For setpoint, intValue is dimming state
            SetCtrlAttribute(g_controls.panelHandle, updateData->control, 
                           ATTR_DIMMED, updateData->intValue);
        } else {
            // For other controls, set the value
            SetCtrlVal(g_controls.panelHandle, updateData->control, updateData->intValue);
        }
        free(updateData);
    }
}

static void CVICALLBACK DeferredButtonTextUpdate(void* data) {
    ControlUpdateData* updateData = (ControlUpdateData*)data;
    if (updateData && g_controls.panelHandle > 0) {
        SetCtrlAttribute(g_controls.panelHandle, updateData->control, 
                        ATTR_LABEL_TEXT, updateData->strValue);
        free(updateData);
    }
}

/******************************************************************************
 * TestTeensyCallback - Toggle callback to control Teensy pin 13
 * 
 * This callback responds to toggle changes and sets Teensy pin 13 high/low
 * through the queue system.
 ******************************************************************************/
int CVICALLBACK TestTeensyCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT: {
            // Get toggle value
            int toggleValue = 0;
            GetCtrlVal(panel, control, &toggleValue);
			TNYQueueManager *g_tnyQueueMgr = TNY_GetGlobalQueueManager();
            
            // Check if Teensy queue is available
            if (!g_tnyQueueMgr) {
                LogError("Teensy queue manager not initialized");
                MessagePopup("Error", "Teensy is not connected!");
                
                // Reset toggle to off
                SetCtrlVal(panel, control, 0);
                return 0;
            }
            
            // Log the action
            LogMessage("Setting Teensy pin 13 to %s", toggleValue ? "HIGH" : "LOW");
            
            // Set pin 13 through the queue
            int result = TNY_SetPinQueued(13, toggleValue ? TNY_PIN_STATE_HIGH : TNY_PIN_STATE_LOW);
            
            if (result != TNY_SUCCESS) {
                LogError("Failed to set Teensy pin 13: %s", TNY_GetErrorString(result));
                
                // Show error to user
                char errorMsg[256];
                sprintf(errorMsg, "Failed to control Teensy pin 13:\n%s", 
                       TNY_GetErrorString(result));
                MessagePopup("Teensy Control Error", errorMsg);
                
                // Reset toggle to opposite state since command failed
                SetCtrlVal(panel, control, !toggleValue);
            }
            
            break;
        }
    }
    return 0;
}