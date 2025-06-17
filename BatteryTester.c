/******************************************************************************
 * BatteryTester.c
 * 
 * Main application file for PSB 10000 Power Supply and Bio-Logic SP-150e
 * Battery Tester with Auto-Discovery and Test Suite
 ******************************************************************************/

#include "common.h"
#include "BatteryTester.h"  
#include "biologic_dll.h"
#include "psb10000_dll.h"
#include "psb10000_test.h"
#include "logging.h"

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define TARGET_PSB_SERIAL       "2872380001"
#define DISCOVERY_DELAY         0.5
#define STATUS_UPDATE_DELAY     0.5
#define THREAD_POOL_SIZE        3

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
static int CVICALLBACK UpdateThread(void *functionData);
static int CVICALLBACK PSBDiscoveryThread(void *functionData);
static int CVICALLBACK TestSuiteThread(void *functionData);
static void UpdateStatus(const char* message);
static void TestProgressCallback(const char *message);

/******************************************************************************
 * Global Variables (defined here, declared extern in common.h)
 ******************************************************************************/
int g_mainPanelHandle = 0;
int g_debugMode = 0;
CmtThreadPoolHandle g_threadPool = 0;

/******************************************************************************
 * Module-Specific Global Variables
 ******************************************************************************/
static PSB_Handle psb;
static DeviceState psbState = DEVICE_STATE_DISCONNECTED;
static int discoveryComplete = 0;
static CmtThreadFunctionID discoveryThreadID = 0;
static CmtThreadFunctionID testSuiteThreadID = 0;
static int testButtonControl = 0;

// Test suite context
static TestSuiteContext testContext;
static TestState testSuiteState = TEST_STATE_IDLE;

/******************************************************************************
 * Status Update Function (thread-safe)
 ******************************************************************************/
static void UpdateStatus(const char* message) {
    if (g_mainPanelHandle > 0) {
        SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, message);
        ProcessSystemEvents();
        
        // Log with PSB device identifier
        LogMessageEx(LOG_DEVICE_PSB, "%s", message);
    }
}

/******************************************************************************
 * Test Suite Progress Callback
 ******************************************************************************/
static void TestProgressCallback(const char *message) {
    UpdateStatus(message);
}

/******************************************************************************
 * PSB Discovery Thread Function
 ******************************************************************************/
static int CVICALLBACK PSBDiscoveryThread(void *functionData) {
    psbState = DEVICE_STATE_CONNECTING;
    UpdateStatus("Initializing PSB discovery...");
    Delay(DISCOVERY_DELAY);
    
    UpdateStatus("Searching for PSB devices...");
    
    int result = PSB_AutoDiscover(TARGET_PSB_SERIAL, &psb);
    
    if (result == SUCCESS) {
        psbState = DEVICE_STATE_CONNECTED;
        discoveryComplete = 1;
        
        UpdateStatus("PSB found! Connected.");
        
        // Read status once to establish communication
        PSB_Status status;
        result = PSB_GetStatus(&psb, &status);
        if (result == SUCCESS) {
            psbState = DEVICE_STATE_READY;
            Delay(STATUS_UPDATE_DELAY);
        } else {
            LogErrorEx(LOG_DEVICE_PSB, "Failed to read initial PSB status");
        }
    } else {
        psbState = DEVICE_STATE_ERROR;
        UpdateStatus("No PSB found. Check connections and power.");
        LogErrorEx(LOG_DEVICE_PSB, "PSB discovery failed: %s", PSB_GetErrorString(result));
        discoveryComplete = 1;
    }
    
    return 0;
}

/******************************************************************************
 * Test Suite Thread Function
 ******************************************************************************/
