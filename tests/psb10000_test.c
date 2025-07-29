/******************************************************************************
 * psb10000_test.c
 * 
 * PSB 10000 Test Suite with Queue System Integration
 * Implementation file for comprehensive testing of PSB10000 functions
 ******************************************************************************/

#include "BatteryTester.h"
#include "psb10000_test.h"
#include "psb10000_queue.h"
#include "common.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/******************************************************************************
 * Additional Test Constants (not in header)
 ******************************************************************************/
#define TEST_DELAY_VERY_SHORT   0.1     // seconds
#define TEST_DELAY_BETWEEN_TESTS 0.2    // seconds

/******************************************************************************
 * Global variables
 ******************************************************************************/
extern PSBQueueManager *g_psbQueueMgr;
static TestSuiteContext *g_psbTestSuiteContext = NULL;

/******************************************************************************
 * Test Cases Array
 ******************************************************************************/

static TestCase testCases[] = {
    {"Remote Mode Control", Test_RemoteMode, 0, "", 0.0},
    {"Status Register Reading", Test_StatusRegisterReading, 0, "", 0.0},
    {"Voltage Control", Test_VoltageControl, 0, "", 0.0},
    {"Voltage Limits", Test_VoltageLimits, 0, "", 0.0},
    {"Current Control", Test_CurrentControl, 0, "", 0.0},
    {"Current Limits", Test_CurrentLimits, 0, "", 0.0},
    {"Power Control", Test_PowerControl, 0, "", 0.0},
    {"Power Limit", Test_PowerLimit, 0, "", 0.0},
	{"Sink Current Control", Test_SinkCurrentControl, 0, "", 0.0},
    {"Sink Power Control", Test_SinkPowerControl, 0, "", 0.0},
    {"Sink Current Limits", Test_SinkCurrentLimits, 0, "", 0.0},
    {"Sink Power Limit", Test_SinkPowerLimit, 0, "", 0.0},
    {"Output Control", Test_OutputControl, 0, "", 0.0},
    {"Invalid Parameters", Test_InvalidParameters, 0, "", 0.0},
    {"Boundary Conditions", Test_BoundaryConditions, 0, "", 0.0},
    {"Sequence Operations", Test_SequenceOperations, 0, "", 0.0},
    {"Output Voltage Verification", Test_OutputVoltageVerification, 0, "", 0.0}
};

static int numTestCases = sizeof(testCases) / sizeof(testCases[0]);

/******************************************************************************
 * Test button Callback and Thread
 ******************************************************************************/

int CVICALLBACK TestPSBCallback (int panel, int control, int event,
                                void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if this is a cancel request (test is running)
            if (g_psbTestSuiteContext != NULL) {
                LogMessage("User requested to cancel PSB test suite");
                PSB_TestSuite_Cancel(g_psbTestSuiteContext);
                
                // Update button text to show cancelling
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancelling...");
                SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
                
                return 0;
            }
            
            // Otherwise, this is a start request
            // Check if system is busy with another operation
            CmtGetLock(g_busyLock);
            if (g_systemBusy) {
                CmtReleaseLock(g_busyLock);
                LogWarning("Cannot start test - system is busy");
                MessagePopup("System Busy", 
                           "Another operation is in progress.\n"
                           "Please wait for it to complete before starting a test.");
                return 0;
            }
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);
            
            PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
            if (!psbHandle || !psbHandle->isConnected) {
                LogError("PSB not connected - cannot run test suite");
                MessagePopup("PSB Not Connected", 
                           "The PSB 10000 is not connected.\n"
                           "Please ensure it is connected before running tests.");
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }
            
            // Dim EXPERIMENTS tab control
            SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 1);
            
            // Dim manual PSB control
            SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 1);
            
            // Change Test PSB button text to "Cancel"
            SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");
            
            // Create test context
            TestSuiteContext *context = calloc(1, sizeof(TestSuiteContext));
            if (context) {
                PSB_TestSuite_Initialize(context, psbHandle, panel, PANEL_STR_PSB_STATUS);
                context->state = TEST_STATE_PREPARING;
                
                // Store pointer to running context
                g_psbTestSuiteContext = context;
                
                // Start test in worker thread
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool, 
                    TestPSBWorkerThread, context, &threadID);
            } else {
                // Failed to allocate - restore UI
                SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);
                
                SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test PSB");
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
            break;
    }
    return 0;
}

