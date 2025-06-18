/******************************************************************************
 * BatteryTester.c
 * 
 * Main application file for PSB 10000 Power Supply and Bio-Logic SP-150e
 * Battery Tester with Status Monitoring Module
 ******************************************************************************/

#include "common.h"
#include "BatteryTester.h"  
#include "biologic_dll.h"
#include "psb10000_dll.h"
#include "psb10000_test.h"
#include "logging.h"
#include "status.h"

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define THREAD_POOL_SIZE        3

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
static int CVICALLBACK UpdateThread(void *functionData);
static int CVICALLBACK PSBTestSuiteThread(void *functionData);

/******************************************************************************
 * Global Variables (defined here, declared extern in common.h)
 ******************************************************************************/
int g_mainPanelHandle = 0;
int g_debugMode = 0;
CmtThreadPoolHandle g_threadPool = 0;

/******************************************************************************
 * Module-Specific Global Variables
 ******************************************************************************/
// Note: PSB handle and state are now managed by the status module
static CmtThreadFunctionID testSuiteThreadID = 0;
static int testButtonControl = 0;

// Test suite context
static TestSuiteContext testContext;
static TestState testSuiteState = TEST_STATE_IDLE;

/******************************************************************************
 * Test Suite Thread Function
 ******************************************************************************/
