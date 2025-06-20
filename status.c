/******************************************************************************
 * status.c
 * 
 * Device Status Monitoring Module Implementation
 * Monitors queue manager states and updates UI periodically
 ******************************************************************************/

#include "common.h"
#include "status.h"
#include "BatteryTester.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "logging.h"
#include <utility.h>
#include <toolbox.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/
static struct {
    int panelHandle;
    CmtThreadFunctionID timerThreadId;
    volatile int timerActive;
    volatile int statusPaused;
    double lastTimerUpdate;
    
    // Last known states for change detection
    ConnectionState lastPSBState;
    ConnectionState lastBioState;
	
	// Track pending remote mode change
    volatile int remoteModeChangePending;
    volatile int pendingRemoteModeValue;
} g_status = {0};

static int g_initialized = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/
static int CVICALLBACK Status_TimerThread(void *functionData);
static void Status_TimerUpdate(void);
static void UpdateDeviceLED(int isPSB, ConnectionState state);
static void UpdateDeviceStatus(int isPSB, const char* message);
static void UpdatePSBValues(PSB_Status* status);
static void CVICALLBACK DeferredLEDUpdate(void* data);
static void CVICALLBACK DeferredStatusUpdate(void* data);
static void CVICALLBACK DeferredNumericUpdate(void* data);
static void CVICALLBACK DeferredToggleUpdate(void* data);
static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            PSBCommandResult *result, void *userData);

/******************************************************************************
 * UI Update Data Structure
 ******************************************************************************/
typedef struct {
    int control;
    int intValue;    // For LED color/state
    double dblValue; // For numeric values
    char strValue[MEDIUM_BUFFER_SIZE]; // For string values
} UIUpdateData;

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int Status_Initialize(int panelHandle) {
    if (g_initialized) {
        LogWarning("Status module already initialized");
        return SUCCESS;
    }
    
    // Initialize state
    memset(&g_status, 0, sizeof(g_status));
    g_status.panelHandle = panelHandle;
    g_status.timerActive = 0;
    g_status.statusPaused = 0;
    g_status.lastPSBState = CONN_STATE_IDLE;
    g_status.lastBioState = CONN_STATE_IDLE;
    
    // Set initial LED states
    if (STATUS_MONITOR_PSB) {
        UpdateDeviceLED(1, CONN_STATE_IDLE);
        UpdateDeviceStatus(1, "PSB Monitoring");
    }
    
    if (STATUS_MONITOR_BIOLOGIC) {
        UpdateDeviceLED(0, CONN_STATE_IDLE);
        UpdateDeviceStatus(0, "BioLogic Monitoring");
    }
    
    g_initialized = 1;
    LogMessage("Status monitoring module initialized");
    
    return SUCCESS;
}

