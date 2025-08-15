/******************************************************************************
 * status.c
 * 
 * Enhanced Device Status Monitoring Module Implementation
 * Monitors queue manager states with independent device timing and async updates
 * 
 * Refactored for single source of truth UI updates:
 * - Only callbacks update UI for success cases
 * - Only timeout handler updates UI for error cases
 * - Queue state only determines if commands should be sent
 ******************************************************************************/

#include "common.h"
#include "status.h"
#include "BatteryTester.h"
#include "psb10000_queue.h"
#include "biologic_queue.h"
#include "dtb4848_queue.h"
#include "logging.h"
#include "controls.h"
#include <utility.h>
#include <toolbox.h>
#include <NIDAQmx.h>

/******************************************************************************
 * Module State and Configuration
 ******************************************************************************/

// Module state tracking
static volatile StatusModuleState g_moduleState = STATUS_STATE_UNINITIALIZED;
static CmtThreadLockHandle g_stateLock = 0;

// Device type enumeration
typedef enum {
    DEVICE_PSB = 0,
    DEVICE_BIOLOGIC = 1,
    DEVICE_DTB = 2,
    DEVICE_COUNT
} DeviceType;

// Per-device state tracking
typedef struct {
    double lastUpdateTime;
    volatile bool pendingCall;
    double callStartTime;
    ConnectionState lastState;
    bool enabled;
} DeviceStatusState;

// Module variables
static struct {
    int panelHandle;
    CmtThreadFunctionID timerThreadId;
    
    // Per-device state
    DeviceStatusState devices[DEVICE_COUNT];
    
    // Remote mode change tracking (PSB specific)
    volatile int remoteModeChangePending;
    volatile int pendingRemoteModeValue;
    
    // DAQmx task handle
    TaskHandle tcTaskHandle;
} g_status = {0};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

// State management
static StatusModuleState Status_GetState(void);
static void Status_SetState(StatusModuleState newState);
static bool Status_ShouldProcessUpdates(void);

// Timer and device management
static int CVICALLBACK Status_TimerThread(void *functionData);
static void Status_ProcessDeviceUpdates(void);
static bool Status_IsDeviceReadyForUpdate(DeviceType deviceType);
static bool Status_CanSendCommand(DeviceType deviceType);
static void Status_RequestDeviceUpdate(DeviceType deviceType);

// Device-specific update functions
static void PSB_RequestStatusUpdate(void);
static void BIO_RequestStatusUpdate(void);
static void DTB_RequestStatusUpdate(void);

// Device state management
static void Status_InitializeDeviceStates(void);
static void Status_HandleDeviceTimeout(DeviceType deviceType);

// UI update functions
static void UpdateDeviceLED(DeviceType deviceType, ConnectionState state);
static void UpdateDeviceStatus(DeviceType deviceType, const char* message);
static void UpdatePSBValues(PSB_Status* status);
static void UpdateDTBValues(DTB_Status* status);

// Deferred callback functions
static void CVICALLBACK DeferredLEDUpdate(void* data);
static void CVICALLBACK DeferredStatusUpdate(void* data);
static void CVICALLBACK DeferredNumericUpdate(void* data);
static void CVICALLBACK DeferredToggleUpdate(void* data);

// Device callbacks
static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            void *result, void *userData);
static void DTBStatusCallback(CommandID cmdId, DTBCommandType type,
                            void *result, void *userData);

// Helper functions
static const char* DeviceTypeToString(DeviceType deviceType);
static const char* StateToString(StatusModuleState state);

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
 * State Management Functions
 ******************************************************************************/

static StatusModuleState Status_GetState(void) {
    StatusModuleState state;
    CmtGetLock(g_stateLock);
    state = g_moduleState;
    CmtReleaseLock(g_stateLock);
    return state;
}

static void Status_SetState(StatusModuleState newState) {
    CmtGetLock(g_stateLock);
    StatusModuleState oldState = g_moduleState;
    g_moduleState = newState;
    CmtReleaseLock(g_stateLock);
    
    LogMessage("Status module state: %s -> %s", 
               StateToString(oldState), StateToString(newState));
}

