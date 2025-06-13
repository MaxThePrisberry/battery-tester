/******************************************************************************
 * Simple Battery Tester for PSB 10000 Power Supply
 * LabWindows/CVI Application with Auto-Discovery and Test Suite
 ******************************************************************************/

#include <ansi_c.h>
#include <cvirte.h>
#include <userint.h>
#include <utility.h>
#include <stdio.h>
#include "BatteryTester.h"  
#include "psb10000.h"
#include "psb10000_test.h"

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
int CVICALLBACK TestButtonCallback(int panel, int control, int event, void *callbackData,
                                   int eventData1, int eventData2);
void CVICALLBACK UpdateUI(void *callbackData);
int CVICALLBACK UpdateThread(void *functionData);
int CVICALLBACK PSBDiscoveryThread(void *functionData);
int CVICALLBACK TestSuiteThread(void *functionData);
void UpdateStatus(const char* message);
int AutoDiscoverPSB(void);

/******************************************************************************
 * Global Variables
 ******************************************************************************/
static int panelHandle = 0;
static PSB_Handle psb;
static int connected = 0;
static int discoveryComplete = 0;
static int remoteToggleInitialized = 0;
static CmtThreadPoolHandle threadPoolHandle = 0;
static CmtThreadFunctionID discoveryThreadID = 0;
static CmtThreadFunctionID testSuiteThreadID = 0;
static int testButtonControl = 0;  // Store test button control ID

// Test suite context
static TestSuiteContext testContext;
static int testSuiteRunning = 0;

// Target PSB serial number
static const char TARGET_SERIAL[] = "2872380001";

/******************************************************************************
 * Status Update Function (thread-safe)
 ******************************************************************************/
void UpdateStatus(const char* message) {
    if (panelHandle > 0) {
        SetCtrlVal(panelHandle, PANEL_STRING_STATUS, message);
        ProcessSystemEvents();
    }
}

/******************************************************************************
 * Test Suite Progress Callback
 ******************************************************************************/
void TestProgressCallback(const char *message) {
    UpdateStatus(message);
}

/******************************************************************************
 * PSB Discovery Thread Function
 ******************************************************************************/
int CVICALLBACK PSBDiscoveryThread(void *functionData) {
    UpdateStatus("Initializing PSB discovery...");
    Delay(0.5);
    
    UpdateStatus("Searching for PSB devices...");
    
    int result = PSB_AutoDiscover(TARGET_SERIAL, &psb);
    
    if (result == PSB_SUCCESS) {
	    connected = 1;
	    discoveryComplete = 1;
	    
	    // Read status once to establish communication
	    PSB_Status status;
	    PSB_GetStatus(&psb, &status);
	    Delay(0.5);
	} else {
        UpdateStatus("No PSB found. Check connections and power.");
        discoveryComplete = 1;
    }
    
    return 0;
}

/******************************************************************************
 * Test Suite Thread Function
 ******************************************************************************/
int CVICALLBACK TestSuiteThread(void *functionData) {
    testSuiteRunning = 1;
    
    UpdateStatus("Initializing test suite...");
    
    // Initialize test context
    PSB_TestSuite_Initialize(&testContext, &psb, panelHandle, PANEL_STRING_STATUS);
    testContext.progressCallback = TestProgressCallback;
    
    // Run the test suite
    int result = PSB_TestSuite_Run(&testContext);
    
    // Cleanup
    PSB_TestSuite_Cleanup(&testContext);
    
    // Final status
    char finalMsg[256];
    if (result > 0) {
        snprintf(finalMsg, sizeof(finalMsg), 
                "Test Suite PASSED! All %d tests completed successfully.", 
                testContext.summary.totalTests);
    } else {
        snprintf(finalMsg, sizeof(finalMsg), 
                "Test Suite FAILED: %d passed, %d failed out of %d tests.", 
                testContext.summary.passedTests,
                testContext.summary.failedTests,
                testContext.summary.totalTests);
    }
    UpdateStatus(finalMsg);
    
    // Re-enable the test button
    if (testButtonControl > 0) {
        SetCtrlAttribute(panelHandle, testButtonControl, ATTR_DIMMED, 0);
    }
    
    testSuiteRunning = 0;
    return 0;
}

