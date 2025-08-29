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
 * Module Data Structures
 ******************************************************************************/

typedef struct {
    int slaveAddress;
    int runButtonControlID;
    int setpointControlID;
    
    // State tracking
    volatile int runStateChangePending;
    volatile int pendingRunState;
    int lastKnownRunState;
    double lastKnownSetpoint;
} DTBDeviceControl;

typedef struct {
    int deviceIndex;  // For passing to callbacks
} DTBCallbackData;

static struct {
    int panelHandle;
    
    // Remote mode state tracking
    volatile int remoteModeChangePending;
    volatile int pendingRemoteModeValue;
    int lastKnownRemoteMode;
    
    // DTB device array
    DTBDeviceControl dtbDevices[DTB_NUM_DEVICES];
    int numDTBDevices;
    
} g_controls = {0};

static int g_initialized = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static void CVICALLBACK DeferredControlUpdate(void* data);
static void CVICALLBACK DeferredButtonTextUpdate(void* data);
static void RemoteModeCallback(CommandID cmdId, PSBCommandType type, 
                              void *result, void *userData);
static void HandleDTBRunStopAction(int deviceIndex, int panel, int control);
static void DTBSetpointCallback(CommandID cmdId, DTBCommandType type,
                               void *result, void *userData);
static void DTBRunStopQueueCallback(CommandID cmdId, DTBCommandType type,
                                   void *result, void *userData);
static void UpdateDTBButtonState(int deviceIndex, int isRunning);
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
 * Helper Function Prototypes
 ******************************************************************************/

