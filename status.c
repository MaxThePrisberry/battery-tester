/******************************************************************************
 * status.c
 * 
 * Device Status Monitoring Module Implementation
 * Manages connection status and periodic updates for PSB and BioLogic devices
 ******************************************************************************/

#include "common.h"
#include "status.h"
#include "BatteryTester.h"
#include "psb10000_dll.h"
#include "biologic_dll.h"
#include "logging.h"
#include <utility.h>      // For Timer() function
#include <math.h>         // For pow() function
#include "psb10000_queue.h"
#include "biologic_queue.h"

// Use hardware timer instead of async timer for better compatibility
#include <toolbox.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static StatusModuleState g_status = {0};
static int g_initialized = 0;
static int g_statusPaused = 0;  // Added for pause/resume functionality

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static void UpdateDeviceLED(int isPSB, ConnectionState state);
static void UpdateDeviceStatus(int isPSB, const char* message);
static void UpdatePSBValues(PSB_Status* status);
static void CVICALLBACK DeferredLEDUpdate(void* data);
static void CVICALLBACK DeferredStatusUpdate(void* data);
static void CVICALLBACK DeferredNumericUpdate(void* data);
static int CVICALLBACK Status_TimerThread(void *functionData);
static void Status_TimerUpdate(void);

static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            PSBCommandResult *result, void *userData);
static void BioConnectionCallback(BioCommandID cmdId, BioCommandType type,
                                BioCommandResult *result, void *userData);

/******************************************************************************
 * Structure for deferred UI updates
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
    
    // Initialize device states
    g_status.psbStatus.state = CONN_STATE_IDLE;
    g_status.biologicStatus.state = CONN_STATE_IDLE;
    g_status.biologicStatus.biologicID = -1;
    
    // Initialize pause state
    g_statusPaused = 0;
    
    // Set initial LED states (red for enabled devices)
    if (STATUS_MONITOR_PSB) {
        UpdateDeviceLED(1, CONN_STATE_IDLE);
        UpdateDeviceStatus(1, "Initializing...");
    }
    
    if (STATUS_MONITOR_BIOLOGIC) {
        UpdateDeviceLED(0, CONN_STATE_IDLE);
        UpdateDeviceStatus(0, "Initializing...");
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
    
    // Start discovery thread - Note: PSB discovery is now handled by queue manager
    if (g_threadPool > 0) {
        int error = CmtScheduleThreadPoolFunction(g_threadPool, Status_DiscoveryThread, 
                                                  NULL, &g_status.discoveryThreadID);
        if (error != 0) {
            LogError("Failed to start discovery thread: %d", error);
            return ERR_THREAD_CREATE;
        }
    } else {
        LogError("Thread pool not available");
        return ERR_THREAD_POOL;
    }
    
    // Start timer thread for periodic updates
    g_status.timerActive = 1;
    g_status.lastTimerUpdate = Timer();
    
    int error = CmtScheduleThreadPoolFunction(g_threadPool, Status_TimerThread, 
                                              NULL, &g_status.timerThreadID);
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
    g_status.shutdownRequested = 1;
    g_status.timerActive = 0;
    
    // Wait for timer thread to complete if running
    if (g_status.timerThreadID != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_status.timerThreadID,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_status.timerThreadID = 0;
    }
    
    // Wait for discovery thread to complete if running
    if (g_status.discoveryThreadID != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_status.discoveryThreadID,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_status.discoveryThreadID = 0;
    }
    
    // No need to disconnect - queue managers handle their own cleanup
    
    LogMessage("Status monitoring stopped");
    return SUCCESS;
}

void Status_Cleanup(void) {
    if (!g_initialized) {
        return;
    }
    
    Status_Stop();
    g_initialized = 0;
    g_statusPaused = 0;
    LogMessage("Status module cleaned up");
}

int Status_Pause(void) {
    if (!g_initialized) {
        return SUCCESS;  // Not an error if not initialized
    }
    
    g_statusPaused = 1;
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
        return SUCCESS;  // Not an error if not initialized
    }
    
    g_statusPaused = 0;
    LogMessage("Status monitoring resumed");
    
    // Update UI to show current state
    if (STATUS_MONITOR_PSB) {
        if (g_status.psbStatus.state == CONN_STATE_CONNECTED) {
            UpdateDeviceStatus(1, "PSB Connected");
        } else {
            UpdateDeviceStatus(1, "PSB Monitoring");
        }
    }
    if (STATUS_MONITOR_BIOLOGIC) {
        if (g_status.biologicStatus.state == CONN_STATE_CONNECTED) {
            UpdateDeviceStatus(0, "BioLogic Connected");
        } else {
            UpdateDeviceStatus(0, "BioLogic Monitoring");
        }
    }
    
    return SUCCESS;
}

ConnectionState Status_GetDeviceState(int isPSB) {
    if (!g_initialized) {
        return CONN_STATE_IDLE;
    }
    
    return isPSB ? g_status.psbStatus.state : g_status.biologicStatus.state;
}

int Status_ForceReconnect(int isPSB) {
    if (!g_initialized) {
        return ERR_NOT_INITIALIZED;
    }
    
    DeviceStatus* deviceStatus = isPSB ? &g_status.psbStatus : &g_status.biologicStatus;
    
    // Reset retry count and state
    deviceStatus->retryCount = 0;
    deviceStatus->nextRetryTime = Timer();
    deviceStatus->state = CONN_STATE_RECONNECTING;
    
    LogMessage("Forced reconnection for %s", isPSB ? "PSB" : "BioLogic");
    return SUCCESS;
}

PSB_Handle* Status_GetPSBHandle(void) {
    if (!g_initialized || g_status.psbStatus.state != CONN_STATE_CONNECTED) {
        return NULL;
    }
    
    return g_status.psbStatus.psbHandle;
}

void Status_SetPSBHandle(PSB_Handle* handle) {
    if (!g_initialized) {
        return;
    }
    
    g_status.psbStatus.psbHandle = handle;
    
    if (handle && handle->isConnected) {
        g_status.psbStatus.state = CONN_STATE_CONNECTED;
        g_status.psbStatus.retryCount = 0;
        UpdateDeviceLED(1, CONN_STATE_CONNECTED);
        UpdateDeviceStatus(1, "PSB Connected");
        
        // Get initial status
        PSB_Status status;
        if (PSB_GetStatus(handle, &status) == PSB_SUCCESS) {
            UpdatePSBValues(&status);
        }
    } else {
        g_status.psbStatus.state = CONN_STATE_ERROR;
        UpdateDeviceLED(1, CONN_STATE_ERROR);
        UpdateDeviceStatus(1, "PSB Not Connected");
    }
}

int32_t Status_GetBioLogicID(void) {
    if (!g_initialized || g_status.biologicStatus.state != CONN_STATE_CONNECTED) {
        return -1;
    }
    
    return g_status.biologicStatus.biologicID;
}

/******************************************************************************
 * Discovery Thread Implementation
 ******************************************************************************/