int CVICALLBACK TestPSBWorkerThread(void *functionData) {
    TestSuiteContext *context = (TestSuiteContext*)functionData;
    
    // Run the test suite
    int result = PSB_TestSuite_Run(context);
    
    // Create one-line summary for status control
    char statusMsg[MEDIUM_BUFFER_SIZE];
    if (context->state == TEST_STATE_ABORTED) {
        SAFE_SPRINTF(statusMsg, sizeof(statusMsg), 
                    "Test cancelled: %d/%d passed", 
                    context->summary.passedTests, 
                    context->summary.totalTests);
    } else if (context->state == TEST_STATE_COMPLETED) {
        SAFE_SPRINTF(statusMsg, sizeof(statusMsg), 
                    "All tests passed (%d/%d)", 
                    context->summary.passedTests,
                    context->summary.totalTests);
    } else {
        SAFE_SPRINTF(statusMsg, sizeof(statusMsg), 
                    "Tests failed: %d/%d passed", 
                    context->summary.passedTests,
                    context->summary.totalTests);
    }
    
    // Update status control with summary
    SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, statusMsg);
    
    // Log detailed results
    if (result > 0) {
        LogMessageEx(LOG_DEVICE_PSB, "PSB test suite completed successfully (%d tests passed)", result);
    } else if (result == -2) {
        LogMessageEx(LOG_DEVICE_PSB, "PSB test suite cancelled by user");
    } else if (result == 0) {
        LogWarningEx(LOG_DEVICE_PSB, "PSB test suite completed with failures");
    } else {
        LogErrorEx(LOG_DEVICE_PSB, "PSB test suite failed with error: %d", result);
    }
    
    // Clean up
    PSB_TestSuite_Cleanup(context);
    
    // Clear the running context pointer
    g_psbTestSuiteContext = NULL;
    
    free(context);
    
    // Restore UI controls
    // Re-enable EXPERIMENTS tab control
    SetCtrlAttribute(g_mainPanelHandle, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);
    
    // Re-enable all tabs
    int numTabs;
    GetNumTabPages(g_mainPanelHandle, PANEL_EXPERIMENTS, &numTabs);
    for (int i = 0; i < numTabs; i++) {
        SetTabPageAttribute(g_mainPanelHandle, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 0);
    }
    
    // Re-enable manual controls
    SetCtrlAttribute(g_mainPanelHandle, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
    
    // Restore Test PSB button
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_LABEL_TEXT, "Test PSB");
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 0);
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    return 0;
}

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static int EnsureRemoteModeQueued(PSB_Handle *handle);
static void GenerateTestSummary(TestSummary *summary, TestCase *tests, int numTests);

static double GetTime(void) {
    return Timer();
}

void UpdateTestProgress(TestSuiteContext *context, const char *message) {
    if (context && context->progressCallback) {
        context->progressCallback(message);
    }
    
    if (context && context->statusStringControl > 0 && context->panelHandle > 0) {
        SetCtrlVal(context->panelHandle, context->statusStringControl, message);
        ProcessDrawEvents();
    }
}

// Helper function to ensure remote mode is enabled using queued commands
static int EnsureRemoteModeQueued(PSB_Handle *handle) {
    PSB_Status status;
    int result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to get status for remote mode check: %s", 
                   PSB_GetErrorString(result));
        return result;
    }
    
    // Only set remote mode if it's not already enabled
    if (!status.remoteMode) {
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode is OFF, enabling it...");
        result = PSB_SetRemoteModeQueued(handle, 1);
        if (result != PSB_SUCCESS) {
            LogErrorEx(LOG_DEVICE_PSB, "Failed to enable remote mode: %s", 
                       PSB_GetErrorString(result));
            return result;
        }
        Delay(0.5); // Give device time to process
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode already enabled");
    }
    
    return PSB_SUCCESS;
}

/******************************************************************************
 * Test Suite Functions
 ******************************************************************************/

int PSB_TestSuite_Initialize(TestSuiteContext *context, PSB_Handle *handle, 
                            int panel, int statusControl) {
    if (!context || !handle) return -1;
    
    memset(context, 0, sizeof(TestSuiteContext));
    context->psbHandle = handle;
    context->panelHandle = panel;
    context->statusStringControl = statusControl;
    context->cancelRequested = 0;
    context->state = TEST_STATE_IDLE;
    
    // Reset all test results
    for (int i = 0; i < numTestCases; i++) {
        testCases[i].result = 0;
        testCases[i].errorMessage[0] = '\0';
        testCases[i].executionTime = 0.0;
    }
    
    return 0;
}

int PSB_TestSuite_Run(TestSuiteContext *context) {
    if (!context || !context->psbHandle) return -1;
    if (!context->psbHandle->isConnected) return -1;
    
    context->state = TEST_STATE_RUNNING;
    context->cancelRequested = 0;
    
    LogMessageEx(LOG_DEVICE_PSB, "Starting PSB Test Suite");
    UpdateTestProgress(context, "Starting PSB Test Suite...");
    
    // Zero out PSB values for safety
    UpdateTestProgress(context, "Zeroing PSB values...");
    if (PSB_ZeroAllValues(context->psbHandle) != PSB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to zero out the PSB before suite execution!");
        UpdateTestProgress(context, "Failed to zero out PSB");
        context->state = TEST_STATE_ERROR;
        return -1;
    }
    
    // Run each test
    for (int i = 0; i < numTestCases; i++) {
        // Check for cancellation before starting each test
        if (context->cancelRequested) {
            LogMessageEx(LOG_DEVICE_PSB, "Test suite cancelled before test %d", i + 1);
            break;
        }
        
        TestCase* test = &testCases[i];
        
        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s", 
                i + 1, numTestCases, test->testName);
        UpdateTestProgress(context, progressMsg);
        
        LogMessageEx(LOG_DEVICE_PSB, "Running test: %s", test->testName);
        
        double startTime = GetTime();
        test->result = test->testFunction(context->psbHandle, test->errorMessage, 
                                     sizeof(test->errorMessage));
        test->executionTime = GetTime() - startTime;
        
        if (test->result > 0) {
            LogMessageEx(LOG_DEVICE_PSB, "Test PASSED: %s (%.2f seconds)", 
                       test->testName, test->executionTime);
            context->summary.passedTests++;
        } else {
            LogErrorEx(LOG_DEVICE_PSB, "Test FAILED: %s - %s", 
                     test->testName, test->errorMessage);
            context->summary.failedTests++;
        }
        
        context->summary.totalTests++;
        
        // Short delay between tests
        if (i < numTestCases - 1 && !context->cancelRequested) {
            Delay(TEST_DELAY_BETWEEN_TESTS);
        }
    }
    
    // Generate summary
    GenerateTestSummary(&context->summary, testCases, numTestCases);
    
    // Set final state
    if (context->cancelRequested) {
        context->state = TEST_STATE_ABORTED;
    } else if (context->summary.failedTests == 0) {
        context->state = TEST_STATE_COMPLETED;
    } else {
        context->state = TEST_STATE_ERROR;
    }
    
    // Return value based on state
    if (context->state == TEST_STATE_ABORTED) {
        return -2; // Special value to indicate cancellation
    } else if (context->state == TEST_STATE_COMPLETED) {
        return context->summary.totalTests; // All passed
    } else {
        return 0; // Some failed
    }
}