static bool Status_ShouldProcessUpdates(void) {
    StatusModuleState state = Status_GetState();
    return (state == STATUS_STATE_RUNNING);
}

/******************************************************************************
 * Public Interface Implementation
 ******************************************************************************/

int Status_Initialize(int panelHandle) {
    // Create state lock first if not already created
    if (g_stateLock == 0) {
        int result = CmtNewLock(NULL, 0, &g_stateLock);
        if (result < 0) {
            LogError("Failed to create status module state lock");
            return ERR_THREAD_SYNC;
        }
    }
    
    StatusModuleState currentState = Status_GetState();
    if (currentState != STATUS_STATE_UNINITIALIZED) {
        LogWarning("Status module already initialized (state: %s)", 
                   StateToString(currentState));
        return SUCCESS;
    }
    
    Status_SetState(STATUS_STATE_INITIALIZING);
    
    // Initialize module state
    memset(&g_status, 0, sizeof(g_status));
    g_status.panelHandle = panelHandle;
    
    // Initialize device states
    Status_InitializeDeviceStates();
    
    // Initialize DAQmx task for thermocouple if enabled
    if (ENABLE_CDAQ) {
        int32 result = DAQmxCreateTask("", &g_status.tcTaskHandle);
        if (result == 0) {
            result = DAQmxCreateAIThrmcplChan(g_status.tcTaskHandle, "cDAQ1Mod2/ai0", "", 
                                             0.0, 400.0, DAQmx_Val_DegC, DAQmx_Val_K_Type_TC, 
                                             DAQmx_Val_BuiltIn, 25.0, NULL);
            if (result != 0) {
                LogWarning("Failed to configure DAQmx thermocouple channel: %d", result);
            }
        } else {
            LogWarning("Failed to create DAQmx task: %d", result);
        }
    }
    
    // Set initial UI states to idle (yellow)
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (g_status.devices[i].enabled) {
            UpdateDeviceLED(i, CONN_STATE_IDLE);
            UpdateDeviceStatus(i, "Initializing...");
        }
    }
    
    LogMessage("Status monitoring module initialized");
    return SUCCESS;
}

int Status_Start(void) {
    StatusModuleState currentState = Status_GetState();
    if (currentState != STATUS_STATE_INITIALIZING) {
        LogError("Status module not in correct state to start (current: %s)", 
                 StateToString(currentState));
        return ERR_INVALID_STATE;
    }
    
    LogMessage("Starting device status monitoring...");
    
    // Start DAQmx task if enabled
    if (ENABLE_CDAQ && g_status.tcTaskHandle != 0) {
        int32 result = DAQmxStartTask(g_status.tcTaskHandle);
        if (result != 0) {
            LogWarning("Failed to start DAQmx task: %d", result);
        }
    }
    
    // Start timer thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, Status_TimerThread, 
                                              NULL, &g_status.timerThreadId);
    if (error != 0) {
        LogError("Failed to start status timer thread: %d", error);
        return ERR_THREAD_CREATE;
    }
    
    Status_SetState(STATUS_STATE_RUNNING);
    
    // Update initial status messages to "Monitoring..."
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (g_status.devices[i].enabled) {
            UpdateDeviceStatus(i, "Monitoring...");
        }
    }
    
    LogMessage("Status monitoring started successfully");
    return SUCCESS;
}

int Status_Stop(void) {
    StatusModuleState currentState = Status_GetState();
    if (currentState == STATUS_STATE_UNINITIALIZED || 
        currentState == STATUS_STATE_STOPPED ||
        currentState == STATUS_STATE_STOPPING) {
        return SUCCESS;
    }
    
    LogMessage("Stopping device status monitoring...");
    Status_SetState(STATUS_STATE_STOPPING);
    
    // Wait for timer thread to complete
    if (g_status.timerThreadId != 0) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_status.timerThreadId,
                                               OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        g_status.timerThreadId = 0;
    }
    
    // Stop DAQmx task if enabled
    if (ENABLE_CDAQ && g_status.tcTaskHandle != 0) {
        DAQmxStopTask(g_status.tcTaskHandle);
    }
    
    Status_SetState(STATUS_STATE_STOPPED);
    LogMessage("Status monitoring stopped");
    return SUCCESS;
}