static int CVICALLBACK PSBTestSuiteThread(void *functionData) {
    testSuiteState = TEST_STATE_RUNNING;
    
    LogMessageEx(LOG_DEVICE_PSB, "Initializing test suite...");
    
    // Pause status monitoring to prevent concurrent access
    Status_Pause();
	
	// Wait for the status to pause completely to avoid data corruption
	Delay(UI_UPDATE_RATE_SLOW);
    
    // Get PSB handle from status module
    PSB_Handle* psb = Status_GetPSBHandle();
    if (psb == NULL) {
        // Update UI and log error
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, "PSB not connected at thread execution");
        }
        LogErrorEx(LOG_DEVICE_PSB, "PSB not connected at thread execution");
        testSuiteState = TEST_STATE_ERROR;
        
        // Resume status monitoring
        Status_Resume();
        
        // Re-enable the test button
        if (testButtonControl > 0) {
            SetCtrlAttribute(g_mainPanelHandle, testButtonControl, ATTR_DIMMED, 0);
        }
        return -1;
    }
    
    // Initialize test context (no progress callback needed since we're logging directly)
    PSB_TestSuite_Initialize(&testContext, psb, g_mainPanelHandle, PANEL_STR_PSB_STATUS);
    testContext.progressCallback = NULL;
    
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
        
        // Update UI and log success
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, finalMsg);
        }
        LogMessageEx(LOG_DEVICE_PSB, "%s", finalMsg);
    } else {
        SAFE_SPRINTF(finalMsg, sizeof(finalMsg), 
                "Test Suite FAILED: %d passed, %d failed out of %d tests.", 
                testContext.summary.passedTests,
                testContext.summary.failedTests,
                testContext.summary.totalTests);
        testSuiteState = TEST_STATE_ERROR;
        
        // Update UI and log error
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, finalMsg);
        }
        LogErrorEx(LOG_DEVICE_PSB, "%s", finalMsg);
    }
    
    // Resume status monitoring
    Status_Resume();
    
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
    LogMessage("Starting Battery Tester...");
    
    // Create thread pool
    error = CmtNewThreadPool(THREAD_POOL_SIZE, &g_threadPool);
    if (error != 0) {
        LogError("Failed to create thread pool: %d", error);
        RunUserInterface();
        DiscardPanel(g_mainPanelHandle);
        return ERR_THREAD_POOL;
    }
    
    // Initialize and start status monitoring
    error = Status_Initialize(g_mainPanelHandle);
    if (error == SUCCESS) {
        error = Status_Start();
        if (error != SUCCESS) {
            LogError("Failed to start status monitoring: %d", error);
        }
    } else {
        LogError("Failed to initialize status module: %d", error);
    }
    
    // Run the UI
    RunUserInterface();
    
    // Cleanup
    LogMessage("Shutting down Battery Tester...");
    
    // Stop and cleanup status monitoring
    Status_Cleanup();
    
    // Cancel any running test suite
    if (testSuiteState == TEST_STATE_RUNNING) {
        PSB_TestSuite_Cancel(&testContext);
        
        // Wait for test suite thread to complete
        CmtWaitForThreadPoolFunctionCompletion(g_threadPool, testSuiteThreadID, 
                                             OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
    }
    
    // Discard thread pool
    if (g_threadPool > 0) {
        CmtDiscardThreadPool(g_threadPool);
        g_threadPool = 0;
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
    
    // Check if PSB is connected through status module
    ConnectionState psbState = Status_GetDeviceState(1);
    
    if (psbState != CONN_STATE_CONNECTED || testSuiteState == TEST_STATE_RUNNING) {
        LogWarningEx(LOG_DEVICE_PSB, "Cannot change remote mode - PSB %s, test suite %s", 
                     psbState != CONN_STATE_CONNECTED ? "not connected" : "connected",
                     testSuiteState == TEST_STATE_RUNNING ? "running" : "not running");
        return 0;
    }
    
    // Get PSB handle from status module
    PSB_Handle* psb = Status_GetPSBHandle();
    if (psb == NULL) {
        LogError("PSB handle not available");
        return 0;
    }
    
    int toggleState;
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &toggleState);
    
    DEBUG_PRINT("User requesting Remote Mode: %s", toggleState ? "ON" : "OFF");
    
    int result = PSB_SetRemoteMode(psb, toggleState);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to set remote mode: %s", PSB_GetErrorString(result));
        SetCtrlVal(panel, PANEL_STR_PSB_STATUS, "Failed to set remote mode");
        SetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, !toggleState);
        return 0;
    }
    
    char statusMsg[SMALL_BUFFER_SIZE];
    SAFE_SPRINTF(statusMsg, sizeof(statusMsg), "Remote mode %s", toggleState ? "ON" : "OFF");
    SetCtrlVal(panel, PANEL_STR_PSB_STATUS, statusMsg);
    LogMessageEx(LOG_DEVICE_PSB, "%s", statusMsg);
    
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
    
    // Check if PSB is connected through status module
    ConnectionState psbState = Status_GetDeviceState(1);
    
    if (psbState != CONN_STATE_CONNECTED) {
        SetCtrlVal(panel, PANEL_STR_PSB_STATUS, "PSB not connected");
        LogErrorEx(LOG_DEVICE_PSB, "PSB not connected through status module", psbState);
        return 0;
    }
    
    if (testSuiteState == TEST_STATE_RUNNING) {
        SetCtrlVal(panel, PANEL_STR_PSB_STATUS, "Test suite already running");
        LogWarningEx(LOG_DEVICE_PSB, "Test suite already running");
        return 0;
    }
    
    // Disable test button during execution
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    testButtonControl = control;
    
    // Run test suite on background thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, PSBTestSuiteThread, NULL, &testSuiteThreadID);
    if (error != 0) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to schedule test suite thread: %d", error);
        SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
        SetCtrlVal(panel, PANEL_STR_PSB_STATUS, "Failed to start test suite");
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
    
    // Check if BioLogic is connected through status module
    ConnectionState bioState = Status_GetDeviceState(0);
    int32_t deviceID = Status_GetBioLogicID();
    
    if (bioState != CONN_STATE_CONNECTED || deviceID < 0) {
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "BioLogic not connected");
        LogErrorEx(LOG_DEVICE_BIO, "BioLogic not connected through status module");
        return 0;
    }
    
    // Disable the button to prevent multiple clicks
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    
    // Update UI
    SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Testing BioLogic connection...");
    ProcessDrawEvents();
    
    // Test the connection
    result = BL_TestConnection(deviceID);
    if (result == SUCCESS) {
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Connection test passed!");
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic connection test PASSED!");
        
        // Update LED to show success
        SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_GREEN);
        SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
    } else {
        SAFE_SPRINTF(message, sizeof(message), 
                    "Connection test failed: %s", GetErrorString(result));
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
        LogErrorEx(LOG_DEVICE_BIO, "Test Failed: %s", message);
        
        // Update LED to show error
        SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_RED);
        SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
    }
    
    // Re-enable the button
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
    
    return 0;
}