static int CVICALLBACK TestSuiteThread(void *functionData) {
    testSuiteState = TEST_STATE_RUNNING;
    
    UpdateStatus("Initializing test suite...");
    
    // Initialize test context
    PSB_TestSuite_Initialize(&testContext, &psb, g_mainPanelHandle, PANEL_STR_PSB_STATUS);
    testContext.progressCallback = TestProgressCallback;
    
    // Run the test suite
    int result = PSB_TestSuite_Run(&testContext);
    
    // Cleanup
    PSB_TestSuite_Cleanup(&testContext);
    
    // Final status
    char finalMsg[MEDIUM_BUFFER_SIZE];
    if (result > 0) {
        SAFE_SPRINTF(finalMsg, sizeof(finalMsg), 
                "Test Suite PASSED! All %d tests completed successfully.", 
                testContext.summary.totalTests);
        testSuiteState = TEST_STATE_COMPLETED;
    } else {
        SAFE_SPRINTF(finalMsg, sizeof(finalMsg), 
                "Test Suite FAILED: %d passed, %d failed out of %d tests.", 
                testContext.summary.passedTests,
                testContext.summary.failedTests,
                testContext.summary.totalTests);
        testSuiteState = TEST_STATE_ERROR;
    }
    UpdateStatus(finalMsg);
    
    // Re-enable the test button
    if (testButtonControl > 0) {
        SetCtrlAttribute(g_mainPanelHandle, testButtonControl, ATTR_DIMMED, 0);
    }
    
    return 0;
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main(int argc, char *argv[]) {
    int error = SUCCESS;
    
    // Initialize CVI runtime
    if (InitCVIRTE(0, argv, 0) == 0) {
        return -1;
    }
    
    // Initialize logging
    LogMessage("=== Battery Tester Starting ===");
    LogMessage("Version: %s", PROJECT_VERSION);
    
    // Load UI
    g_mainPanelHandle = LoadPanel(0, "BatteryTester.uir", PANEL);
    if (g_mainPanelHandle < 0) {
        LogError("Failed to load UI panel");
        return ERR_UI - 1;
    }
    
    // Show UI
    DisplayPanel(g_mainPanelHandle);
    UpdateStatus("Starting Battery Tester...");
    
    // Create thread pool
    error = CmtNewThreadPool(THREAD_POOL_SIZE, &g_threadPool);
    if (error != 0) {
        LogError("Failed to create thread pool: %d", error);
        UpdateStatus("Failed to create thread pool");
        RunUserInterface();
        DiscardPanel(g_mainPanelHandle);
        return ERR_THREAD_POOL;
    }
    
    // Start PSB discovery
    UpdateStatus("Initializing PSB discovery...");
    error = CmtScheduleThreadPoolFunction(g_threadPool, PSBDiscoveryThread, NULL, &discoveryThreadID);
    if (error != 0) {
        LogError("Failed to schedule discovery thread: %d", error);
    }
    
    // Run the UI
    RunUserInterface();
    
    // Cleanup
    LogMessage("Shutting down Battery Tester...");
    
    // Cancel any running test suite
    if (testSuiteState == TEST_STATE_RUNNING) {
        PSB_TestSuite_Cancel(&testContext);
        
        // Wait for test suite thread to complete
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, testSuiteThreadID, 
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Wait for discovery thread if still running
    if (psbState == DEVICE_STATE_CONNECTING) {
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, discoveryThreadID, 
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Discard thread pool
    if (g_threadPool > 0) {
        CmtDiscardThreadPool(g_threadPool);
        g_threadPool = 0;
    }
    
    // Disconnect PSB if connected
    if (psbState >= DEVICE_STATE_CONNECTED) {
        LogMessageEx(LOG_DEVICE_PSB, "Disconnecting PSB...");
        PSB_SetOutputEnable(&psb, 0);
        PSB_SetRemoteMode(&psb, 0);
        PSB_Close(&psb);
    }
    
    // Cleanup BioLogic DLL if initialized
    if (IsBioLogicInitialized()) {
        CleanupBioLogic();
    }
    
    // Discard panel
    if (g_mainPanelHandle > 0) {
        DiscardPanel(g_mainPanelHandle);
    }
    
    return 0;
}

/******************************************************************************
 * Panel Callback
 ******************************************************************************/
int CVICALLBACK PanelCallback(int panel, int event, void *callbackData, 
                              int eventData1, int eventData2) {
    if (event == EVENT_CLOSE) {
        QuitUserInterface(0);
    }
    return 0;
}

/******************************************************************************
 * Remote Mode Toggle Callback
 ******************************************************************************/
int CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData,
                                 int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    if (psbState != DEVICE_STATE_READY || testSuiteState == TEST_STATE_RUNNING) {
        LogWarningEx(LOG_DEVICE_PSB, "Cannot change remote mode - PSB %s, test suite %s", 
                     psbState != DEVICE_STATE_READY ? "not ready" : "ready",
                     testSuiteState == TEST_STATE_RUNNING ? "running" : "not running");
        return 0;
    }
    
    int toggleState;
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &toggleState);
    
    DEBUG_PRINT("User requesting Remote Mode: %s", toggleState ? "ON" : "OFF");
    
    int result = PSB_SetRemoteMode(&psb, toggleState);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to set remote mode: %s", PSB_GetErrorString(result));
        UpdateStatus("Failed to set remote mode");
        SetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, !toggleState);
        return 0;
    }
    
    char statusMsg[SMALL_BUFFER_SIZE];
    SAFE_SPRINTF(statusMsg, sizeof(statusMsg), "Remote mode %s", toggleState ? "ON" : "OFF");
    UpdateStatus(statusMsg);
    
    return 0;
}