int CVICALLBACK Status_DiscoveryThread(void *functionData) {
    LogMessage("Device discovery thread started");
    
    // PSB - Just check if connected via queue manager
    if (STATUS_MONITOR_PSB && !g_status.shutdownRequested) {
        PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
        if (psbQueueMgr) {
            PSBQueueStats stats;
            PSB_QueueGetStats(psbQueueMgr, &stats);
            
            if (stats.isConnected) {
                PSB_Handle *handle = PSB_QueueGetHandle(psbQueueMgr);
                if (handle) {
                    Status_SetPSBHandle(handle);
                    g_status.psbStatus.state = CONN_STATE_CONNECTED;
                    UpdateDeviceLED(1, CONN_STATE_CONNECTED);
                    UpdateDeviceStatus(1, "PSB Connected");
                    LogMessage("PSB connected via queue manager");
                }
            } else {
                g_status.psbStatus.state = CONN_STATE_ERROR;
                UpdateDeviceLED(1, CONN_STATE_ERROR);
                UpdateDeviceStatus(1, "PSB Not Connected");
            }
        }
    }
    
    // BioLogic - Just check if connected via queue manager
    if (STATUS_MONITOR_BIOLOGIC && !g_status.shutdownRequested) {
        BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
        if (bioQueueMgr) {
            BioQueueStats stats;
            BIO_QueueGetStats(bioQueueMgr, &stats);
            
            if (stats.isConnected) {
                g_status.biologicStatus.state = CONN_STATE_CONNECTED;
                // Note: You'll need to add a way to get device ID from queue manager
                UpdateDeviceLED(0, CONN_STATE_CONNECTED);
                UpdateDeviceStatus(0, "BioLogic Connected");
                LogMessage("BioLogic connected via queue manager");
            } else {
                g_status.biologicStatus.state = CONN_STATE_ERROR;
                UpdateDeviceLED(0, CONN_STATE_ERROR);
                UpdateDeviceStatus(0, "BioLogic Not Connected");
            }
        }
    }
    
    LogMessage("Device discovery thread completed");
    return 0;
}

/******************************************************************************
 * Timer Thread Implementation
 ******************************************************************************/