/******************************************************************************
 * Deferred UI Update Function (runs in main thread)
 ******************************************************************************/
void CVICALLBACK UpdateUI(void *callbackData) {
    PSB_Status *status = (PSB_Status*)callbackData;
    
    if (panelHandle > 0 && !testSuiteRunning) {
        // Update voltage and current readings
        SetCtrlVal(panelHandle, PANEL_NUM_VOLTAGE, status->voltage);
        SetCtrlVal(panelHandle, PANEL_NUM_CURRENT, status->current);
        
        // Initialize toggle once
        if (!remoteToggleInitialized) {
            SetCtrlVal(panelHandle, PANEL_TOGGLE_REMOTE_MODE, status->remoteMode);
            remoteToggleInitialized = 1;
        }
        
        // Update LED
        SetCtrlVal(panelHandle, PANEL_LED_REMOTE_MODE, status->remoteMode);
        
        // Change LED color
        if (status->remoteMode) {
            SetCtrlAttribute(panelHandle, PANEL_LED_REMOTE_MODE, ATTR_ON_COLOR, VAL_GREEN);
        } else {
            SetCtrlAttribute(panelHandle, PANEL_LED_REMOTE_MODE, ATTR_ON_COLOR, VAL_RED);
        }
    }
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main(int argc, char *argv[]) {
    if (InitCVIRTE(0, argv, 0) == 0) return -1;
    
    // Load UI
    panelHandle = LoadPanel(0, "BatteryTester.uir", PANEL);
    if (panelHandle < 0) {
        printf("Failed to load UI panel\n");
        return -1;
    }
    
    // Show UI
    DisplayPanel(panelHandle);
    UpdateStatus("Starting PSB Battery Tester...");
    
    // Create thread pool
    if (CmtNewThreadPool(3, &threadPoolHandle) != 0) {
        UpdateStatus("Failed to create thread pool");
        RunUserInterface();
        DiscardPanel(panelHandle);
        return -1;
    }
    
    // Start PSB discovery
    UpdateStatus("Initializing PSB discovery...");
    CmtScheduleThreadPoolFunction(threadPoolHandle, PSBDiscoveryThread, NULL, &discoveryThreadID);
    
    // Run the UI
    RunUserInterface();
    
    // Cleanup
    connected = 0;
    
    if (testSuiteRunning) {
        PSB_TestSuite_Cancel(&testContext);
    }
    
    if (threadPoolHandle > 0) {
        // Wait for threads
        if (testSuiteRunning) {
            CmtWaitForThreadPoolFunctionCompletion(threadPoolHandle, testSuiteThreadID, 
                                                  OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        }
        CmtWaitForThreadPoolFunctionCompletion(threadPoolHandle, discoveryThreadID, 
                                              OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        CmtDiscardThreadPool(threadPoolHandle);
    }
    
    if (connected) {
        PSB_SetOutputEnable(&psb, 0);
        PSB_SetRemoteMode(&psb, 0);
        PSB_Close(&psb);
    }
    
    DiscardPanel(panelHandle);
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
    
    if (!connected || testSuiteRunning) {
        printf("ERROR: Cannot change remote mode - %s\n", 
               !connected ? "not connected" : "test suite running");
        return 0;
    }
    
    int toggleState;
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &toggleState);
    
    printf("=== User requesting Remote Mode: %s ===\n", toggleState ? "ON" : "OFF");
    
    int result = PSB_SetRemoteMode(&psb, toggleState);
    if (result != PSB_SUCCESS) {
        printf("FAILED to set remote mode: %s\n", PSB_GetErrorString(result));
        UpdateStatus("Failed to set remote mode");
        SetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, !toggleState);
        return 0;
    }
    
    char statusMsg[100];
    snprintf(statusMsg, sizeof(statusMsg), "Remote mode %s", toggleState ? "ON" : "OFF");
    UpdateStatus(statusMsg);
    
    return 0;
}

/******************************************************************************
 * Set Values Button Callback
 ******************************************************************************/
int CVICALLBACK SetValuesCallback(int panel, int control, int event, void *callbackData,
                                  int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    if (!connected || testSuiteRunning) {
        printf("ERROR: Cannot set values - %s\n", 
               !connected ? "not connected" : "test suite running");
        return 0;
    }
    
    double voltage, current;
    int remoteModeState;
    
    // Check remote mode
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &remoteModeState);
    if (!remoteModeState) {
        printf("ERROR: Remote mode must be enabled first!\n");
        UpdateStatus("Enable remote mode first");
        return 0;
    }
    
    // Get values
    GetCtrlVal(panel, PANEL_NUM_SET_VOLTAGE, &voltage);
    GetCtrlVal(panel, PANEL_NUM_SET_CURRENT, &current);
    
    printf("=== Setting PSB values: %.2fV, %.2fA ===\n", voltage, current);
    
    // Set voltage
    int result = PSB_SetVoltage(&psb, voltage);
    if (result != PSB_SUCCESS) {
        printf("Failed to set voltage: %s\n", PSB_GetErrorString(result));
        UpdateStatus("Failed to set voltage");
        return 0;
    }
    
    // Set current
    result = PSB_SetCurrent(&psb, current);
    if (result != PSB_SUCCESS) {
        printf("Failed to set current: %s\n", PSB_GetErrorString(result));
        UpdateStatus("Failed to set current");
        return 0;
    }
    
    // Enable output
    result = PSB_SetOutputEnable(&psb, 1);
    if (result != PSB_SUCCESS) {
        printf("Failed to enable output: %s\n", PSB_GetErrorString(result));
        UpdateStatus("Failed to enable output");
        return 0;
    }
    
    UpdateStatus("Values set successfully");
    printf("=== PSB configuration completed ===\n");
    
    return 0;
}