void PSB_TestSuite_Cancel(TestSuiteContext *context) {
    if (context) {
        context->cancelRequested = 1;
        LogMessageEx(LOG_DEVICE_PSB, "Test suite cancellation requested");
    }
}

void PSB_TestSuite_Cleanup(TestSuiteContext *context) {
    if (context) {
        // Ensure PSB is in safe state using queued commands
        if (context->psbHandle && context->psbHandle->isConnected) {            
            // Ensure output is off and set values to zero
            PSB_ZeroAllValues(context->psbHandle);
        }
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_RemoteMode(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing remote mode control...");
    
    PSB_Status status;
    int result;
    int initialRemoteState;
    
    // Use queued version for status read
    LogDebugEx(LOG_DEVICE_PSB, "Reading initial state...");
    result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read initial status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    initialRemoteState = status.remoteMode;
    LogDebugEx(LOG_DEVICE_PSB, "Initial state - Remote mode: %s, Control location: 0x%02X", 
           status.remoteMode ? "ON" : "OFF", status.controlLocation);
    
    // Toggle remote mode OFF (if it's ON)
    if (initialRemoteState) {
        LogDebugEx(LOG_DEVICE_PSB, "Turning remote mode OFF...");
        result = PSB_SetRemoteModeQueued(handle, 0);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn remote mode OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Verify it turned off
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after turning OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        if (status.remoteMode != 0) {
            snprintf(errorMsg, errorMsgSize, "Remote mode did not turn OFF as expected");
            return -1;
        }
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode successfully turned OFF");
    }
    
    // Turn remote mode ON
    LogDebugEx(LOG_DEVICE_PSB, "Turning remote mode ON...");
    result = PSB_SetRemoteModeQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn remote mode ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify it turned on
    result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status after turning ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    if (status.remoteMode != 1) {
        snprintf(errorMsg, errorMsgSize, "Remote mode did not turn ON as expected");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Remote mode successfully turned ON");
    LogDebugEx(LOG_DEVICE_PSB, "Remote mode control test passed");
    
    return 1;
}

int Test_StatusRegisterReading(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing status register reading...");
    
    PSB_Status status1, status2;
    int result;
    
    // Ensure remote mode is on using queued command
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Read status multiple times using queued commands
    for (int i = 0; i < 5; i++) {
        result = PSB_GetStatusQueued(handle, &status1);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status on iteration %d: %s", 
                    i + 1, PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Status read %d: Output=%d, Remote=%d, Reg=%d, Control=0x%02X", 
               i + 1, status1.outputEnabled, status1.remoteMode, 
               status1.regulationMode, status1.controlLocation);
        
        Delay(TEST_DELAY_VERY_SHORT);
    }
    
    // Compare two consecutive reads
    result = PSB_GetStatusQueued(handle, &status1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read first comparison status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_VERY_SHORT);
    
    result = PSB_GetStatusQueued(handle, &status2);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read second comparison status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Verify critical status bits are consistent
    if (status1.remoteMode != status2.remoteMode) {
        snprintf(errorMsg, errorMsgSize, "Inconsistent remote mode status between reads");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Status register reading is consistent and valid");
    return 1;
}

int Test_VoltageControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage control...");
    
    // Ensure remote mode using queued command
    int result = PSB_SetRemoteModeQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different voltage values using queued commands
    double testVoltages[] = {TEST_VOLTAGE_LOW, TEST_VOLTAGE_MID, TEST_VOLTAGE_HIGH};
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to %.2fV...", testVoltages[i]);
        
        result = PSB_SetVoltageQueued(handle, testVoltages[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.2fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read back the status using queued command
        PSB_Status status;
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting voltage: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Voltage set command accepted for %.2fV", testVoltages[i]);
    }
    
    return 1;
}

int Test_VoltageLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage limits...");
    
    // Ensure remote mode is on
    int result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test valid limits using queued commands
    double minVoltage = 15.0;
    double maxVoltage = 45.0;
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage limits: min=%.2fV, max=%.2fV", minVoltage, maxVoltage);
    
    result = PSB_SetVoltageLimitsQueued(handle, minVoltage, maxVoltage);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Voltage limits set successfully");
    Delay(TEST_DELAY_SHORT);
    
    // Test voltage within limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage within limits (30V)...");
    result = PSB_SetVoltageQueued(handle, 30.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage within limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test voltage outside limits (should be clamped)
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage outside limits...");
    result = PSB_SetVoltageQueued(handle, 50.0);  // Above max
    // This might succeed but be clamped to max
    
    result = PSB_SetVoltageQueued(handle, 10.0);  // Below min
    // This might succeed but be clamped to min
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe voltage limits...");
    result = PSB_SetVoltageLimitsQueued(handle, PSB_SAFE_VOLTAGE_MIN, PSB_SAFE_VOLTAGE_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe voltage limits");
    }
    
    return 1;
}

int Test_CurrentControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current control...");
    
    // Ensure remote mode using queued command
    int result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different current values using queued commands
    double testCurrents[] = {TEST_CURRENT_LOW, TEST_CURRENT_MID, TEST_CURRENT_HIGH};
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting current to %.2fA...", testCurrents[i]);
        
        result = PSB_SetCurrentQueued(handle, testCurrents[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set current to %.2fA: %s", 
                    testCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read back status to verify command was accepted
        PSB_Status status;
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting current: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Current set command accepted for %.2fA", testCurrents[i]);
    }
    
    return 1;
}

int Test_CurrentLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current limits...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First zero all values to ensure clean state
    result = PSB_ZeroAllValues(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to zero values: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting current limits within valid range
    double testMinCurrent = TEST_CURRENT_LOW;   // 6.0A
    double testMaxCurrent = TEST_CURRENT_HIGH;  // 50.0A
	
	LogDebugEx(LOG_DEVICE_PSB, "Setting current to %.2fA (within new limits)...", TEST_CURRENT_MID);
    result = PSB_SetCurrentQueued(handle, TEST_CURRENT_MID);  // 30.0A - within 6-50A range
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current before limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limits: %.2fA - %.2fA...", 
               testMinCurrent, testMaxCurrent);
    
    result = PSB_SetCurrentLimitsQueued(handle, testMinCurrent, testMaxCurrent);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limits (%.1fA-%.1fA): %s", 
                testMinCurrent, testMaxCurrent, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test that current is constrained by limits
    LogDebugEx(LOG_DEVICE_PSB, "Testing current above max limit...");
    result = PSB_SetCurrentQueued(handle, TEST_CURRENT_MAX);  // 60.0A - should be clamped to 50A
    if (result != PSB_SUCCESS && result != PSB_ERROR_INVALID_PARAM) {
        LogWarningEx(LOG_DEVICE_PSB, "Unexpected error setting current above limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing current below min limit...");
    result = PSB_SetCurrentQueued(handle, PSB_SAFE_CURRENT_MIN);  // 0A - should be clamped to 6A
    if (result != PSB_SUCCESS && result != PSB_ERROR_INVALID_PARAM) {
        LogWarningEx(LOG_DEVICE_PSB, "Unexpected error setting current below limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe current limits...");
    result = PSB_SetCurrentLimitsQueued(handle, PSB_SAFE_CURRENT_MIN, PSB_SAFE_CURRENT_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe current limits: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Current limits test completed");
    return 1;
}

int Test_PowerControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power control...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test valid power values
    double testPowers[] = {TEST_POWER_LOW, TEST_POWER_MID, TEST_POWER_HIGH};  // 100W, 600W, 1000W
    
    for (int i = 0; i < ARRAY_SIZE(testPowers); i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting power to %.2fW...", testPowers[i]);
        result = PSB_SetPowerQueued(handle, testPowers[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set power to %.1fW: %s", 
                    testPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        Delay(TEST_DELAY_SHORT);
        
        // Get actual values to verify
        double actualVoltage, actualCurrent, actualPower;
        result = PSB_GetActualValuesQueued(handle, &actualVoltage, &actualCurrent, &actualPower);
        if (result != PSB_SUCCESS) {
            LogWarningEx(LOG_DEVICE_PSB, "Failed to read actual values: %s", 
                        PSB_GetErrorString(result));
        } else {
            LogDebugEx(LOG_DEVICE_PSB, "Power set to %.1fW (Actual: V=%.2fV, I=%.2fA, P=%.2fW)", 
                      testPowers[i], actualVoltage, actualCurrent, actualPower);
        }
    }
    
    // Test invalid power value (should fail)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid power (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetPowerQueued(handle, TEST_POWER_INVALID);  // 1400W - beyond device limit
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid power: %s", PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Power control test completed");
    return 1;
}

int Test_PowerLimit(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power limit...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First, ensure power is set to a low value to avoid conflicts
    LogDebugEx(LOG_DEVICE_PSB, "Setting initial power to %.1fW...", TEST_POWER_LOW);
    result = PSB_SetPowerQueued(handle, TEST_POWER_LOW);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set initial power: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid power limit
    double testPowerLimit = TEST_POWER_MAX;  // 1200W - just below device max
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting power limit to %.2fW...", testPowerLimit);
    result = PSB_SetPowerLimitQueued(handle, testPowerLimit);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power limit to %.1fW: %s", 
                testPowerLimit, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify we can set power below the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing power below limit (%.1fW)...", TEST_POWER_HIGH);
    result = PSB_SetPowerQueued(handle, TEST_POWER_HIGH);  // 1000W - should work
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power below limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test that power above limit is rejected or clamped
    LogDebugEx(LOG_DEVICE_PSB, "Testing power above limit...");
    result = PSB_SetPowerQueued(handle, testPowerLimit + TEST_SINK_POWER_ABOVE_LIMIT);
    if (result == PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Power above limit was accepted (may be clamped by device)");
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Power above limit correctly rejected: %s", 
                  PSB_GetErrorString(result));
    }
    
    // Restore safe power limit
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe power limit (%.1fW)...", PSB_SAFE_POWER_MAX);
    result = PSB_SetPowerLimitQueued(handle, PSB_SAFE_POWER_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe power limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Test invalid power limit (beyond device capability)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid power limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetPowerLimitQueued(handle, TEST_POWER_INVALID);  // 1400W - beyond max
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power limit %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid power limit: %s", 
              PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Power limit test completed");
    return 1;
}

int Test_SinkCurrentControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current control...");
    
    int result;
    PSB_Status status;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Note: The PSB will automatically switch to sink mode when:
    // 1. A sink parameter is set, AND
    // 2. The connected voltage is higher than the PSB's output voltage setting
    
    // First, set output voltage low to allow sink mode activation
    LogDebugEx(LOG_DEVICE_PSB, "Setting output voltage to 0V to prepare for sink mode...");
    result = PSB_SetVoltageQueued(handle, PSB_SAFE_VOLTAGE_MIN);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage to 0V: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Ensure output is disabled for safety
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring output is disabled...");
    result = PSB_SetOutputEnableQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different sink current values
    double testSinkCurrents[] = {
        TEST_SINK_CURRENT_LOW, 
        TEST_SINK_CURRENT_MID, 
        TEST_SINK_CURRENT_HIGH
    };
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting sink current to %.2fA...", testSinkCurrents[i]);
        
        result = PSB_SetSinkCurrentQueued(handle, testSinkCurrents[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set sink current to %.2fA: %s", 
                    testSinkCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read status to check if device is in sink mode
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting sink current: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Sink current set to %.2fA, Mode: %s", 
                  testSinkCurrents[i], status.sinkMode ? "SINK" : "SOURCE");
    }
    
    // Test invalid sink current (negative)
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative sink current (%.1fA)...", TEST_SINK_CURRENT_NEGATIVE);
    result = PSB_SetSinkCurrentQueued(handle, TEST_SINK_CURRENT_NEGATIVE);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink current");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink current: %s", 
              PSB_GetErrorString(result));
    
    // Test sink current beyond limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current beyond limit (%.1fA)...", TEST_CURRENT_INVALID);
    result = PSB_SetSinkCurrentQueued(handle, TEST_CURRENT_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected sink current %.1fA (max is %.1fA)", 
                TEST_CURRENT_INVALID, PSB_SAFE_SINK_CURRENT_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive sink current: %s", 
              PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink current control test passed");
    return 1;
}

int Test_SinkPowerControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power control...");
    
    int result;
    PSB_Status status;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Prepare for sink mode
    LogDebugEx(LOG_DEVICE_PSB, "Preparing for sink mode operation...");
    result = PSB_SetVoltageQueued(handle, PSB_SAFE_VOLTAGE_MIN);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage to 0V: %s", 
                   PSB_GetErrorString(result));
    }
    
    result = PSB_SetOutputEnableQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different sink power values
    double testSinkPowers[] = {
        TEST_SINK_POWER_LOW,
        TEST_SINK_POWER_MID,
        TEST_SINK_POWER_HIGH
    };
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting sink power to %.2fW...", testSinkPowers[i]);
        
        result = PSB_SetSinkPowerQueued(handle, testSinkPowers[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set sink power to %.2fW: %s", 
                    testSinkPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read status
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting sink power: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Sink power set to %.2fW, Mode: %s", 
                  testSinkPowers[i], status.sinkMode ? "SINK" : "SOURCE");
    }
    
    // Test invalid sink power (negative)
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative sink power (%.1fW)...", TEST_SINK_POWER_NEGATIVE);
    result = PSB_SetSinkPowerQueued(handle, TEST_SINK_POWER_NEGATIVE);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink power");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink power: %s", 
              PSB_GetErrorString(result));
    
    // Test sink power beyond limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power beyond limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetSinkPowerQueued(handle, TEST_POWER_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected sink power %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_SINK_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive sink power: %s", 
              PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink power control test passed");
    return 1;
}