void Status_Cleanup(void) {
    Status_Stop();
    
    StatusModuleState currentState = Status_GetState();
    if (currentState == STATUS_STATE_UNINITIALIZED) {
        return;
    }
    
    // Clean up DAQmx task
    if (ENABLE_CDAQ && g_status.tcTaskHandle != 0) {
        DAQmxClearTask(g_status.tcTaskHandle);
        g_status.tcTaskHandle = 0;
    }
    
    // Set final state
    Status_SetState(STATUS_STATE_UNINITIALIZED);
    
    // Clean up state lock
    if (g_stateLock != 0) {
        CmtDiscardLock(g_stateLock);
        g_stateLock = 0;
    }
    
    LogMessage("Status module cleaned up");
}

int Status_Pause(void) {
    StatusModuleState currentState = Status_GetState();
    if (currentState != STATUS_STATE_RUNNING) {
        return SUCCESS;
    }
    
    Status_SetState(STATUS_STATE_PAUSED);
    LogMessage("Status monitoring paused");
    
    // Update UI to show paused state
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (g_status.devices[i].enabled) {
            UpdateDeviceStatus(i, "Monitoring Paused");
        }
    }
    
    return SUCCESS;
}

int Status_Resume(void) {
    StatusModuleState currentState = Status_GetState();
    if (currentState != STATUS_STATE_PAUSED) {
        return SUCCESS;
    }
    
    Status_SetState(STATUS_STATE_RUNNING);
    LogMessage("Status monitoring resumed");
    
    // Reset device timing to force immediate updates
    double currentTime = GetTimestamp();
    for (int i = 0; i < DEVICE_COUNT; i++) {
        if (g_status.devices[i].enabled) {
            g_status.devices[i].lastUpdateTime = currentTime - (STATUS_UPDATE_PERIOD_MS / 1000.0);
            g_status.devices[i].pendingCall = false;
            UpdateDeviceStatus(i, "Resuming...");
        }
    }
    
    return SUCCESS;
}

/******************************************************************************
 * Device State Initialization
 ******************************************************************************/

static void Status_InitializeDeviceStates(void) {
    double currentTime = GetTimestamp();
    
    // Initialize PSB state
    g_status.devices[DEVICE_PSB].enabled = ENABLE_PSB;
    g_status.devices[DEVICE_PSB].lastUpdateTime = currentTime;
    g_status.devices[DEVICE_PSB].pendingCall = false;
    g_status.devices[DEVICE_PSB].lastState = CONN_STATE_IDLE;
    
    // Initialize BioLogic state
    g_status.devices[DEVICE_BIOLOGIC].enabled = ENABLE_BIOLOGIC;
    g_status.devices[DEVICE_BIOLOGIC].lastUpdateTime = currentTime;
    g_status.devices[DEVICE_BIOLOGIC].pendingCall = false;
    g_status.devices[DEVICE_BIOLOGIC].lastState = CONN_STATE_IDLE;
    
    // Initialize DTB state
    g_status.devices[DEVICE_DTB].enabled = ENABLE_DTB;
    g_status.devices[DEVICE_DTB].lastUpdateTime = currentTime;
    g_status.devices[DEVICE_DTB].pendingCall = false;
    g_status.devices[DEVICE_DTB].lastState = CONN_STATE_IDLE;
    
    LogMessage("Device states initialized");
}

/******************************************************************************
 * Timer Thread Implementation
 ******************************************************************************/