/******************************************************************************
 * Test PSB Button Callback - Runs Test Suite
 ******************************************************************************/
int CVICALLBACK TestPSBCallback(int panel, int control, int event, void *callbackData,
                                int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    if (psbState != DEVICE_STATE_READY) {
        UpdateStatus("PSB not ready - cannot run tests");
        LogErrorEx(LOG_DEVICE_PSB, "Cannot run tests - PSB state: %d", psbState);
        return 0;
    }
    
    if (testSuiteState == TEST_STATE_RUNNING) {
        UpdateStatus("Test suite already running");
        return 0;
    }
    
    // Disable test button during execution
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    testButtonControl = control;
    
    // Run test suite on background thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, TestSuiteThread, NULL, &testSuiteThreadID);
    if (error != 0) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to schedule test suite thread: %d", error);
        SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
        UpdateStatus("Failed to start test suite");
        return 0;
    }
    
    return 0;
}

/******************************************************************************
 * Test BioLogic Button Callback
 ******************************************************************************/
int CVICALLBACK TestBiologicCallback(int panel, int control, int event,
                                     void *callbackData, int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    int result;
    char message[LARGE_BUFFER_SIZE];
    int32_t deviceID = -1;
    TDeviceInfos_t deviceInfo;
    const char* deviceAddress = "USB0";
    
    // Disable the button to prevent multiple clicks
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    
    // Update UI
    SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Initializing BioLogic DLL...");
    ProcessDrawEvents();
    
    // Initialize the BioLogic DLL if needed
    if (!IsBioLogicInitialized()) {
        result = InitializeBioLogic();
        if (result != SUCCESS) {
            SAFE_SPRINTF(message, sizeof(message), 
                        "Failed to initialize BioLogic DLL. Error: %d", result);
            SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
            
            // Log error to textbox
            LogErrorEx(LOG_DEVICE_BIO, "Connection Error: %s", message);
            LogErrorEx(LOG_DEVICE_BIO, "BioLogic initialization failed: %d", result);
            
            SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
            return 0;
        }
    }
    
    // Initialize device info structure
    memset(&deviceInfo, 0, sizeof(TDeviceInfos_t));
    
    // Update status
    SAFE_SPRINTF(message, sizeof(message), "Connecting to SP-150e on %s...", deviceAddress);
    SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
    ProcessDrawEvents();
    
    // Connect to the device
    result = BL_Connect(deviceAddress, TIMEOUT, &deviceID, &deviceInfo);
    
    if (result == SUCCESS) {
        // Connection successful
        SAFE_SPRINTF(message, sizeof(message), "Connected! Device ID: %d", deviceID);
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
        ProcessDrawEvents();
        
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic connected - Device ID: %d", deviceID);
        
        // Verify device type
        const char* deviceTypeName = "Unknown";
        switch(deviceInfo.DeviceCode) {
            case KBIO_DEV_SP150E: 
                deviceTypeName = "SP-150e"; 
                break;
            case KBIO_DEV_SP150: 
                deviceTypeName = "SP-150"; 
                break;
            default: 
                deviceTypeName = "Unknown device"; 
                LogWarningEx(LOG_DEVICE_BIO, "Unknown BioLogic device code: %d", deviceInfo.DeviceCode);
                break;
        }
        
        // Display device information in textbox
        LogMessageEx(LOG_DEVICE_BIO, "=== Device Connected ===");
        LogMessageEx(LOG_DEVICE_BIO, "Connected to: %s", deviceTypeName);
        LogMessageEx(LOG_DEVICE_BIO, "Firmware Version: %d", deviceInfo.FirmwareVersion);
        LogMessageEx(LOG_DEVICE_BIO, "Channels: %d", deviceInfo.NumberOfChannels);
        LogMessageEx(LOG_DEVICE_BIO, "Firmware Date: %04d-%02d-%02d",
                     deviceInfo.FirmwareDate_yyyy,
                     deviceInfo.FirmwareDate_mm,
                     deviceInfo.FirmwareDate_dd);
        LogMessageEx(LOG_DEVICE_BIO, "=======================");
        
        // Test the connection
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Testing connection...");
        ProcessDrawEvents();
        
        result = BL_TestConnection(deviceID);
        if (result == SUCCESS) {
            SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Connection test passed!");
            LogMessageEx(LOG_DEVICE_BIO, "Connection test PASSED!");
            Delay(STATUS_UPDATE_DELAY);
        } else {
            SAFE_SPRINTF(message, sizeof(message), 
                        "Connection test failed: %s", GetErrorString(result));
            SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
            LogErrorEx(LOG_DEVICE_BIO, "Test Failed: %s", message);
        }
        
        // Always disconnect after testing
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Disconnecting...");
        ProcessDrawEvents();
        
        result = BL_Disconnect(deviceID);
        if (result == SUCCESS) {
            SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Test complete - Disconnected");
            
            // Update LED if present
            int isVisible = 0;
            GetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_VISIBLE, &isVisible);
            if (isVisible) {
                SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_GREEN);
                SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
            }
            
            LogMessageEx(LOG_DEVICE_BIO, "SUCCESS: Connection test completed successfully!");
            LogMessageEx(LOG_DEVICE_BIO, "Device has been disconnected.");
        } else {
            SAFE_SPRINTF(message, sizeof(message), 
                        "Warning: Disconnect failed! Error: %s", 
                        GetErrorString(result));
            SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
            LogErrorEx(LOG_DEVICE_BIO, "Disconnect Error: %s", message);
        }
        
    } else {
        // Connection failed
        SAFE_SPRINTF(message, sizeof(message), 
                    "Connection failed. Error %d: %s", 
                    result, GetErrorString(result));
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
        
        // Update LED if present
        int isVisible = 0;
        GetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_VISIBLE, &isVisible);
        if (isVisible) {
            SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_RED);
            SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
        }
        
        LogErrorEx(LOG_DEVICE_BIO, "Connection Error: %s", message);
        
        // Automatically scan for devices
        LogMessageEx(LOG_DEVICE_BIO, "Device not found. Scanning for available devices...");
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Scanning for devices...");
        ProcessDrawEvents();
        
        // Initialize BLFind if needed
        if (!IsBLFindInitialized()) {
            InitializeBLFind();
        }
        
        // Run scan
        ScanForBioLogicDevices();
        
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Scan complete - check output");
        LogMessageEx(LOG_DEVICE_BIO, "Device scan complete.");
        LogMessageEx(LOG_DEVICE_BIO, "Check the output above for available devices.");
        LogMessageEx(LOG_DEVICE_BIO, "Try connecting with the address shown in the scan results.");
    }
    
    // Re-enable the button
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
    
    return 0;
}