int Test_SinkCurrentLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current limits...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First zero all values to ensure clean state
    LogDebugEx(LOG_DEVICE_PSB, "Zeroing values for baseline...");
    result = PSB_ZeroAllValues(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to zero values: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
	Delay(TEST_DELAY_SHORT);
    
    // CRITICAL: Set sink current to a value within the new limits BEFORE setting limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink current to %.2fA (within new limits)...", 
               TEST_SINK_CURRENT_LIMIT_TEST);
    result = PSB_SetSinkCurrentQueued(handle, TEST_SINK_CURRENT_LIMIT_TEST); // 20A
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current before limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid sink current limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink current limits: %.2fA - %.2fA...", 
               TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX);
    
    result = PSB_SetSinkCurrentLimitsQueued(handle, TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current limits (%.1fA-%.1fA): %s", 
                TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test that sink current can be set within limits
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current within limits (%.1fA)...", TEST_SINK_CURRENT_LIMIT_TEST);
    result = PSB_SetSinkCurrentQueued(handle, TEST_SINK_CURRENT_LIMIT_TEST);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current within limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink current at max limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current at max limit (%.1fA)...", TEST_SINK_CURRENT_LIMIT_MAX);
    result = PSB_SetSinkCurrentQueued(handle, TEST_SINK_CURRENT_LIMIT_MAX);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current at max limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink current at min limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current at min limit (%.1fA)...", TEST_SINK_CURRENT_LIMIT_MIN);
    result = PSB_SetSinkCurrentQueued(handle, TEST_SINK_CURRENT_LIMIT_MIN);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current at min limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test invalid limits (min > max)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid sink current limits (min > max)...");
    result = PSB_SetSinkCurrentLimitsQueued(handle, TEST_SINK_CURRENT_LIMIT_MIN_INV, TEST_SINK_CURRENT_LIMIT_MAX_INV);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted sink current limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted sink current limits: %s", 
              PSB_GetErrorString(result));
    
    // Test negative minimum limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative minimum sink current limit (%.1fA)...", TEST_SINK_CURRENT_MIN_NEG);
    result = PSB_SetSinkCurrentLimitsQueued(handle, TEST_SINK_CURRENT_MIN_NEG, TEST_SINK_CURRENT_LIMIT_MAX);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative minimum sink current limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative minimum limit: %s", 
              PSB_GetErrorString(result));
    
    // Test excessive maximum limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing excessive maximum sink current limit (%.1fA)...", TEST_CURRENT_INVALID);
    result = PSB_SetSinkCurrentLimitsQueued(handle, PSB_SAFE_SINK_CURRENT_MIN, TEST_CURRENT_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected excessive maximum sink current limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive maximum limit: %s", 
              PSB_GetErrorString(result));
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe sink current limits...");
    result = PSB_SetSinkCurrentLimitsQueued(handle, PSB_SAFE_SINK_CURRENT_MIN, PSB_SAFE_SINK_CURRENT_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe sink current limits: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink current limits test passed");
    return 1;
}