static int CVICALLBACK Status_TimerThread(void *functionData) {
    LogMessage("Status timer thread started");
    
    while (Status_GetState() != STATUS_STATE_STOPPING) {
        // Process device updates if in running state
        if (Status_ShouldProcessUpdates()) {
            Status_ProcessDeviceUpdates();
        }
        
        // Update thermocouple reading (synchronous)
        if (ENABLE_CDAQ && g_status.tcTaskHandle != 0 && Status_ShouldProcessUpdates()) {
            float64 data[1];
            int32 result = DAQmxReadAnalogF64(g_status.tcTaskHandle, 1, 10.0, 
                                             DAQmx_Val_GroupByChannel, data, 1, NULL, NULL);
            if (result == 0) {
                UIUpdateData* tcData = malloc(sizeof(UIUpdateData));
                if (tcData) {
                    tcData->control = PANEL_NUM_TC0;
                    tcData->dblValue = data[0];
                    PostDeferredCall(DeferredNumericUpdate, tcData);
                }
            }
        }
        
        // Sleep for timer interval
        Delay(0.01);  // 10ms
    }
    
    LogMessage("Status timer thread stopped");
    return 0;
}

static void Status_ProcessDeviceUpdates(void) {
    double currentTime = GetTimestamp();
    
    // Process each device independently
    for (int i = 0; i < DEVICE_COUNT; i++) {
        DeviceStatusState *device = &g_status.devices[i];
        
        if (!device->enabled) {
            continue;
        }
        
        // Check for timeout on pending calls
        if (device->pendingCall) {
            double callDuration = (currentTime - device->callStartTime) * 1000.0;
            if (callDuration > STATUS_CALLBACK_TIMEOUT_MS) {
                LogWarning("%s status callback timed out after %.1f ms", 
                          DeviceTypeToString(i), callDuration);
                Status_HandleDeviceTimeout(i);
            }
            continue; // Skip update request if call is still pending
        }
        
        // Check if device is ready for update and queue allows commands
        if (Status_IsDeviceReadyForUpdate(i) && Status_CanSendCommand(i)) {
            Status_RequestDeviceUpdate(i);
        }
    }
}

static bool Status_IsDeviceReadyForUpdate(DeviceType deviceType) {
    DeviceStatusState *device = &g_status.devices[deviceType];
    double currentTime = GetTimestamp();
    double timeSinceLastUpdate = (currentTime - device->lastUpdateTime) * 1000.0;
    
    return (timeSinceLastUpdate >= STATUS_UPDATE_PERIOD_MS && !device->pendingCall);
}

static bool Status_CanSendCommand(DeviceType deviceType) {
    // Check queue connection state to determine if we should attempt sending commands
    // This does NOT update the UI - only determines if command should be sent
    
    switch (deviceType) {
        case DEVICE_PSB: {
            PSBQueueManager *mgr = PSB_GetGlobalQueueManager();
            if (mgr) {
                PSBQueueStats stats;
                PSB_QueueGetStats(mgr, &stats);
                return stats.isConnected;
            }
            return false;
        }
        case DEVICE_BIOLOGIC: {
            BioQueueManager *mgr = BIO_GetGlobalQueueManager();
            if (mgr) {
                BioQueueStats stats;
                BIO_QueueGetStats(mgr, &stats);
                return stats.isConnected;
            }
            return false;
        }
        case DEVICE_DTB: {
            DTBQueueManager *mgr = DTB_GetGlobalQueueManager();
            if (mgr) {
                DTBQueueStats stats;
                DTB_QueueGetStats(mgr, &stats);
                return stats.isConnected;
            }
            return false;
        }
        default:
            return false;
    }
}

static void Status_RequestDeviceUpdate(DeviceType deviceType) {
    DeviceStatusState *device = &g_status.devices[deviceType];
    
    // Mark call as pending and record start time
    device->pendingCall = true;
    device->callStartTime = GetTimestamp();
    device->lastUpdateTime = device->callStartTime;
    
    // Request device-specific status update
    switch (deviceType) {
        case DEVICE_PSB:
            PSB_RequestStatusUpdate();
            break;
        case DEVICE_BIOLOGIC:
            BIO_RequestStatusUpdate();
            break;
        case DEVICE_DTB:
            DTB_RequestStatusUpdate();
            break;
        default:
            LogError("Unknown device type: %d", deviceType);
            device->pendingCall = false;
            break;
    }
}