int Status_Start(void) {
    if (!g_initialized) {
        LogError("Status module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    LogMessage("Starting device status monitoring...");
    
    // Start timer thread for periodic updates
    g_status.timerActive = 1;
    g_status.lastTimerUpdate = Timer();
    
    int error = CmtScheduleThreadPoolFunction(g_threadPool, Status_TimerThread, 
                                              NULL, &g_status.timerThreadId);
    if (error != 0) {
        LogError("Failed to start timer thread: %d", error);
        g_status.timerActive = 0;
        return ERR_THREAD_CREATE;
    }
    
    LogMessage("Status monitoring started successfully");
    return SUCCESS;
}

int Status_Stop(void) {
    if (!g_initialized) {
        return SUCCESS;
    }
    
    LogMessage("Stopping device status monitoring...");
    
    // Signal shutdown
    g_status.timerActive = 0;
    
    // Wait for timer thread to complete
    if (g_status.timerThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_status.timerThreadId,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_status.timerThreadId = 0;
    }
    
    LogMessage("Status monitoring stopped");
    return SUCCESS;
}

void Status_Cleanup(void) {
    if (!g_initialized) {
        return;
    }
    
    Status_Stop();
    g_initialized = 0;
    g_status.statusPaused = 0;
    LogMessage("Status module cleaned up");
}

int Status_Pause(void) {
    if (!g_initialized) {
        return SUCCESS;
    }
    
    g_status.statusPaused = 1;
    LogMessage("Status monitoring paused");
    
    // Update UI to show paused state
    if (STATUS_MONITOR_PSB) {
        UpdateDeviceStatus(1, "Monitoring Paused");
    }
    if (STATUS_MONITOR_BIOLOGIC) {
        UpdateDeviceStatus(0, "Monitoring Paused");
    }
    
    return SUCCESS;
}

int Status_Resume(void) {
    if (!g_initialized) {
        return SUCCESS;
    }
    
    g_status.statusPaused = 0;
    LogMessage("Status monitoring resumed");
    
    // Force immediate status update
    Status_TimerUpdate();
    
    return SUCCESS;
}

/******************************************************************************
 * Timer Thread Implementation
 ******************************************************************************/

static int CVICALLBACK Status_TimerThread(void *functionData) {
    LogMessage("Status timer thread started");
    
    while (g_status.timerActive) {
        double currentTime = Timer();
        double elapsedTime = currentTime - g_status.lastTimerUpdate;
        
        // Check if it's time for an update
        if (elapsedTime >= (STATUS_UPDATE_PERIOD_MS / 1000.0)) {
            g_status.lastTimerUpdate = currentTime;
            
            // Perform the status update only if not paused
            if (!g_status.statusPaused) {
                Status_TimerUpdate();
            }
        }
        
        // Small sleep to prevent CPU hogging
        Delay(0.01);  // 10ms
    }
    
    LogMessage("Status timer thread stopped");
    return 0;
}

/******************************************************************************
 * Status Update Implementation
 ******************************************************************************/

static void Status_TimerUpdate(void) {
    if (g_status.statusPaused) {
        return;
    }
    
    // Update PSB status
    if (STATUS_MONITOR_PSB) {
        PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
        if (psbQueueMgr) {
            PSBQueueStats stats;
            PSB_QueueGetStats(psbQueueMgr, &stats);
            
            // Determine current state
            ConnectionState currentState = stats.isConnected ? CONN_STATE_CONNECTED : CONN_STATE_ERROR;
            
            // Update UI if state changed
            if (currentState != g_status.lastPSBState) {
                g_status.lastPSBState = currentState;
                UpdateDeviceLED(1, currentState);
                UpdateDeviceStatus(1, stats.isConnected ? "PSB Connected" : "PSB Not Connected");
                
                // Get initial values if just connected
                if (stats.isConnected && currentState == CONN_STATE_CONNECTED) {
                    PSB_Handle *handle = PSB_QueueGetHandle(psbQueueMgr);
                    if (handle) {
                        PSB_Status status;
                        if (PSB_GetStatus(handle, &status) == PSB_SUCCESS) {
                            UpdatePSBValues(&status);
                            
                            // Set initial remote mode LED state
                            UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
                            if (ledData) {
                                ledData->control = PANEL_LED_REMOTE_MODE;
                                ledData->intValue = status.remoteMode;
                                PostDeferredCall(DeferredLEDUpdate, ledData);
                            }
                            
                            // Set initial remote mode toggle state
                            UIUpdateData* toggleData = malloc(sizeof(UIUpdateData));
                            if (toggleData) {
                                toggleData->control = PANEL_TOGGLE_REMOTE_MODE;
                                toggleData->intValue = status.remoteMode;
                                PostDeferredCall(DeferredToggleUpdate, toggleData);
                            }
                            
                            // Log the initial remote mode state
                            LogMessageEx(LOG_DEVICE_PSB, "Initial remote mode state: %s", 
                                       status.remoteMode ? "ON" : "OFF");
                        }
                    }
                } else if (!stats.isConnected) {
                    // PSB disconnected - update remote mode controls to show disconnected state
                    UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
                    if (ledData) {
                        ledData->control = PANEL_LED_REMOTE_MODE;
                        ledData->intValue = 0;  // Turn off LED
                        PostDeferredCall(DeferredLEDUpdate, ledData);
                    }
                    
                    // Disable toggle when disconnected
                    UIUpdateData* toggleData = malloc(sizeof(UIUpdateData));
                    if (toggleData) {
                        toggleData->control = PANEL_TOGGLE_REMOTE_MODE;
                        toggleData->intValue = 0;  // Set to OFF
                        PostDeferredCall(DeferredToggleUpdate, toggleData);
                    }
                }
            }
            
            // Queue periodic status update if connected
            if (stats.isConnected && !PSB_QueueHasCommandType(psbQueueMgr, PSB_CMD_GET_STATUS)) {
                PSB_QueueCommandAsync(psbQueueMgr, PSB_CMD_GET_STATUS, NULL, 
                                    PSB_PRIORITY_NORMAL, PSBStatusCallback, NULL);
            }
        }
    }
    
    // Update BioLogic status
    if (STATUS_MONITOR_BIOLOGIC) {
        BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
        if (bioQueueMgr) {
            BioQueueStats stats;
            BIO_QueueGetStats(bioQueueMgr, &stats);
            
            // Determine current state
            ConnectionState currentState = stats.isConnected ? CONN_STATE_CONNECTED : CONN_STATE_ERROR;
            
            // Update UI if state changed
            if (currentState != g_status.lastBioState) {
                g_status.lastBioState = currentState;
                UpdateDeviceLED(0, currentState);
                UpdateDeviceStatus(0, stats.isConnected ? "BioLogic Connected" : "BioLogic Not Connected");
            }
        }
    }
}

/******************************************************************************
 * PSB Status Callback
 ******************************************************************************/

static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            PSBCommandResult *result, void *userData) {
    if (result->errorCode == PSB_SUCCESS) {
        PSB_Status *status = &result->data.status;
        UpdatePSBValues(status);
        
        // Update remote mode LED
        UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
        if (ledData) {
            ledData->control = PANEL_LED_REMOTE_MODE;
            ledData->intValue = status->remoteMode;
            PostDeferredCall(DeferredLEDUpdate, ledData);
        }
        
        // ALSO update the toggle to match actual state
        UIUpdateData* toggleData = malloc(sizeof(UIUpdateData));
        if (toggleData) {
            toggleData->control = PANEL_TOGGLE_REMOTE_MODE;
            toggleData->intValue = status->remoteMode;
            PostDeferredCall(DeferredToggleUpdate, toggleData);
        }
    }
}

/******************************************************************************
 * UI Update Functions
 ******************************************************************************/

static void UpdateDeviceLED(int isPSB, ConnectionState state) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (!data) return;
    
    data->control = isPSB ? PANEL_LED_PSB_STATUS : PANEL_LED_BIOLOGIC_STATUS;
    
    // Determine LED color based on state
    switch (state) {
        case CONN_STATE_IDLE:
        case CONN_STATE_ERROR:
            data->intValue = VAL_RED;
            break;
            
        case CONN_STATE_CONNECTED:
            data->intValue = VAL_GREEN;
            break;
            
        default:
            data->intValue = VAL_DK_YELLOW;
            break;
    }
    
    PostDeferredCall(DeferredLEDUpdate, data);
}