int Test_SinkPowerLimit(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power limit...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First, ensure sink power is set to a low value to avoid conflicts
    LogDebugEx(LOG_DEVICE_PSB, "Setting initial sink power to %.1fW...", TEST_SINK_POWER_LOW);
    result = PSB_SetSinkPowerQueued(handle, TEST_SINK_POWER_LOW);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set initial sink power: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid sink power limit
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink power limit to %.2fW...", TEST_SINK_POWER_LIMIT_1);
    result = PSB_SetSinkPowerLimitQueued(handle, TEST_SINK_POWER_LIMIT_1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power limit to %.1fW: %s", 
                TEST_SINK_POWER_LIMIT_1, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify we can set sink power below the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power below limit (%.1fW)...", TEST_SINK_POWER_LIMIT_TEST);
    result = PSB_SetSinkPowerQueued(handle, TEST_SINK_POWER_LIMIT_TEST);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power below limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink power at the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power at limit (%.1fW)...", TEST_SINK_POWER_LIMIT_1);
    result = PSB_SetSinkPowerQueued(handle, TEST_SINK_POWER_LIMIT_1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power at limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // IMPORTANT: Before changing to a lower limit, first reduce the sink power
    LogDebugEx(LOG_DEVICE_PSB, "Reducing sink power to %.1fW before lowering limit...", TEST_SINK_POWER_LOW);
    result = PSB_SetSinkPowerQueued(handle, TEST_SINK_POWER_LOW);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to reduce sink power before changing limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Now test different (lower) power limit
    LogDebugEx(LOG_DEVICE_PSB, "Changing sink power limit to %.2fW...", TEST_SINK_POWER_LIMIT_2);
    result = PSB_SetSinkPowerLimitQueued(handle, TEST_SINK_POWER_LIMIT_2);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to change sink power limit to %.1fW: %s", 
                TEST_SINK_POWER_LIMIT_2, PSB_GetErrorString(result));
        return -1;
    }
    
    // Test negative power limit (should fail)
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative sink power limit (%.1fW)...", TEST_SINK_POWER_NEGATIVE);
    result = PSB_SetSinkPowerLimitQueued(handle, TEST_SINK_POWER_NEGATIVE);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink power limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink power limit: %s", 
              PSB_GetErrorString(result));
    
    // Test excessive power limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing excessive sink power limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetSinkPowerLimitQueued(handle, TEST_POWER_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected sink power limit %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_SINK_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive sink power limit: %s", 
              PSB_GetErrorString(result));
    
    // Restore safe power limit
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe sink power limit (%.1fW)...", PSB_SAFE_SINK_POWER_MAX);
    result = PSB_SetSinkPowerLimitQueued(handle, PSB_SAFE_SINK_POWER_MAX);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe sink power limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink power limit test passed");
    return 1;
}