/******************************************************************************
 * Device-Specific Status Request Functions
 ******************************************************************************/

static void PSB_RequestStatusUpdate(void) {
    int cmdId = PSB_GetStatusAsync(DEVICE_PRIORITY_LOW, PSBStatusCallback, NULL);
    if (cmdId <= 0) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to request PSB status.");
        g_status.devices[DEVICE_PSB].pendingCall = false;
    }
}

static void BIO_RequestStatusUpdate(void) {
    // BioLogic has no status functions, so update UI based on queue connection state
    g_status.devices[DEVICE_BIOLOGIC].pendingCall = false;
    
    BioQueueManager *mgr = BIO_GetGlobalQueueManager();
    if (mgr) {
        BioQueueStats stats;
        BIO_QueueGetStats(mgr, &stats);
        
        if (stats.isConnected) {
            g_status.devices[DEVICE_BIOLOGIC].lastState = CONN_STATE_CONNECTED;
            UpdateDeviceLED(DEVICE_BIOLOGIC, CONN_STATE_CONNECTED);
            UpdateDeviceStatus(DEVICE_BIOLOGIC, "BioLogic Connected");
            LogDebugEx(LOG_DEVICE_BIO, "BioLogic queue connected");
        } else {
            g_status.devices[DEVICE_BIOLOGIC].lastState = CONN_STATE_ERROR;
            UpdateDeviceLED(DEVICE_BIOLOGIC, CONN_STATE_ERROR);
            UpdateDeviceStatus(DEVICE_BIOLOGIC, "BioLogic Not Connected");
            LogDebugEx(LOG_DEVICE_BIO, "BioLogic queue disconnected");
        }
    } else {
        g_status.devices[DEVICE_BIOLOGIC].lastState = CONN_STATE_ERROR;
        UpdateDeviceLED(DEVICE_BIOLOGIC, CONN_STATE_ERROR);
        UpdateDeviceStatus(DEVICE_BIOLOGIC, "BioLogic Queue Error");
        LogErrorEx(LOG_DEVICE_BIO, "BioLogic queue manager not available");
    }
}

static void DTB_RequestStatusUpdate(void) {
    int cmdId = DTB_GetStatusAsync(DTB1_SLAVE_ADDRESS, DTBStatusCallback, NULL, DEVICE_PRIORITY_LOW);
    if (cmdId <= 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to request DTB status.");
        g_status.devices[DEVICE_DTB].pendingCall = false;
    }
}

/******************************************************************************
 * Device State Management
 ******************************************************************************/

static void Status_HandleDeviceTimeout(DeviceType deviceType) {
    DeviceStatusState *device = &g_status.devices[deviceType];
    
    // Clear pending call flag
    device->pendingCall = false;
    
    // Update connection state to error
    device->lastState = CONN_STATE_ERROR;
    UpdateDeviceLED(deviceType, CONN_STATE_ERROR);
    
    // Set appropriate error message
    char errorMsg[128];
    snprintf(errorMsg, sizeof(errorMsg), "%s Timeout", DeviceTypeToString(deviceType));
    UpdateDeviceStatus(deviceType, errorMsg);
    
    LogWarning("%s status update timed out", DeviceTypeToString(deviceType));
}

/******************************************************************************
 * Device Status Callbacks - SINGLE SOURCE OF TRUTH FOR UI UPDATES
 ******************************************************************************/

