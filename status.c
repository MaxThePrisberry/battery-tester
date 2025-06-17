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

// Use hardware timer instead of async timer for better compatibility
#include <toolbox.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static StatusModuleState g_status = {0};
static int g_initialized = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static void UpdateDeviceLED(int isPSB, ConnectionState state);
static void UpdateDeviceStatus(int isPSB, const char* message);
static void UpdatePSBValues(PSB_Status* status);
static int ConnectPSB(void);
static int ConnectBioLogic(void);
static void DisconnectPSB(void);
static void DisconnectBioLogic(void);
static double CalculateRetryDelay(int retryCount);
static void CVICALLBACK DeferredLEDUpdate(void* data);
static void CVICALLBACK DeferredStatusUpdate(void* data);
static void CVICALLBACK DeferredNumericUpdate(void* data);
static int CVICALLBACK Status_TimerThread(void *functionData);
static void Status_TimerUpdate(void);

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
    
    // Start discovery thread
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
    
    // Disconnect devices
    if (STATUS_MONITOR_PSB) {
        DisconnectPSB();
    }
    
    if (STATUS_MONITOR_BIOLOGIC) {
        DisconnectBioLogic();
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
    LogMessage("Status module cleaned up");
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
    
    // Connect to PSB if enabled
    if (STATUS_MONITOR_PSB && !g_status.shutdownRequested) {
        UpdateDeviceStatus(1, "Searching for PSB...");
        UpdateDeviceLED(1, CONN_STATE_DISCOVERING);
        g_status.psbStatus.state = CONN_STATE_DISCOVERING;
        
        int result = ConnectPSB();
        if (result == SUCCESS) {
            g_status.psbStatus.state = CONN_STATE_CONNECTED;
            g_status.psbStatus.retryCount = 0;
            UpdateDeviceLED(1, CONN_STATE_CONNECTED);
            UpdateDeviceStatus(1, "PSB Connected");
        } else {
            g_status.psbStatus.state = CONN_STATE_ERROR;
            g_status.psbStatus.nextRetryTime = Timer() + CalculateRetryDelay(0);
            UpdateDeviceLED(1, CONN_STATE_ERROR);
            UpdateDeviceStatus(1, "PSB Connection Failed");
        }
    }
    
    // Connect to BioLogic if enabled
    if (STATUS_MONITOR_BIOLOGIC && !g_status.shutdownRequested) {
        UpdateDeviceStatus(0, "Connecting to BioLogic...");
        UpdateDeviceLED(0, CONN_STATE_CONNECTING);
        g_status.biologicStatus.state = CONN_STATE_CONNECTING;
        
        int result = ConnectBioLogic();
        if (result == SUCCESS) {
            g_status.biologicStatus.state = CONN_STATE_CONNECTED;
            g_status.biologicStatus.retryCount = 0;
            UpdateDeviceLED(0, CONN_STATE_CONNECTED);
            UpdateDeviceStatus(0, "BioLogic Connected");
        } else {
            g_status.biologicStatus.state = CONN_STATE_ERROR;
            g_status.biologicStatus.nextRetryTime = Timer() + CalculateRetryDelay(0);
            UpdateDeviceLED(0, CONN_STATE_ERROR);
            UpdateDeviceStatus(0, "BioLogic Connection Failed");
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
            
            // Perform the status update
            Status_TimerUpdate();
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
    if (g_status.shutdownRequested) {
        return;
    }
    
    double currentTime = Timer();
    
    // Update PSB if enabled
    if (STATUS_MONITOR_PSB) {
        DeviceStatus* psbStatus = &g_status.psbStatus;
        
        switch (psbStatus->state) {
            case CONN_STATE_CONNECTED:
                // Get and update PSB values
                if (psbStatus->psbHandle != NULL) {
                    PSB_Status status;
                    int result = PSB_GetStatus(psbStatus->psbHandle, &status);
                    
                    if (result == PSB_SUCCESS) {
                        // Update UI values
                        UpdatePSBValues(&status);
                        psbStatus->lastUpdateTime = currentTime;
                        
                        // Update remote mode LED if needed
                        UIUpdateData* ledData = malloc(sizeof(UIUpdateData));
                        if (ledData) {
                            ledData->control = PANEL_LED_REMOTE_MODE;
                            ledData->intValue = status.remoteMode;
                            PostDeferredCall(DeferredLEDUpdate, ledData);
                        }
                    } else {
                        // Connection lost
                        LogError("Lost connection to PSB: %s", PSB_GetErrorString(result));
                        psbStatus->state = CONN_STATE_ERROR;
                        psbStatus->nextRetryTime = currentTime + CalculateRetryDelay(0);
                        UpdateDeviceLED(1, CONN_STATE_ERROR);
                        UpdateDeviceStatus(1, "PSB Connection Lost");
                        DisconnectPSB();
                    }
                }
                break;
                
            case CONN_STATE_ERROR:
            case CONN_STATE_RECONNECTING:
                // Check if it's time to retry
                if (currentTime >= psbStatus->nextRetryTime) {
                    psbStatus->state = CONN_STATE_RECONNECTING;
                    UpdateDeviceStatus(1, "Reconnecting to PSB...");
                    UpdateDeviceLED(1, CONN_STATE_RECONNECTING);
                    
                    int result = ConnectPSB();
                    if (result == SUCCESS) {
                        psbStatus->state = CONN_STATE_CONNECTED;
                        psbStatus->retryCount = 0;
                        UpdateDeviceLED(1, CONN_STATE_CONNECTED);
                        UpdateDeviceStatus(1, "PSB Reconnected");
                    } else {
                        psbStatus->retryCount++;
                        psbStatus->nextRetryTime = currentTime + 
                            CalculateRetryDelay(psbStatus->retryCount);
                        UpdateDeviceLED(1, CONN_STATE_ERROR);
                        char msg[MEDIUM_BUFFER_SIZE];
                        SAFE_SPRINTF(msg, sizeof(msg), "PSB Reconnect Failed (Retry %d)", 
                                     psbStatus->retryCount);
                        UpdateDeviceStatus(1, msg);
                    }
                }
                break;
        }
    }
    
    // Check BioLogic connection status (no continuous monitoring per requirements)
    if (STATUS_MONITOR_BIOLOGIC) {
        DeviceStatus* bioStatus = &g_status.biologicStatus;
        
        switch (bioStatus->state) {
            case CONN_STATE_CONNECTED:
                // Just verify connection is still alive periodically
                if (currentTime - bioStatus->lastUpdateTime > 10.0) { // Check every 10 seconds
                    if (bioStatus->biologicID >= 0) {
                        int result = BL_TestConnection(bioStatus->biologicID);
                        if (result == SUCCESS) {
                            bioStatus->lastUpdateTime = currentTime;
                        } else {
                            LogError("Lost connection to BioLogic: %s", GetErrorString(result));
                            bioStatus->state = CONN_STATE_ERROR;
                            bioStatus->nextRetryTime = currentTime + CalculateRetryDelay(0);
                            UpdateDeviceLED(0, CONN_STATE_ERROR);
                            UpdateDeviceStatus(0, "BioLogic Connection Lost");
                            DisconnectBioLogic();
                        }
                    }
                }
                break;
                
            case CONN_STATE_ERROR:
            case CONN_STATE_RECONNECTING:
                // Check if it's time to retry
                if (currentTime >= bioStatus->nextRetryTime) {
                    bioStatus->state = CONN_STATE_RECONNECTING;
                    UpdateDeviceStatus(0, "Reconnecting to BioLogic...");
                    UpdateDeviceLED(0, CONN_STATE_RECONNECTING);
                    
                    int result = ConnectBioLogic();
                    if (result == SUCCESS) {
                        bioStatus->state = CONN_STATE_CONNECTED;
                        bioStatus->retryCount = 0;
                        UpdateDeviceLED(0, CONN_STATE_CONNECTED);
                        UpdateDeviceStatus(0, "BioLogic Reconnected");
                    } else {
                        bioStatus->retryCount++;
                        bioStatus->nextRetryTime = currentTime + 
                            CalculateRetryDelay(bioStatus->retryCount);
                        UpdateDeviceLED(0, CONN_STATE_ERROR);
                        char msg[MEDIUM_BUFFER_SIZE];
                        SAFE_SPRINTF(msg, sizeof(msg), "BioLogic Reconnect Failed (Retry %d)", 
                                     bioStatus->retryCount);
                        UpdateDeviceStatus(0, msg);
                    }
                }
                break;
        }
    }
}

/******************************************************************************
 * Device Connection Functions
 ******************************************************************************/

static int ConnectPSB(void) {
    // Allocate PSB handle if needed
    if (g_status.psbStatus.psbHandle == NULL) {
        g_status.psbStatus.psbHandle = &g_status.psbDevice;
    }
    
    // Use auto-discovery to find and connect
    int result = PSB_AutoDiscover(PSB_TARGET_SERIAL, g_status.psbStatus.psbHandle);
    
    if (result == PSB_SUCCESS) {
        LogMessage("Successfully connected to PSB %s", PSB_TARGET_SERIAL);
        
        // Enable remote mode for control
        result = PSB_SetRemoteMode(g_status.psbStatus.psbHandle, 1);
        if (result != PSB_SUCCESS) {
            LogWarning("Failed to set PSB remote mode: %s", PSB_GetErrorString(result));
        }
        
        // Get initial status
        PSB_Status status;
        result = PSB_GetStatus(g_status.psbStatus.psbHandle, &status);
        if (result == PSB_SUCCESS) {
            UpdatePSBValues(&status);
            g_status.psbStatus.lastUpdateTime = Timer();
        }
        
        return SUCCESS;
    } else {
        LogError("Failed to connect to PSB: %s", PSB_GetErrorString(result));
        SAFE_STRCPY(g_status.psbStatus.lastError, PSB_GetErrorString(result), 
                    sizeof(g_status.psbStatus.lastError));
        return result;
    }
}

static int ConnectBioLogic(void) {
    // Initialize BioLogic DLL if needed
    if (!IsBioLogicInitialized()) {
        int result = InitializeBioLogic();
        if (result != SUCCESS) {
            LogError("Failed to initialize BioLogic DLL: %d", result);
            return result;
        }
    }
    
    // Connect to BioLogic
    TDeviceInfos_t deviceInfo;
    int32_t deviceID = -1;
    
    int result = BL_Connect(BIOLOGIC_DEFAULT_ADDRESS, TIMEOUT, &deviceID, &deviceInfo);
    
    if (result == SUCCESS) {
        g_status.biologicStatus.biologicID = deviceID;
        
        const char* deviceTypeName = "Unknown";
        switch(deviceInfo.DeviceCode) {
            case KBIO_DEV_SP150E: deviceTypeName = "SP-150e"; break;
            case KBIO_DEV_SP150: deviceTypeName = "SP-150"; break;
            default: break;
        }
        
        LogMessage("Connected to BioLogic %s (ID: %d)", deviceTypeName, deviceID);
        LogMessage("  Firmware Version: %d", deviceInfo.FirmwareVersion);
        LogMessage("  Channels: %d", deviceInfo.NumberOfChannels);
        
        // Test the connection
        result = BL_TestConnection(deviceID);
        if (result == SUCCESS) {
            g_status.biologicStatus.lastUpdateTime = Timer();
            return SUCCESS;
        } else {
            LogError("BioLogic connection test failed: %s", GetErrorString(result));
            BL_Disconnect(deviceID);
            g_status.biologicStatus.biologicID = -1;
            return result;
        }
    } else {
        LogError("Failed to connect to BioLogic: %s", GetErrorString(result));
        SAFE_STRCPY(g_status.biologicStatus.lastError, GetErrorString(result), 
                    sizeof(g_status.biologicStatus.lastError));
        return result;
    }
}

static void DisconnectPSB(void) {
    if (g_status.psbStatus.psbHandle != NULL && 
        g_status.psbStatus.psbHandle->isConnected) {
        
        // Disable output and remote mode before disconnecting
        PSB_SetOutputEnable(g_status.psbStatus.psbHandle, 0);
        PSB_SetRemoteMode(g_status.psbStatus.psbHandle, 0);
        PSB_Close(g_status.psbStatus.psbHandle);
        
        LogMessage("Disconnected from PSB");
    }
}

static void DisconnectBioLogic(void) {
    if (g_status.biologicStatus.biologicID >= 0) {
        BL_Disconnect(g_status.biologicStatus.biologicID);
        g_status.biologicStatus.biologicID = -1;
        LogMessage("Disconnected from BioLogic");
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
 * Utility Functions
 ******************************************************************************/

static double CalculateRetryDelay(int retryCount) {
    // Exponential backoff: base * multiplier^retryCount
    double delay = RECONNECT_BASE_DELAY_MS * pow(RECONNECT_MULTIPLIER, retryCount);
    
    // Cap at maximum delay
    if (delay > RECONNECT_MAX_DELAY_MS) {
        delay = RECONNECT_MAX_DELAY_MS;
    }
    
    return delay / 1000.0; // Convert to seconds for Timer()
}