static bool IsValidDTBDeviceIndex(int deviceIndex);
static int FindDTBDeviceByControl(int controlID);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int Controls_Initialize(int panelHandle) {
    if (g_initialized) {
        LogWarning("Controls module already initialized");
        return SUCCESS;
    }
    
    if (panelHandle <= 0) {
        LogError("Invalid panel handle provided to Controls_Initialize");
        return ERR_INVALID_PARAMETER;
    }
    
    // Initialize state - internal setup only
    memset(&g_controls, 0, sizeof(g_controls));
    g_controls.panelHandle = panelHandle;
    
    // Initialize remote mode state
    g_controls.remoteModeChangePending = 0;
    g_controls.pendingRemoteModeValue = 0;
    g_controls.lastKnownRemoteMode = 0;
    
    // Initialize DTB devices array
    g_controls.numDTBDevices = DTB_NUM_DEVICES;
    
    if (DTB_NUM_DEVICES > 0) {
        // Setup DTB device 0 (DTB1)
        g_controls.dtbDevices[0].slaveAddress = DTB1_SLAVE_ADDRESS;
        g_controls.dtbDevices[0].runButtonControlID = PANEL_BTN_DTB_1_RUN_STOP;
        g_controls.dtbDevices[0].setpointControlID = PANEL_NUM_DTB_1_SETPOINT;
        g_controls.dtbDevices[0].runStateChangePending = 0;
        g_controls.dtbDevices[0].pendingRunState = 0;
        g_controls.dtbDevices[0].lastKnownRunState = 0;
        g_controls.dtbDevices[0].lastKnownSetpoint = 0.0;
    }
    
    if (DTB_NUM_DEVICES > 1) {
        // Setup DTB device 1 (DTB2)
        g_controls.dtbDevices[1].slaveAddress = DTB2_SLAVE_ADDRESS;
        g_controls.dtbDevices[1].runButtonControlID = PANEL_BTN_DTB_2_RUN_STOP;
        g_controls.dtbDevices[1].setpointControlID = PANEL_NUM_DTB_2_SETPOINT;
        g_controls.dtbDevices[1].runStateChangePending = 0;
        g_controls.dtbDevices[1].pendingRunState = 0;
        g_controls.dtbDevices[1].lastKnownRunState = 0;
        g_controls.dtbDevices[1].lastKnownSetpoint = 0.0;
    }
    
    g_initialized = 1;
    LogMessage("Controls module initialized with %d DTB devices", g_controls.numDTBDevices);
    
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
            if (PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL) == PSB_SUCCESS) {
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
    
    // Check DTB device states if no pending changes
    if (ENABLE_DTB) {
        DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
        if (dtbQueueMgr) {
            for (int i = 0; i < g_controls.numDTBDevices; i++) {
                DTBDeviceControl *device = &g_controls.dtbDevices[i];
                
                if (!device->runStateChangePending) {
                    DTB_Status status;
                    if (DTB_GetStatusQueued(device->slaveAddress, &status, DEVICE_PRIORITY_NORMAL) == DTB_SUCCESS) {
                        int stateChanged = (status.outputEnabled != device->lastKnownRunState);
                        int setpointChanged = (fabs(status.setPoint - device->lastKnownSetpoint) >= 0.1);
                        
                        if (stateChanged) {
                            device->lastKnownRunState = status.outputEnabled;
                            UpdateDTBButtonState(i, status.outputEnabled);
                        }
                        
                        if (setpointChanged) {
                            SetCtrlVal(g_controls.panelHandle, device->setpointControlID, status.setPoint);
                        }
                        
                        // Always update internal tracking
                        device->lastKnownSetpoint = status.setPoint;
                        
                        if (stateChanged || (setpointChanged && device->lastKnownSetpoint == 0.0)) {
                            LogMessage("DTB%d state: %s, setpoint: %.1f°C", 
                                     i + 1, status.outputEnabled ? "Running" : "Stopped",
                                     status.setPoint);
                        }
                    }
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
            // Check if remote mode change is already pending
            if (g_controls.remoteModeChangePending) {
                // Reset toggle to last known state
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
                return 0;
            }
            
            // Get PSB queue manager
            PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
            if (!psbQueueMgr) {
                LogWarning("PSB queue manager not available");
                SetCtrlVal(panel, control, g_controls.lastKnownRemoteMode);
                return 0;
            }
            
            // Check if PSB is connected
            PSBQueueStats stats;
            PSB_QueueGetStats(psbQueueMgr, &stats);
            if (!stats.isConnected) {
                LogWarning("PSB not connected - cannot change remote mode");
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
			CommandID cmdId = PSB_SetRemoteModeAsync(enable, DEVICE_PRIORITY_NORMAL, RemoteModeCallback, NULL);
            
            if (cmdId == 0) {
                LogError("Failed to queue remote mode command");
                
                // Clear pending flags
                g_controls.remoteModeChangePending = 0;
                
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
}

/******************************************************************************
 * DTB Run/Stop Implementation - CVICALLBACK Functions
 ******************************************************************************/

int CVICALLBACK DTB1RunStopCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2) {
    if (event == EVENT_COMMIT) {
        HandleDTBRunStopAction(0, panel, control); // Device 0
    }
    return 0;
}

int CVICALLBACK DTB2RunStopCallback(int panel, int control, int event,
                                   void *callbackData, int eventData1, int eventData2) {
    if (event == EVENT_COMMIT) {
        HandleDTBRunStopAction(1, panel, control); // Device 1  
    }
    return 0;
}

/******************************************************************************
 * DTB Run/Stop Implementation - Internal Handler
 ******************************************************************************/

static void HandleDTBRunStopAction(int deviceIndex, int panel, int control) {
    if (!IsValidDTBDeviceIndex(deviceIndex)) {
        LogError("Invalid DTB device index: %d", deviceIndex);
        return;
    }
    
    DTBDeviceControl *device = &g_controls.dtbDevices[deviceIndex];
    
    // Check if this device already has a run state change pending
    if (device->runStateChangePending) {
        return;
    }
    
    // Get DTB queue manager
    DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
    if (!dtbQueueMgr) {
        LogWarning("DTB queue manager not available");
        return;
    }
    
    // Check if DTB is connected
    DTBQueueStats stats;
    DTB_QueueGetStats(dtbQueueMgr, &stats);
    if (!stats.isConnected) {
        LogWarning("DTB not connected");
        return;
    }
    
    // Allocate callback data
    DTBCallbackData *callbackData = malloc(sizeof(DTBCallbackData));
    if (!callbackData) {
        LogError("Failed to allocate callback data");
        return;
    }
    callbackData->deviceIndex = deviceIndex;
    
    // Determine action based on current state
    if (device->lastKnownRunState) {
        // Currently running - stop it
        device->runStateChangePending = 1;
        device->pendingRunState = 0;
        
        LogMessage("Stopping DTB%d temperature control...", deviceIndex + 1);
        
        // Queue stop command
        CommandID cmdId = DTB_SetRunStopAsync(device->slaveAddress, 0, DTBRunStopQueueCallback, callbackData, DEVICE_PRIORITY_NORMAL);
        
        if (cmdId == 0) {
            LogError("Failed to queue DTB%d stop command", deviceIndex + 1);
            device->runStateChangePending = 0;
            free(callbackData);
        }
        
    } else {
        // Currently stopped - start it
        // First get the setpoint value
        double setpoint;
        GetCtrlVal(panel, device->setpointControlID, &setpoint);
        
        device->runStateChangePending = 1;
        device->pendingRunState = 1;
        
        LogMessage("Setting DTB%d setpoint to %.1f°C...", deviceIndex + 1, setpoint);
        
        // Store the setpoint we're sending
        device->lastKnownSetpoint = setpoint;
        
        // Queue setpoint command first
        CommandID cmdId = DTB_SetSetPointAsync(device->slaveAddress, setpoint, DTBSetpointCallback, callbackData, DEVICE_PRIORITY_NORMAL);
        
        if (cmdId == 0) {
            LogError("Failed to queue DTB%d setpoint command", deviceIndex + 1);
            device->runStateChangePending = 0;
            free(callbackData);
        }
    }
}

static void DTBSetpointCallback(CommandID cmdId, DTBCommandType type,
                               void *result, void *userData) {
    DTBCommandResult *cmdResult = (DTBCommandResult *)result;
    DTBCallbackData *callbackData = (DTBCallbackData *)userData;
    
    if (!callbackData || !IsValidDTBDeviceIndex(callbackData->deviceIndex)) {
        LogError("Invalid callback data in DTBSetpointCallback");
        if (callbackData) free(callbackData);
        return;
    }
    
    int deviceIndex = callbackData->deviceIndex;
    DTBDeviceControl *device = &g_controls.dtbDevices[deviceIndex];
    
    if (cmdResult && cmdResult->errorCode == DTB_SUCCESS) {
        // Setpoint set successfully, now start the controller
        DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
        if (dtbQueueMgr) {
            LogMessage("Starting DTB%d temperature control...", deviceIndex + 1);
            
            CommandID cmdId = DTB_SetRunStopAsync(device->slaveAddress, 1, DTBRunStopQueueCallback, callbackData, DEVICE_PRIORITY_NORMAL);
            
            if (cmdId == 0) {
                LogError("Failed to queue DTB%d start command", deviceIndex + 1);
                device->runStateChangePending = 0;
                free(callbackData);
            }
            // Don't free callbackData here - it will be used by DTBRunStopQueueCallback
            return;
        }
    } else {
        // Failed to set setpoint
        const char *errorStr = cmdResult ? DTB_GetErrorString(cmdResult->errorCode) : "Unknown error";
        LogError("Failed to set DTB%d setpoint: %s", deviceIndex + 1, errorStr);
        
        device->runStateChangePending = 0;
        free(callbackData);
    }
}

static void DTBRunStopQueueCallback(CommandID cmdId, DTBCommandType type,
                                   void *result, void *userData) {
    DTBCommandResult *cmdResult = (DTBCommandResult *)result;
    DTBCallbackData *callbackData = (DTBCallbackData *)userData;
    
    if (!callbackData || !IsValidDTBDeviceIndex(callbackData->deviceIndex)) {
        LogError("Invalid callback data in DTBRunStopQueueCallback");
        if (callbackData) free(callbackData);
        return;
    }
    
    int deviceIndex = callbackData->deviceIndex;
    DTBDeviceControl *device = &g_controls.dtbDevices[deviceIndex];
    
    if (cmdResult && cmdResult->errorCode == DTB_SUCCESS) {
        // Success - update state
        device->lastKnownRunState = device->pendingRunState;
        UpdateDTBButtonState(deviceIndex, device->pendingRunState);
        
        LogMessage("DTB%d temperature control %s", deviceIndex + 1,
                  device->pendingRunState ? "started" : "stopped");
    } else {
        // Failed - revert to last known state
        const char *errorStr = cmdResult ? DTB_GetErrorString(cmdResult->errorCode) : "Unknown error";
        LogError("Failed to %s DTB%d: %s", 
                device->pendingRunState ? "start" : "stop", deviceIndex + 1, errorStr);
        
        UpdateDTBButtonState(deviceIndex, device->lastKnownRunState);
    }
    
    // Clear pending flags
    device->runStateChangePending = 0;
    
    // Clean up
    free(callbackData);
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

void Controls_NotifyDTBRunState(int deviceIndex, int isRunning, double setpoint) {
    if (!IsValidDTBDeviceIndex(deviceIndex)) {
        LogWarning("Controls_NotifyDTBRunState: Invalid device index: %d", deviceIndex);
        return;
    }
    
    DTBDeviceControl *device = &g_controls.dtbDevices[deviceIndex];
    
    if (!device->runStateChangePending) {
        // Only update if no change is pending
        if (isRunning != device->lastKnownRunState) {
            device->lastKnownRunState = isRunning;
            UpdateDTBButtonState(deviceIndex, isRunning);
        }
        
        // Update internal tracking of setpoint, but DON'T update the control
        // The user might be editing it
        device->lastKnownSetpoint = setpoint;
    }
}

/******************************************************************************
 * Query Functions
 ******************************************************************************/

int Controls_IsRemoteModeChangePending(void) {
    return g_controls.remoteModeChangePending;
}

int Controls_IsDTBRunStateChangePending(int deviceIndex) {
    if (!IsValidDTBDeviceIndex(deviceIndex)) {
        return 0;
    }
    return g_controls.dtbDevices[deviceIndex].runStateChangePending;
}

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static void UpdateDTBButtonState(int deviceIndex, int isRunning) {
    if (!IsValidDTBDeviceIndex(deviceIndex)) {
        LogError("UpdateDTBButtonState: Invalid device index: %d", deviceIndex);
        return;
    }
    
    DTBDeviceControl *device = &g_controls.dtbDevices[deviceIndex];
    
    // Update button text
    ControlUpdateData* textData = malloc(sizeof(ControlUpdateData));
    if (textData) {
        textData->control = device->runButtonControlID;
        strcpy(textData->strValue, isRunning ? "Stop" : "Run");
        PostDeferredCall(DeferredButtonTextUpdate, textData);
    }
    
    // Update setpoint control dimming
    ControlUpdateData* dimData = malloc(sizeof(ControlUpdateData));
    if (dimData) {
        dimData->control = device->setpointControlID;
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
        // Check if this is a DTB setpoint control by finding the device index
        int deviceIndex = FindDTBDeviceByControl(updateData->control);
        if (deviceIndex >= 0) {
            // This is a DTB setpoint control - intValue is dimming state
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
 * Helper Functions Implementation
 ******************************************************************************/

static bool IsValidDTBDeviceIndex(int deviceIndex) {
    return (deviceIndex >= 0 && deviceIndex < g_controls.numDTBDevices);
}

static int FindDTBDeviceByControl(int controlID) {
    for (int i = 0; i < g_controls.numDTBDevices; i++) {
        if (g_controls.dtbDevices[i].setpointControlID == controlID) {
            return i;
        }
    }
    return -1; // Not found
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
            int result = TNY_SetPinQueued(13, toggleValue ? TNY_PIN_STATE_HIGH : TNY_PIN_STATE_LOW, DEVICE_PRIORITY_NORMAL);
            
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