static void PSBStatusCallback(CommandID cmdId, PSBCommandType type, 
                            void *result, void *userData) {
    // Check if we should process this callback
    if (!Status_ShouldProcessUpdates()) {
        return;
    }
    
    // Clear pending call flag
    g_status.devices[DEVICE_PSB].pendingCall = false;
    
    PSBCommandResult *cmdResult = (PSBCommandResult *)result;
    if (!cmdResult) {
        LogErrorEx(LOG_DEVICE_PSB, "PSBStatusCallback: NULL result pointer");
        Status_HandleDeviceTimeout(DEVICE_PSB);
        return;
    }
    
    if (cmdResult->errorCode == PSB_SUCCESS) {
        PSB_Status *status = &cmdResult->data.status;
        
        // Update PSB measurement values
        UpdatePSBValues(status);
        
        // Update remote mode LED (separate from status LED)
        UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
        if (ledData) {
            ledData->control = PANEL_LED_REMOTE_MODE;
            ledData->intValue = status->remoteMode;
            PostDeferredCall(DeferredLEDUpdate, ledData);
        }
        
        // Determine device state based on remote mode
        ConnectionState newState = status->remoteMode ? CONN_STATE_CONNECTED : CONN_STATE_IDLE;
        g_status.devices[DEVICE_PSB].lastState = newState;
        
        // Update status LED based on remote mode
        UpdateDeviceLED(DEVICE_PSB, newState);
        
        // Update status message
        const char* statusMsg = status->remoteMode ? 
            "PSB Connected - Remote Mode" : "PSB Connected - Local Mode";
        UpdateDeviceStatus(DEVICE_PSB, statusMsg);
        
        // Update toggle to match actual state (if no change is pending)
        if (!g_status.remoteModeChangePending) {
            UIUpdateData* toggleData = malloc(sizeof(UIUpdateData));
            if (toggleData) {
                toggleData->control = PANEL_TOGGLE_REMOTE_MODE;
                toggleData->intValue = status->remoteMode;
                PostDeferredCall(DeferredToggleUpdate, toggleData);
            }
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "PSB status updated: V=%.2fV, I=%.2fA, P=%.2fW, Remote=%s",
                  status->voltage, status->current, status->power,
                  status->remoteMode ? "ON" : "OFF");
    } else {
        // Communication error - set error state
        LogErrorEx(LOG_DEVICE_PSB, "Failed to get PSB status: %s", 
                 PSB_GetErrorString(cmdResult->errorCode));
        
        g_status.devices[DEVICE_PSB].lastState = CONN_STATE_ERROR;
        UpdateDeviceLED(DEVICE_PSB, CONN_STATE_ERROR);
        UpdateDeviceStatus(DEVICE_PSB, "PSB Communication Error");
    }
}

static void DTBStatusCallback(CommandID cmdId, DTBCommandType type,
                            void *result, void *userData) {
    // Check if we should process this callback
    if (!Status_ShouldProcessUpdates()) {
        return;
    }
    
    // Clear pending call flag
    g_status.devices[DEVICE_DTB].pendingCall = false;
    
    DTBCommandResult *cmdResult = (DTBCommandResult *)result;
    if (!cmdResult) {
        LogErrorEx(LOG_DEVICE_DTB, "DTBStatusCallback: NULL result pointer");
        Status_HandleDeviceTimeout(DEVICE_DTB);
        return;
    }
    
    if (cmdResult->errorCode == DTB_SUCCESS) {
        DTB_Status *status = &cmdResult->data.status;
        
        // Update DTB measurement values
        UpdateDTBValues(status);
        
        // Update device state based on output enabled status
        ConnectionState newState = status->outputEnabled ? CONN_STATE_CONNECTED : CONN_STATE_IDLE;
        g_status.devices[DEVICE_DTB].lastState = newState;
        
        // Update status LED
        UpdateDeviceLED(DEVICE_DTB, newState);
        
        // Update status message
        const char* statusMsg = status->outputEnabled ? "DTB Running" : "DTB Connected - Stopped";
        UpdateDeviceStatus(DEVICE_DTB, statusMsg);
        
        LogDebugEx(LOG_DEVICE_DTB, "DTB status updated: Temp=%.1f°C, Output=%s",
                  status->processValue, status->outputEnabled ? "ON" : "OFF");
    } else {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to get DTB status: %s",
                 DTB_GetErrorString(cmdResult->errorCode));
        g_status.devices[DEVICE_DTB].lastState = CONN_STATE_ERROR;
        UpdateDeviceLED(DEVICE_DTB, CONN_STATE_ERROR);
        UpdateDeviceStatus(DEVICE_DTB, "DTB Error");
    }
}