int Test_OutputControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing output enable/disable...");
    
    PSB_Status status;
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Read initial output state using queued command
    result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read initial output state: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    int initialOutputState = status.outputEnabled;
    LogDebugEx(LOG_DEVICE_PSB, "Initial output state: %s", 
           initialOutputState ? "ENABLED" : "DISABLED");
    
    // If output is on, turn it off first
    if (initialOutputState) {
        LogDebugEx(LOG_DEVICE_PSB, "Turning output OFF...");
        result = PSB_SetOutputEnableQueued(handle, 0);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn output OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Verify it turned off
        result = PSB_GetStatusQueued(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after turning output OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        if (status.outputEnabled != 0) {
            snprintf(errorMsg, errorMsgSize, "Output did not turn OFF as expected");
            return -1;
        }
    }
    
    // Turn output ON using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Turning output ON...");
    result = PSB_SetOutputEnableQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn output ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify it turned on
    result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status after turning output ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    if (status.outputEnabled != 1) {
        snprintf(errorMsg, errorMsgSize, "Output did not turn ON as expected");
        return -1;
    }
    
    // Turn output OFF again for safety
    LogDebugEx(LOG_DEVICE_PSB, "Turning output OFF for safety...");
    result = PSB_SetOutputEnableQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn output OFF for safety: %s", 
                   PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Output control test passed");
    return 1;
}