/******************************************************************************
 * Test Button Callback - Runs Test Suite
 ******************************************************************************/
int CVICALLBACK TestButtonCallback(int panel, int control, int event, void *callbackData,
                                   int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    if (!connected) {
        UpdateStatus("Not connected to PSB - cannot run tests");
        printf("ERROR: Not connected to PSB\n");
        return 0;
    }
    
    if (testSuiteRunning) {
        UpdateStatus("Test suite already running");
        return 0;
    }
    
    // Disable test button during execution
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    testButtonControl = control;  // Store control ID for later
    
    // Run test suite on background thread
    CmtScheduleThreadPoolFunction(threadPoolHandle, TestSuiteThread, NULL, &testSuiteThreadID);
    
    // Note: Button will be re-enabled when test completes
    
    return 0;
}

/******************************************************************************
 * Manual Auto-Discovery Function (kept for reference)
 ******************************************************************************/
int AutoDiscoverPSB(void) {
    printf("\n=== AUTO-DISCOVERING PSB 10000 ===\n");
    printf("Searching for PSB with serial number: %s\n", TARGET_SERIAL);
    
    int result = PSB_AutoDiscover(TARGET_SERIAL, &psb);
    
    if (result == PSB_SUCCESS) {
        printf("? Successfully connected to PSB %s\n", TARGET_SERIAL);
        return 1;
    } else {
        printf("? PSB with serial number %s not found\n", TARGET_SERIAL);
        printf("Please check:\n");
        printf("1. PSB is powered on\n");
        printf("2. USB cable is connected\n");
        printf("3. PSB appears in Device Manager\n");
        printf("4. Correct serial number: %s\n", TARGET_SERIAL);
        return 0;
    }
}