/******************************************************************************
 * UI Update Functions
 ******************************************************************************/

static void UpdateDeviceLED(DeviceType deviceType, ConnectionState state) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (!data) return;
    
    switch (deviceType) {
        case DEVICE_PSB:
            data->control = PANEL_LED_PSB_STATUS;
            break;
        case DEVICE_BIOLOGIC:
            data->control = PANEL_LED_BIOLOGIC_STATUS;
            break;
        case DEVICE_DTB:
            data->control = PANEL_LED_DTB_STATUS;
            break;
        default:
            free(data);
            return;
    }
    
    // Determine LED color based on state
    switch (state) {
        case CONN_STATE_IDLE:
            data->intValue = VAL_YELLOW;  // Yellow for idle/stopped
            break;
        case CONN_STATE_ERROR:
            data->intValue = VAL_RED;      // Red for errors
            break;
        case CONN_STATE_CONNECTED:
            data->intValue = VAL_GREEN;    // Green for connected/running
            break;
        default:
            data->intValue = VAL_DK_YELLOW;
            break;
    }
    
    PostDeferredCall(DeferredLEDUpdate, data);
}

static void UpdateDeviceStatus(DeviceType deviceType, const char* message) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (!data) return;
    
    switch (deviceType) {
        case DEVICE_PSB:
            data->control = PANEL_STR_PSB_STATUS;
            break;
        case DEVICE_BIOLOGIC:
            data->control = PANEL_STR_BIOLOGIC_STATUS;
            break;
        case DEVICE_DTB:
            data->control = PANEL_STR_DTB_STATUS;
            break;
        default:
            free(data);
            return;
    }
    
    strncpy(data->strValue, message, sizeof(data->strValue) - 1);
    data->strValue[sizeof(data->strValue) - 1] = '\0';
    
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

static void UpdateDTBValues(DTB_Status* status) {
    // Update current temperature
    UIUpdateData* tempData = malloc(sizeof(UIUpdateData));
    if (tempData) {
        tempData->control = PANEL_NUM_DTB_TEMPERATURE;
        tempData->dblValue = status->processValue;
        PostDeferredCall(DeferredNumericUpdate, tempData);
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
            // Device status LEDs use color scheme based on connection state
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

/******************************************************************************
 * Remote Mode LED Update Function
 ******************************************************************************/

void Status_UpdateRemoteLED(int isOn) {
    UIUpdateData* data = malloc(sizeof(UIUpdateData));
    if (data) {
        data->control = PANEL_LED_REMOTE_MODE;
        data->intValue = isOn;
        PostDeferredCall(DeferredLEDUpdate, data);
    }
}

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static const char* DeviceTypeToString(DeviceType deviceType) {
    switch (deviceType) {
        case DEVICE_PSB: return "PSB";
        case DEVICE_BIOLOGIC: return "BioLogic";
        case DEVICE_DTB: return "DTB";
        default: return "Unknown";
    }
}

static const char* StateToString(StatusModuleState state) {
    switch (state) {
        case STATUS_STATE_UNINITIALIZED: return "UNINITIALIZED";
        case STATUS_STATE_INITIALIZING: return "INITIALIZING";
        case STATUS_STATE_RUNNING: return "RUNNING";
        case STATUS_STATE_PAUSED: return "PAUSED";
        case STATUS_STATE_STOPPING: return "STOPPING";
        case STATUS_STATE_STOPPED: return "STOPPED";
        default: return "UNKNOWN";
    }
}