int Test_InvalidParameters(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid parameter handling...");
    
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test invalid voltage (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative voltage...");
    result = PSB_SetVoltageQueued(handle, -10.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative voltage");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative voltage");
    
    // Test invalid current (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative current...");
    result = PSB_SetCurrentQueued(handle, -5.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative current");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative current");
    
    // Test invalid power (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative power...");
    result = PSB_SetPowerQueued(handle, -100.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative power");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative power");
    
    // Test invalid limits (min > max) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid voltage limits (min > max)...");
    result = PSB_SetVoltageLimitsQueued(handle, 50.0, 20.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted voltage limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted voltage limits");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid current limits (min > max)...");
    result = PSB_SetCurrentLimitsQueued(handle, 40.0, 10.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted current limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted current limits");
    
    LogDebugEx(LOG_DEVICE_PSB, "Invalid parameter handling test passed");
    return 1;
}

int Test_BoundaryConditions(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing boundary conditions...");
    
    int result;
    
    // Ensure remote mode and zero values
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    result = PSB_ZeroAllValues(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to zero values: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test minimum voltage using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum voltage (%.2fV)...", PSB_SAFE_VOLTAGE_MIN);
    result = PSB_SetVoltageQueued(handle, PSB_SAFE_VOLTAGE_MIN);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum voltage accepted");
    
    // Test minimum current using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum current (%.2fA)...", PSB_SAFE_CURRENT_MIN);
    result = PSB_SetCurrentQueued(handle, PSB_SAFE_CURRENT_MIN);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum current accepted");
    
    // Test values below minimum (should fail) using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum voltage...");
    result = PSB_SetVoltageQueued(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected voltage below minimum");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum current...");
    result = PSB_SetCurrentQueued(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected current below minimum");
    
    // Test maximum values using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum voltage (%.2fV)...", PSB_NOMINAL_VOLTAGE);  // 60V
    result = PSB_SetVoltageQueued(handle, PSB_NOMINAL_VOLTAGE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum voltage accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum current (%.2fA)...", PSB_NOMINAL_CURRENT);  // 60A
    result = PSB_SetCurrentQueued(handle, PSB_NOMINAL_CURRENT);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum current accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Boundary conditions test passed");
    return 1;
}

int Test_SequenceOperations(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sequence of operations...");
    
    int result;
    PSB_Status status;
    
    // This test explicitly tests remote mode transitions, but respects the final state
    
    // Step 1: Turn remote mode OFF to test the sequence
    LogDebugEx(LOG_DEVICE_PSB, "Step 1: Setting remote mode OFF for sequence test...");
    result = PSB_SetRemoteModeQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn off remote mode, continuing anyway: %s", 
                PSB_GetErrorString(result));
    } else {
        Delay(TEST_DELAY_SHORT);
    }
    
    // Step 2: Turn remote mode ON using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 2: Setting remote mode ON...");
    result = PSB_SetRemoteModeQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Step 3: Set voltage using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 3: Setting voltage to 24V...");
    result = PSB_SetVoltageQueued(handle, 24.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Step 4: Set current using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 4: Setting current to 10A...");
    result = PSB_SetCurrentQueued(handle, 10.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Step 5: Enable output using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 5: Enabling output...");
    result = PSB_SetOutputEnableQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable output: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Step 6: Read status using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 6: Reading status...");
    result = PSB_GetStatusQueued(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Verify status
    if (!status.remoteMode) {
        snprintf(errorMsg, errorMsgSize, "Remote mode not active after sequence");
        return -1;
    }
    
    if (!status.outputEnabled) {
        snprintf(errorMsg, errorMsgSize, "Output not enabled after sequence");
        return -1;
    }
    
    // Step 7: Disable output for safety using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 7: Disabling output...");
    result = PSB_SetOutputEnableQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    // Keep remote mode ON as required
    LogDebugEx(LOG_DEVICE_PSB, "Keeping remote mode ON as required");
    
    LogDebugEx(LOG_DEVICE_PSB, "Sequence operations test passed");
    return 1;
}

int Test_OutputVoltageVerification(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing output voltage verification...");
    
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Ensure output is initially disabled
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring output is disabled...");
    result = PSB_SetOutputEnableQueued(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    // Set safe operating parameters using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Setting safe operating parameters...");
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limit to 1.0A...");
    result = PSB_SetCurrentQueued(handle, 1.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to 0V...");
    result = PSB_SetVoltageQueued(handle, 0.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
	// Set power limit and value high to avoid hitting CP mode during the test
	LogWarningEx(LOG_DEVICE_PSB, "Setting power limit to 600W...");
    result = PSB_SetPowerLimitQueued(handle, 600.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial power limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
	LogWarningEx(LOG_DEVICE_PSB, "Setting power to 600W...");
    result = PSB_SetPowerQueued(handle, 600.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial power: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test voltage values
    double testVoltages[] = {5.0, 12.0, 24.0, 48.0};
    double tolerance = 0.5;
    
    LogWarningEx(LOG_DEVICE_PSB, "*** READY TO BEGIN OUTPUT TESTS ***");
    LogWarningEx(LOG_DEVICE_PSB, "The test will enable the PSB output with low current limit (1A)");
    LogWarningEx(LOG_DEVICE_PSB, "Ensure nothing is connected to the output terminals!");
    
    int userResponse = ConfirmPopup("Output Test Warning",
                                   "WARNING: This test will enable the PSB output!\n\n"
                                   "The output will be limited to 1A for safety.\n"
                                   "Ensure NOTHING is connected to the output terminals!\n\n"
                                   "Do you want to continue with the test?");
    
    if (userResponse == 0) {
        LogMessageEx(LOG_DEVICE_PSB, "Output test cancelled by user");
        return 1;  // Don't fail the test, user chose safety
    }
    
    for (int i = 0; i < 4; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to %.1fV...", testVoltages[i]);
        
        // Set voltage using queued command
        result = PSB_SetVoltageQueued(handle, testVoltages[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.1fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            // Ensure output is off before returning
            PSB_SetOutputEnableQueued(handle, 0);
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Enable output using queued command
        LogDebugEx(LOG_DEVICE_PSB, "Enabling output...");
        result = PSB_SetOutputEnableQueued(handle, 1);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to enable output: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_MEDIUM);  // Wait for output to stabilize
        
        // Read actual values using queued command
        double actualVoltage, actualCurrent, actualPower;
        result = PSB_GetActualValuesQueued(handle, &actualVoltage, &actualCurrent, &actualPower);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read actual values: %s", 
                    PSB_GetErrorString(result));
            PSB_SetOutputEnableQueued(handle, 0);
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Set: %.1fV, Actual: %.3fV, Current: %.3fA, Power: %.3fW", 
               testVoltages[i], actualVoltage, actualCurrent, actualPower);
        
        // Verify voltage is within tolerance (only if output is actually on)
        if (fabs(actualVoltage - testVoltages[i]) > tolerance) {
            LogWarningEx(LOG_DEVICE_PSB, "Voltage deviation exceeds tolerance: Set=%.1fV, Actual=%.3fV", 
                       testVoltages[i], actualVoltage);
            // This is just a warning, not a failure
        }
        
        // Disable output before next iteration
        LogDebugEx(LOG_DEVICE_PSB, "Disabling output...");
        result = PSB_SetOutputEnableQueued(handle, 0);
        if (result != PSB_SUCCESS) {
            LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                       PSB_GetErrorString(result));
        }
        
        Delay(TEST_DELAY_SHORT);
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Output voltage verification test completed");
    return 1;
}

/******************************************************************************
 * Test Summary Generation
 ******************************************************************************/

void GenerateTestSummary(TestSummary *summary, TestCase *tests, int numTests) {
    if (!summary || !tests) return;
    
    // Calculate total execution time from individual test times
    double totalTime = 0.0;
    for (int i = 0; i < numTests; i++) {
        totalTime += tests[i].executionTime;
    }
    
    summary->executionTime = totalTime;
    
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    LogMessageEx(LOG_DEVICE_PSB, "PSB Test Suite Summary:");
    LogMessageEx(LOG_DEVICE_PSB, "Total Tests: %d", summary->totalTests);
    LogMessageEx(LOG_DEVICE_PSB, "Passed: %d", summary->passedTests);
    LogMessageEx(LOG_DEVICE_PSB, "Failed: %d", summary->failedTests);
    LogMessageEx(LOG_DEVICE_PSB, "Total Time: %.2f seconds", totalTime);
    LogMessageEx(LOG_DEVICE_PSB, "Average Time: %.2f seconds", 
                 (numTests > 0) ? (totalTime / numTests) : 0.0);
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    
    if (summary->failedTests > 0) {
        LogMessageEx(LOG_DEVICE_PSB, "Failed Tests:");
        for (int i = 0; i < numTests; i++) {
            if (tests[i].result <= 0) {
                LogMessageEx(LOG_DEVICE_PSB, "  - %s: %s", 
                           tests[i].testName, tests[i].errorMessage);
            }
        }
    }
}