static void UpdateDeviceStatus(int isPSB, const char* message) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (!data) return;
    
    data->control = isPSB ? PANEL_STR_PSB_STATUS : PANEL_STR_BIOLOGIC_STATUS;
    SAFE_STRCPY(data->strValue, message, sizeof(data->strValue));
    
    PostDeferredCall(DeferredStatusUpdate, data);
}

static void UpdatePSBValues(PSB_Status* status) {
    // Update voltage
    UIUpdateData* vData = malloc(sizeof(UIUpdateData));
    if (vData) {
        vData->control = PANEL_NUM_VOLTAGE;
        vData->dblValue = status->voltage;
        PostDeferredCall(DeferredNumericUpdate, vData);
    }
    
    // Update current
    UIUpdateData* iData = malloc(sizeof(UIUpdateData));
    if (iData) {
        iData->control = PANEL_NUM_CURRENT;
        iData->dblValue = status->current;
        PostDeferredCall(DeferredNumericUpdate, iData);
    }
    
    // Update power
    UIUpdateData* pData = malloc(sizeof(UIUpdateData));
    if (pData) {
        pData->control = PANEL_NUM_POWER;
        pData->dblValue = status->power;
        PostDeferredCall(DeferredNumericUpdate, pData);
    }
}

/******************************************************************************
 * Deferred Callback Functions
 ******************************************************************************/

static void CVICALLBACK DeferredLEDUpdate(void* data) {
    UIUpdateData* updateData = (UIUpdateData*)data;
    if (updateData && g_status.panelHandle > 0) {
        if (updateData->control == PANEL_LED_REMOTE_MODE) {
            // Set Remote LED to green when ON
            SetCtrlAttribute(g_status.panelHandle, updateData->control, 
                            ATTR_ON_COLOR, VAL_GREEN);  
            SetCtrlVal(g_status.panelHandle, updateData->control, updateData->intValue);
        } else {
            // Device status LEDs keep their color scheme
            SetCtrlAttribute(g_status.panelHandle, updateData->control, 
                            ATTR_ON_COLOR, updateData->intValue);
            SetCtrlVal(g_status.panelHandle, updateData->control, 1);
        }
        free(updateData);
    }
}

static void CVICALLBACK DeferredStatusUpdate(void* data) {
    UIUpdateData* updateData = (UIUpdateData*)data;
    if (updateData && g_status.panelHandle > 0) {
        SetCtrlVal(g_status.panelHandle, updateData->control, updateData->strValue);
        ProcessDrawEvents();
        free(updateData);
    }
}

static void CVICALLBACK DeferredNumericUpdate(void* data) {
    UIUpdateData* updateData = (UIUpdateData*)data;
    if (updateData && g_status.panelHandle > 0) {
        SetCtrlVal(g_status.panelHandle, updateData->control, updateData->dblValue);
        free(updateData);
    }
}

static void CVICALLBACK DeferredToggleUpdate(void* data) {
    UIUpdateData* updateData = (UIUpdateData*)data;
    if (updateData && g_status.panelHandle > 0) {
        // Don't update toggle if a change is pending
        if (!g_status.remoteModeChangePending) {
            // Get current toggle value to avoid unnecessary updates
            int currentValue;
            GetCtrlVal(g_status.panelHandle, updateData->control, &currentValue);
            
            // Only update if value has changed
            if (currentValue != updateData->intValue) {
                SetCtrlVal(g_status.panelHandle, updateData->control, updateData->intValue);
            }
        }
        
        free(updateData);
    }
}

void Status_SetRemoteModeChangePending(int pending, int value) {
    g_status.remoteModeChangePending = pending;
    g_status.pendingRemoteModeValue = value;
}

int Status_IsRemoteModeChangePending(void) {
    return g_status.remoteModeChangePending;
}

void Status_UpdateRemoteLED(int isOn) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (data) {
        data->control = PANEL_LED_REMOTE_MODE;
        data->intValue = isOn;
        PostDeferredCall(DeferredLEDUpdate, data);
    }
}