static int CVICALLBACK Status_TimerThread(void *functionData) {
    LogMessage("Status timer thread started");
    
    while (g_status.timerActive && !g_status.shutdownRequested) {
        double currentTime = Timer();
        double elapsedTime = currentTime - g_status.lastTimerUpdate;
        
        // Check if it's time for an update
        if (elapsedTime >= (STATUS_UPDATE_PERIOD_MS / 1000.0)) {
            g_status.lastTimerUpdate = currentTime;
            
            // Perform the status update only if not paused
            if (!g_statusPaused) {
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
 * Status Update Implementation (called by timer thread)
 ******************************************************************************/

static void Status_TimerUpdate(void) {
    if (g_status.shutdownRequested || g_statusPaused) {
        return;
    }
    
    double currentTime = Timer();
    
    // Update PSB status
    if (STATUS_MONITOR_PSB) {
        PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
        if (psbQueueMgr) {
            PSBQueueStats stats;
            PSB_QueueGetStats(psbQueueMgr, &stats);
            
            // Update connection state
            ConnectionState oldState = g_status.psbStatus.state;
            ConnectionState newState = stats.isConnected ? CONN_STATE_CONNECTED : CONN_STATE_ERROR;
            
            if (oldState != newState) {
                g_status.psbStatus.state = newState;
                UpdateDeviceLED(1, newState);
                UpdateDeviceStatus(1, stats.isConnected ? "PSB Connected" : "PSB Disconnected");
                
                if (stats.isConnected) {
                    PSB_Handle *handle = PSB_QueueGetHandle(psbQueueMgr);
                    if (handle) {
                        Status_SetPSBHandle(handle);
                    }
                }
            }
            
            // Get status if connected
            if (stats.isConnected && !PSB_QueueHasCommandType(psbQueueMgr, PSB_CMD_GET_STATUS)) {
                PSB_QueueCommandAsync(psbQueueMgr, PSB_CMD_GET_STATUS, NULL, 
                                    PSB_PRIORITY_NORMAL, PSBStatusCallback, &g_status.psbStatus);
            }
        }
    }
    
    // Update BioLogic status
    if (STATUS_MONITOR_BIOLOGIC) {
        BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
        if (bioQueueMgr) {
            BioQueueStats stats;
            BIO_QueueGetStats(bioQueueMgr, &stats);
            
            // Update connection state
            ConnectionState oldState = g_status.biologicStatus.state;
            ConnectionState newState = stats.isConnected ? CONN_STATE_CONNECTED : CONN_STATE_ERROR;
            
            if (oldState != newState) {
                g_status.biologicStatus.state = newState;
                UpdateDeviceLED(0, newState);
                UpdateDeviceStatus(0, stats.isConnected ? "BioLogic Connected" : "BioLogic Disconnected");
            }
            
            // Periodic connection test if connected
            if (stats.isConnected && 
                (currentTime - g_status.biologicStatus.lastUpdateTime > 10.0) &&
                !BIO_QueueHasCommandType(bioQueueMgr, BIO_CMD_TEST_CONNECTION)) {
                    
                BIO_QueueCommandAsync(bioQueueMgr, BIO_CMD_TEST_CONNECTION, NULL,
                                    BIO_PRIORITY_NORMAL, BioConnectionCallback, &g_status.biologicStatus);
            }
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
            
        case CONN_STATE_DISCOVERING:
        case CONN_STATE_CONNECTING:
        case CONN_STATE_RECONNECTING:
            data->intValue = VAL_DK_YELLOW;  // Dark yellow for warning/transitional states
            break;
            
        case CONN_STATE_CONNECTED:
            data->intValue = VAL_GREEN;
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
        // Set LED color first
        SetCtrlAttribute(g_status.panelHandle, updateData->control, 
                        ATTR_ON_COLOR, updateData->intValue);
        // Then turn it on
        SetCtrlVal(g_status.panelHandle, updateData->control, 1);
        
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

/******************************************************************************
 * New Async Callbacks for Status Updates
 ******************************************************************************/

// PSB status callback - just updates UI values
static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            PSBCommandResult *result, void *userData) {
    DeviceStatus *psbStatus = (DeviceStatus*)userData;
    
    if (result->errorCode == PSB_SUCCESS) {
        PSB_Status *status = &result->data.status;
        UpdatePSBValues(status);
        psbStatus->lastUpdateTime = Timer();
        
        // Update remote mode LED
        UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
        if (ledData) {
            ledData->control = PANEL_LED_REMOTE_MODE;
            ledData->intValue = status->remoteMode;
            PostDeferredCall(DeferredLEDUpdate, ledData);
        }
    }
    // No error handling - let queue manager handle reconnection
}

// BioLogic connection test callback - just updates timestamp
static void BioConnectionCallback(BioCommandID cmdId, BioCommandType type,
                                BioCommandResult *result, void *userData) {
    DeviceStatus *bioStatus = (DeviceStatus*)userData;
    
    if (result->errorCode == SUCCESS) {
        bioStatus->lastUpdateTime = Timer();
    }
    // No error handling - let queue manager handle reconnection
}