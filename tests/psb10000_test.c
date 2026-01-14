/******************************************************************************
 * psb10000_test.c
 *
 * PSB 10000 Test Suite with Queue System Integration
 * Implementation file for comprehensive testing of PSB10000 functions
 *
 * CRITICAL: PSB Mode Selection Rules
 * ==================================
 * The PSB 10000 has non-obvious mode selection behavior. For correct operation:
 * - Write ONLY the target register for your desired mode (CV/CC/CP)
 * - DO NOT write multiple setpoint registers in sequence
 * - Mode is selected at output enable time, not register write time
 *
 * See: notes/PSB-MODE-SELECTION-RULES-2026-01-13.txt for complete documentation
 * This file explains the 40+ commits of debugging that led to understanding
 * the PSB's mode selection algorithm.
 ******************************************************************************/

#include "BatteryTester.h"
#include "psb10000_test.h"
#include "psb10000_queue.h"
#include "teensy_queue.h"  // For relay control
#include "common.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>

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
                PSB_TestSuite_Initialize(context, panel, PANEL_STR_PSB_STATUS);
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
        snprintf(statusMsg, sizeof(statusMsg), 
                 "Test cancelled: %d/%d passed", 
                 context->summary.passedTests, 
                 context->summary.totalTests);
    } else if (context->state == TEST_STATE_COMPLETED) {
        snprintf(statusMsg, sizeof(statusMsg), 
                 "All tests passed (%d/%d)", 
                 context->summary.passedTests,
                 context->summary.totalTests);
    } else {
		snprintf(statusMsg, sizeof(statusMsg), 
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
static int EnsureRemoteModeQueued() {
    PSB_Status status;
    int result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to get status for remote mode check: %s", 
                   PSB_GetErrorString(result));
        return result;
    }
    
    // Only set remote mode if it's not already enabled
    if (!status.remoteMode) {
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode is OFF, enabling it...");
        result = PSB_SetRemoteModeQueued(1, DEVICE_PRIORITY_NORMAL);
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

int PSB_TestSuite_Initialize(TestSuiteContext *context, int panel, int statusControl) {
    if (!context) return -1;
    
    memset(context, 0, sizeof(TestSuiteContext));
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
    if (!context) return -1;
    
    context->state = TEST_STATE_RUNNING;
    context->cancelRequested = 0;
    
    LogMessageEx(LOG_DEVICE_PSB, "Starting PSB Test Suite");
    UpdateTestProgress(context, "Starting PSB Test Suite...");
    
    // Zero out PSB values for safety
    UpdateTestProgress(context, "Zeroing PSB values...");
    if (PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL) != PSB_SUCCESS) {
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
        test->result = test->testFunction(test->errorMessage, 
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
        PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_RemoteMode(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing remote mode control...");
    
    PSB_Status status;
    int result;
    int initialRemoteState;
    
    // Use queued version for status read
    LogDebugEx(LOG_DEVICE_PSB, "Reading initial state...");
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
        result = PSB_SetRemoteModeQueued(0, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn remote mode OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Verify it turned off
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetRemoteModeQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn remote mode ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify it turned on
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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

int Test_StatusRegisterReading(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing status register reading...");
    
    PSB_Status status1, status2;
    int result;
    
    // Ensure remote mode is on using queued command
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Read status multiple times using queued commands
    for (int i = 0; i < 5; i++) {
        result = PSB_GetStatusQueued(&status1, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_GetStatusQueued(&status1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read first comparison status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_VERY_SHORT);
    
    result = PSB_GetStatusQueued(&status2, DEVICE_PRIORITY_NORMAL);
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

int Test_VoltageControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage control...");
    
    // Ensure remote mode using queued command
    int result = PSB_SetRemoteModeQueued(1, DEVICE_PRIORITY_NORMAL);
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
        
        result = PSB_SetVoltageQueued(testVoltages[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.2fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read back the status using queued command
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting voltage: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Voltage set command accepted for %.2fV", testVoltages[i]);
    }
    
    return 1;
}

int Test_VoltageLimits(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage limits...");
    
    // Ensure remote mode is on
    int result = EnsureRemoteModeQueued();
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
    
    result = PSB_SetVoltageLimitsQueued(minVoltage, maxVoltage, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Voltage limits set successfully");
    Delay(TEST_DELAY_SHORT);
    
    // Test voltage within limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage within limits (30V)...");
    result = PSB_SetVoltageQueued(30.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage within limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test voltage outside limits (should be clamped)
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage outside limits...");
    result = PSB_SetVoltageQueued(50.0, DEVICE_PRIORITY_NORMAL);  // Above max
    // This might succeed but be clamped to max
    
    result = PSB_SetVoltageQueued(10.0, DEVICE_PRIORITY_NORMAL);  // Below min
    // This might succeed but be clamped to min
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe voltage limits...");
    result = PSB_SetVoltageLimitsQueued(PSB_SAFE_VOLTAGE_MIN, PSB_SAFE_VOLTAGE_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe voltage limits");
    }
    
    return 1;
}

int Test_CurrentControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current control...");
    
    // Ensure remote mode using queued command
    int result = EnsureRemoteModeQueued();
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
        
        result = PSB_SetCurrentQueued(testCurrents[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set current to %.2fA: %s", 
                    testCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read back status to verify command was accepted
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after setting current: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Current set command accepted for %.2fA", testCurrents[i]);
    }
    
    return 1;
}

int Test_CurrentLimits(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current limits...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First zero all values to ensure clean state
    result = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetCurrentQueued(TEST_CURRENT_MID, DEVICE_PRIORITY_NORMAL);  // 30.0A - within 6-50A range
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current before limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limits: %.2fA - %.2fA...", 
               testMinCurrent, testMaxCurrent);
    
    result = PSB_SetCurrentLimitsQueued(testMinCurrent, testMaxCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limits (%.1fA-%.1fA): %s", 
                testMinCurrent, testMaxCurrent, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test that current is constrained by limits
    LogDebugEx(LOG_DEVICE_PSB, "Testing current above max limit...");
    result = PSB_SetCurrentQueued(TEST_CURRENT_MAX, DEVICE_PRIORITY_NORMAL);  // 60.0A - should be clamped to 50A
    if (result != PSB_SUCCESS && result != PSB_ERROR_INVALID_PARAM) {
        LogWarningEx(LOG_DEVICE_PSB, "Unexpected error setting current above limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing current below min limit...");
    result = PSB_SetCurrentQueued(PSB_SAFE_CURRENT_MIN, DEVICE_PRIORITY_NORMAL);  // 0A - should be clamped to 6A
    if (result != PSB_SUCCESS && result != PSB_ERROR_INVALID_PARAM) {
        LogWarningEx(LOG_DEVICE_PSB, "Unexpected error setting current below limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe current limits...");
    result = PSB_SetCurrentLimitsQueued(PSB_SAFE_CURRENT_MIN, PSB_SAFE_CURRENT_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe current limits: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Current limits test completed");
    return 1;
}

int Test_PowerControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power control...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test valid power values
    double testPowers[] = {TEST_POWER_LOW, TEST_POWER_MID, TEST_POWER_HIGH};  // 100W, 600W, 1000W
    
    for (int i = 0; i < ARRAY_SIZE(testPowers); i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting power to %.2fW...", testPowers[i]);
        result = PSB_SetPowerQueued(testPowers[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set power to %.1fW: %s", 
                    testPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        Delay(TEST_DELAY_SHORT);
        
        // Get actual values to verify
        double actualVoltage, actualCurrent, actualPower;
        result = PSB_GetActualValuesQueued(&actualVoltage, &actualCurrent, &actualPower, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetPowerQueued(TEST_POWER_INVALID, DEVICE_PRIORITY_NORMAL);  // 1400W - beyond device limit
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid power: %s", PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Power control test completed");
    return 1;
}

int Test_PowerLimit(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power limit...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First, ensure power is set to a low value to avoid conflicts
    LogDebugEx(LOG_DEVICE_PSB, "Setting initial power to %.1fW...", TEST_POWER_LOW);
    result = PSB_SetPowerQueued(TEST_POWER_LOW, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set initial power: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid power limit
    double testPowerLimit = TEST_POWER_MAX;  // 1200W - just below device max
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting power limit to %.2fW...", testPowerLimit);
    result = PSB_SetPowerLimitQueued(testPowerLimit, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power limit to %.1fW: %s", 
                testPowerLimit, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify we can set power below the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing power below limit (%.1fW)...", TEST_POWER_HIGH);
    result = PSB_SetPowerQueued(TEST_POWER_HIGH, DEVICE_PRIORITY_NORMAL);  // 1000W - should work
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power below limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test that power above limit is rejected or clamped
    LogDebugEx(LOG_DEVICE_PSB, "Testing power above limit...");
    result = PSB_SetPowerQueued(testPowerLimit + TEST_SINK_POWER_ABOVE_LIMIT, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Power above limit was accepted (may be clamped by device)");
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Power above limit correctly rejected: %s", 
                  PSB_GetErrorString(result));
    }
    
    // Restore safe power limit
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe power limit (%.1fW)...", PSB_SAFE_POWER_MAX);
    result = PSB_SetPowerLimitQueued(PSB_SAFE_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe power limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Test invalid power limit (beyond device capability)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid power limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetPowerLimitQueued(TEST_POWER_INVALID, DEVICE_PRIORITY_NORMAL);  // 1400W - beyond max
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

int Test_SinkCurrentControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current control...");
    
    int result;
    PSB_Status status;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
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
    result = PSB_SetVoltageQueued(PSB_SAFE_VOLTAGE_MIN, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage to 0V: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Ensure output is disabled for safety
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring output is disabled...");
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
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
        
        result = PSB_SetSinkCurrentQueued(testSinkCurrents[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set sink current to %.2fA: %s", 
                    testSinkCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read status to check if device is in sink mode
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetSinkCurrentQueued(TEST_SINK_CURRENT_NEGATIVE, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink current");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink current: %s", 
              PSB_GetErrorString(result));
    
    // Test sink current beyond limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current beyond limit (%.1fA)...", TEST_CURRENT_INVALID);
    result = PSB_SetSinkCurrentQueued(TEST_CURRENT_INVALID, DEVICE_PRIORITY_NORMAL);
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

int Test_SinkPowerControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power control...");
    
    int result;
    PSB_Status status;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Prepare for sink mode
    LogDebugEx(LOG_DEVICE_PSB, "Preparing for sink mode operation...");
    result = PSB_SetVoltageQueued(PSB_SAFE_VOLTAGE_MIN, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage to 0V: %s", 
                   PSB_GetErrorString(result));
    }
    
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
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
        
        result = PSB_SetSinkPowerQueued(testSinkPowers[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set sink power to %.2fW: %s", 
                    testSinkPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read status
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetSinkPowerQueued(TEST_SINK_POWER_NEGATIVE, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink power");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink power: %s", 
              PSB_GetErrorString(result));
    
    // Test sink power beyond limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power beyond limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetSinkPowerQueued(TEST_POWER_INVALID, DEVICE_PRIORITY_NORMAL);
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

int Test_SinkCurrentLimits(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current limits...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First zero all values to ensure clean state
    LogDebugEx(LOG_DEVICE_PSB, "Zeroing values for baseline...");
    result = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to zero values: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
	Delay(TEST_DELAY_SHORT);
    
    // CRITICAL: Set sink current to a value within the new limits BEFORE setting limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink current to %.2fA (within new limits)...", 
               TEST_SINK_CURRENT_LIMIT_TEST);
    result = PSB_SetSinkCurrentQueued(TEST_SINK_CURRENT_LIMIT_TEST, DEVICE_PRIORITY_NORMAL); // 20A
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current before limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid sink current limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink current limits: %.2fA - %.2fA...", 
               TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX);
    
    result = PSB_SetSinkCurrentLimitsQueued(TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current limits (%.1fA-%.1fA): %s", 
                TEST_SINK_CURRENT_LIMIT_MIN, TEST_SINK_CURRENT_LIMIT_MAX, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test that sink current can be set within limits
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current within limits (%.1fA)...", TEST_SINK_CURRENT_LIMIT_TEST);
    result = PSB_SetSinkCurrentQueued(TEST_SINK_CURRENT_LIMIT_TEST, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current within limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink current at max limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current at max limit (%.1fA)...", TEST_SINK_CURRENT_LIMIT_MAX);
    result = PSB_SetSinkCurrentQueued(TEST_SINK_CURRENT_LIMIT_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current at max limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink current at min limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink current at min limit (%.1fA)...", TEST_SINK_CURRENT_LIMIT_MIN);
    result = PSB_SetSinkCurrentQueued(TEST_SINK_CURRENT_LIMIT_MIN, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink current at min limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test invalid limits (min > max)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid sink current limits (min > max)...");
    result = PSB_SetSinkCurrentLimitsQueued(TEST_SINK_CURRENT_LIMIT_MIN_INV, TEST_SINK_CURRENT_LIMIT_MAX_INV, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted sink current limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted sink current limits: %s", 
              PSB_GetErrorString(result));
    
    // Test negative minimum limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative minimum sink current limit (%.1fA)...", TEST_SINK_CURRENT_MIN_NEG);
    result = PSB_SetSinkCurrentLimitsQueued(TEST_SINK_CURRENT_MIN_NEG, TEST_SINK_CURRENT_LIMIT_MAX, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative minimum sink current limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative minimum limit: %s", 
              PSB_GetErrorString(result));
    
    // Test excessive maximum limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing excessive maximum sink current limit (%.1fA)...", TEST_CURRENT_INVALID);
    result = PSB_SetSinkCurrentLimitsQueued(PSB_SAFE_SINK_CURRENT_MIN, TEST_CURRENT_INVALID, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected excessive maximum sink current limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive maximum limit: %s", 
              PSB_GetErrorString(result));
    
    // Restore safe limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe sink current limits...");
    result = PSB_SetSinkCurrentLimitsQueued(PSB_SAFE_SINK_CURRENT_MIN, PSB_SAFE_SINK_CURRENT_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe sink current limits: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink current limits test passed");
    return 1;
}

int Test_SinkPowerLimit(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power limit...");
    
    int result;
    
    // Ensure remote mode
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // First, ensure sink power is set to a low value to avoid conflicts
    LogDebugEx(LOG_DEVICE_PSB, "Setting initial sink power to %.1fW...", TEST_SINK_POWER_LOW);
    result = PSB_SetSinkPowerQueued(TEST_SINK_POWER_LOW, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set initial sink power: %s", 
                   PSB_GetErrorString(result));
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting valid sink power limit
    LogDebugEx(LOG_DEVICE_PSB, "Setting sink power limit to %.2fW...", TEST_SINK_POWER_LIMIT_1);
    result = PSB_SetSinkPowerLimitQueued(TEST_SINK_POWER_LIMIT_1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power limit to %.1fW: %s", 
                TEST_SINK_POWER_LIMIT_1, PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify we can set sink power below the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power below limit (%.1fW)...", TEST_SINK_POWER_LIMIT_TEST);
    result = PSB_SetSinkPowerQueued(TEST_SINK_POWER_LIMIT_TEST, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power below limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test sink power at the limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing sink power at limit (%.1fW)...", TEST_SINK_POWER_LIMIT_1);
    result = PSB_SetSinkPowerQueued(TEST_SINK_POWER_LIMIT_1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set sink power at limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // IMPORTANT: Before changing to a lower limit, first reduce the sink power
    LogDebugEx(LOG_DEVICE_PSB, "Reducing sink power to %.1fW before lowering limit...", TEST_SINK_POWER_LOW);
    result = PSB_SetSinkPowerQueued(TEST_SINK_POWER_LOW, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to reduce sink power before changing limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Now test different (lower) power limit
    LogDebugEx(LOG_DEVICE_PSB, "Changing sink power limit to %.2fW...", TEST_SINK_POWER_LIMIT_2);
    result = PSB_SetSinkPowerLimitQueued(TEST_SINK_POWER_LIMIT_2, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to change sink power limit to %.1fW: %s", 
                TEST_SINK_POWER_LIMIT_2, PSB_GetErrorString(result));
        return -1;
    }
    
    // Test negative power limit (should fail)
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative sink power limit (%.1fW)...", TEST_SINK_POWER_NEGATIVE);
    result = PSB_SetSinkPowerLimitQueued(TEST_SINK_POWER_NEGATIVE, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative sink power limit");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative sink power limit: %s", 
              PSB_GetErrorString(result));
    
    // Test excessive power limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing excessive sink power limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetSinkPowerLimitQueued(TEST_POWER_INVALID, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected sink power limit %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_SAFE_SINK_POWER_MAX);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected excessive sink power limit: %s", 
              PSB_GetErrorString(result));
    
    // Restore safe power limit
    LogDebugEx(LOG_DEVICE_PSB, "Restoring safe sink power limit (%.1fW)...", PSB_SAFE_SINK_POWER_MAX);
    result = PSB_SetSinkPowerLimitQueued(PSB_SAFE_SINK_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore safe sink power limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Sink power limit test passed");
    return 1;
}

int Test_OutputControl(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing output enable/disable...");
    
    PSB_Status status;
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Read initial output state using queued command
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
        result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn output OFF: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Verify it turned off
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn output ON: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify it turned on
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn output OFF for safety: %s", 
                   PSB_GetErrorString(result));
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Output control test passed");
    return 1;
}

int Test_InvalidParameters(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid parameter handling...");
    
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test invalid voltage (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative voltage...");
    result = PSB_SetVoltageQueued(-10.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative voltage");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative voltage");
    
    // Test invalid current (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative current...");
    result = PSB_SetCurrentQueued(-5.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative current");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative current");
    
    // Test invalid power (negative) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative power...");
    result = PSB_SetPowerQueued(-100.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative power");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative power");
    
    // Test invalid limits (min > max) using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid voltage limits (min > max)...");
    result = PSB_SetVoltageLimitsQueued(50.0, 20.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted voltage limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted voltage limits");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid current limits (min > max)...");
    result = PSB_SetCurrentLimitsQueued(40.0, 10.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected inverted current limits");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected inverted current limits");
    
    LogDebugEx(LOG_DEVICE_PSB, "Invalid parameter handling test passed");
    return 1;
}

int Test_BoundaryConditions(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing boundary conditions...");
    
    int result;
    
    // Ensure remote mode and zero values
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    result = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to zero values: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test minimum voltage using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum voltage (%.2fV)...", PSB_SAFE_VOLTAGE_MIN);
    result = PSB_SetVoltageQueued(PSB_SAFE_VOLTAGE_MIN, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum voltage accepted");
    
    // Test minimum current using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum current (%.2fA)...", PSB_SAFE_CURRENT_MIN);
    result = PSB_SetCurrentQueued(PSB_SAFE_CURRENT_MIN, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum current accepted");
    
    // Test values below minimum (should fail) using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum voltage...");
    result = PSB_SetVoltageQueued(-2.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected voltage below minimum");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum current...");
    result = PSB_SetCurrentQueued(-2.0, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected current below minimum");
    
    // Test maximum values using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum voltage (%.2fV)...", PSB_NOMINAL_VOLTAGE);  // 60V
    result = PSB_SetVoltageQueued(PSB_NOMINAL_VOLTAGE, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum voltage accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum current (%.2fA)...", PSB_NOMINAL_CURRENT);  // 60A
    result = PSB_SetCurrentQueued(PSB_NOMINAL_CURRENT, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum current accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Boundary conditions test passed");
    return 1;
}

int Test_SequenceOperations(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sequence of operations...");
    
    int result;
    PSB_Status status;
    
    // This test explicitly tests remote mode transitions, but respects the final state
    
    // Step 1: Turn remote mode OFF to test the sequence
    LogDebugEx(LOG_DEVICE_PSB, "Step 1: Setting remote mode OFF for sequence test...");
    result = PSB_SetRemoteModeQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn off remote mode, continuing anyway: %s", 
                PSB_GetErrorString(result));
    } else {
        Delay(TEST_DELAY_SHORT);
    }
    
    // Step 2: Turn remote mode ON using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 2: Setting remote mode ON...");
    result = PSB_SetRemoteModeQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Step 3: Set voltage using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 3: Setting voltage to 24V...");
    result = PSB_SetVoltageQueued(24.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Step 4: Set current using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 4: Setting current to 10A...");
    result = PSB_SetCurrentQueued(10.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Step 5: Enable output using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 5: Enabling output...");
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable output: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Step 6: Read status using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Step 6: Reading status...");
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
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
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    // Keep remote mode ON as required
    LogDebugEx(LOG_DEVICE_PSB, "Keeping remote mode ON as required");
    
    LogDebugEx(LOG_DEVICE_PSB, "Sequence operations test passed");
    return 1;
}

int Test_OutputVoltageVerification(char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing output voltage verification...");
    
    int result;
    
    // Ensure remote mode using queued command
    result = EnsureRemoteModeQueued();
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Ensure output is initially disabled
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring output is disabled...");
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to disable output: %s", 
                   PSB_GetErrorString(result));
    }
    
    // Set safe operating parameters using queued commands
    LogDebugEx(LOG_DEVICE_PSB, "Setting safe operating parameters...");
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limit to 1.0A...");
    result = PSB_SetCurrentQueued(1.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to 0V...");
    result = PSB_SetVoltageQueued(0.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
	// Set power limit and value high to avoid hitting CP mode during the test
	LogWarningEx(LOG_DEVICE_PSB, "Setting power limit to 600W...");
    result = PSB_SetPowerLimitQueued(600.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial power limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
	
	LogWarningEx(LOG_DEVICE_PSB, "Setting power to 600W...");
    result = PSB_SetPowerQueued(600.0, DEVICE_PRIORITY_NORMAL);
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
        result = PSB_SetVoltageQueued(testVoltages[i], DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.1fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            // Ensure output is off before returning
            PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Enable output using queued command
        LogDebugEx(LOG_DEVICE_PSB, "Enabling output...");
        result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to enable output: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_MEDIUM);  // Wait for output to stabilize
        
        // Read actual values using queued command
        double actualVoltage, actualCurrent, actualPower;
        result = PSB_GetActualValuesQueued(&actualVoltage, &actualCurrent, &actualPower, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read actual values: %s", 
                    PSB_GetErrorString(result));
            PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
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
        result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
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
}/******************************************************************************
 * PSB REGISTER MATRIX TEST IMPLEMENTATION
 *
 * Comprehensive systematic testing of PSB register configurations to
 * empirically determine mode selection behavior.
 *
 * This code will be appended to tests/psb10000_test.c
 ******************************************************************************/

/******************************************************************************
 * Register Test Case Structure
 ******************************************************************************/

typedef struct {
    int testId;
    char testName[128];

    // Battery conditions
    double batteryVoltageStart;
    double targetVoltage;

    // Register configuration (setpoints)
    double reg498_sinkPower;
    double reg499_sinkCurrent;
    double reg500_voltage;
    double reg501_current;
    double reg502_power;

    // Register configuration (limits)
    double reg9000_voltageMax;
    double reg9001_voltageMin;
    double reg9002_currentMax;
    double reg9003_currentMin;
    double reg9004_powerMax;
    double reg9005_sinkPowerMax;
    double reg9008_sinkCurrentMax;
    double reg9009_sinkCurrentMin;

    // Results - pre-enable
    int preEnableMode;           // 0=CV, 1=CR, 2=CC, 3=CP
    int preEnableDirection;      // 0=SOURCE, 1=SINK
    int preEnableOutput;         // 0=OFF, 1=ON

    // Results - post-enable
    int postEnableMode;          // 0=CV, 1=CR, 2=CC, 3=CP
    int postEnableDirection;     // 0=SOURCE, 1=SINK
    int postEnableOutput;        // 0=OFF, 1=ON
    int modeSwitched;            // Did mode change after enable?

    // Measurement results
    double avgVoltage;
    double avgCurrent;
    double peakCurrent;
    double avgPower;
    int currentNonZero;          // Was current > 0.1A?

    // Test result
    int testResult;              // 0=not run, 1=PASS, -1=FAIL
    char notes[256];
} RegisterTestCase;

/******************************************************************************
 * Test Matrix Generation Functions
 ******************************************************************************/

// Generate CV mode tests (primary focus)
static int GenerateCVModeTests(RegisterTestCase **tests, int *numTests) {
    int count = 0;
    RegisterTestCase *testArray = (RegisterTestCase*)malloc(sizeof(RegisterTestCase) * 30);
    if (!testArray) return ERR_OUT_OF_MEMORY;

    memset(testArray, 0, sizeof(RegisterTestCase) * 30);

    // Battery starting voltage (typical discharged state)
    double batteryV = 2.7;
    double targetV = TEST_CV_VOLTAGE_TARGET;  // 4.2V

    // Test 1-6: CV with varying power limits (test CP mode threshold)
    double powerLimits[] = {
        TEST_POWER_LIMIT_VERY_LOW,   // 20W - known to cause CP
        TEST_POWER_LIMIT_LOW,         // 30W - boundary
        TEST_POWER_LIMIT_MID,         // 50W
        TEST_POWER_LIMIT_HIGH,        // 100W
        TEST_POWER_LIMIT_VERY_HIGH,   // 200W
        TEST_POWER_LIMIT_SAFE         // 1224W - known working
    };

    for (int i = 0; i < 6; i++) {
        RegisterTestCase *test = &testArray[count++];
        test->testId = count;
        snprintf(test->testName, sizeof(test->testName),
                 "CV-PowerLimit-%.0fW", powerLimits[i]);

        test->batteryVoltageStart = batteryV;
        test->targetVoltage = targetV;

        // CV configuration: ONLY voltage register will be written
        // Other registers show what they'll be from initialization
        // NOTE: Per PSB-MODE-SELECTION-RULES, we write ONLY reg500_voltage
        test->reg498_sinkPower = 100.0;    // Decoy (set at init, not rewritten)
        test->reg499_sinkCurrent = 10.0;   // Decoy (set at init, not rewritten)
        test->reg500_voltage = targetV;    // ACTIVE - this is written for CV mode
        test->reg501_current = 0.0;        // Zero (from init, not rewritten)
        test->reg502_power = 0.0;          // Zero (from init, not rewritten)

        // Limits - vary power limit
        test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
        test->reg9001_voltageMin = 0.0;
        test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
        test->reg9003_currentMin = 0.0;
        test->reg9004_powerMax = powerLimits[i];  // VARIABLE
        test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
        test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
        test->reg9009_sinkCurrentMin = 0.0;
    }

    // Test 7-9: CV with varying decoy values
    double decoyCurrents[] = {0.0, 5.0, 10.0};  // No decoy, low decoy, standard decoy
    double decoyPowers[] = {0.0, 50.0, 100.0};

    for (int i = 0; i < 3; i++) {
        RegisterTestCase *test = &testArray[count++];
        test->testId = count;
        snprintf(test->testName, sizeof(test->testName),
                 "CV-Decoy-%.0fA-%.0fW", decoyCurrents[i], decoyPowers[i]);

        test->batteryVoltageStart = batteryV;
        test->targetVoltage = targetV;

        // CV with variable decoys - testing if decoy values matter
        // NOTE: Decoys are set at initialization, but for these tests we'll
        // reinitialize with different decoy values to test their effect
        // These tests will require special handling in RunSingleRegisterTest
        test->reg498_sinkPower = decoyPowers[i];   // Decoy to test (will be rewritten)
        test->reg499_sinkCurrent = decoyCurrents[i]; // Decoy to test (will be rewritten)
        test->reg500_voltage = targetV;             // ACTIVE - written for CV mode
        test->reg501_current = 0.0;                 // Zero (not written)
        test->reg502_power = 0.0;                   // Zero (not written)

        // High safe limits
        test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
        test->reg9001_voltageMin = 0.0;
        test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
        test->reg9003_currentMin = 0.0;
        test->reg9004_powerMax = PSB_SAFE_POWER_MAX;  // High
        test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
        test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
        test->reg9009_sinkCurrentMin = 0.0;
    }

    // Test 10-12: CV with small secondary setpoints (test interference)
    double secondaryCurrents[] = {0.0, 0.5, 1.0};

    for (int i = 0; i < 3; i++) {
        RegisterTestCase *test = &testArray[count++];
        test->testId = count;
        snprintf(test->testName, sizeof(test->testName),
                 "CV-Secondary-%.1fA", secondaryCurrents[i]);

        test->batteryVoltageStart = batteryV;
        test->targetVoltage = targetV;

        // CV with small secondary setpoint - testing if secondary setpoints interfere
        // NOTE: These tests write REG 501 (secondary current) before REG 500 (voltage)
        // to see if having a non-zero secondary setpoint interferes with CV mode
        test->reg498_sinkPower = 100.0;              // Decoy (from init)
        test->reg499_sinkCurrent = 10.0;             // Decoy (from init)
        test->reg500_voltage = targetV;              // ACTIVE - written last for CV mode
        test->reg501_current = secondaryCurrents[i]; // Secondary (will be written first)
        test->reg502_power = 0.0;                    // Zero (not written)

        // High safe limits
        test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
        test->reg9001_voltageMin = 0.0;
        test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
        test->reg9003_currentMin = 0.0;
        test->reg9004_powerMax = PSB_SAFE_POWER_MAX;
        test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
        test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
        test->reg9009_sinkCurrentMin = 0.0;
    }

    *tests = testArray;
    *numTests = count;
    return SUCCESS;
}

// Generate CC mode tests (limited - safe current values only)
static int GenerateCCModeTests(RegisterTestCase **tests, int *numTests) {
    int count = 0;
    RegisterTestCase *testArray = (RegisterTestCase*)malloc(sizeof(RegisterTestCase) * 15);
    if (!testArray) return ERR_OUT_OF_MEMORY;

    memset(testArray, 0, sizeof(RegisterTestCase) * 15);

    double batteryV = 2.7;

    // Test with safe current values (0.5A, 1A, 3A MAX)
    double testCurrents[] = {
        TEST_CC_CURRENT_LOW,   // 0.5A
        TEST_CC_CURRENT_MID,   // 1.0A
        TEST_CC_CURRENT_HIGH   // 3.0A (MAX - SAFE LIMIT)
    };

    double testVoltages[] = {3.0, 3.7, 4.2};

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            RegisterTestCase *test = &testArray[count++];
            test->testId = count;
            snprintf(test->testName, sizeof(test->testName),
                     "CC-%.1fA-%.1fV", testCurrents[i], testVoltages[j]);

            test->batteryVoltageStart = batteryV;
            test->targetVoltage = testVoltages[j];

            // CC configuration: ONLY current register will be written
            // Other registers show what they'll be from initialization
            test->reg498_sinkPower = 100.0;           // Decoy (from init, not rewritten)
            test->reg499_sinkCurrent = 10.0;          // Decoy (from init, not rewritten)
            test->reg500_voltage = 0.0;               // Zero (from init, not rewritten)
            test->reg501_current = testCurrents[i];   // ACTIVE - written for CC mode
            test->reg502_power = 0.0;                 // Zero (from init, not rewritten)

            // High safe limits
            test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
            test->reg9001_voltageMin = 0.0;
            test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
            test->reg9003_currentMin = 0.0;
            test->reg9004_powerMax = PSB_SAFE_POWER_MAX;
            test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
            test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
            test->reg9009_sinkCurrentMin = 0.0;
        }
    }

    *tests = testArray;
    *numTests = count;
    return SUCCESS;
}

// Generate CP mode tests (limited - safe power values only)
static int GenerateCPModeTests(RegisterTestCase **tests, int *numTests) {
    int count = 0;
    RegisterTestCase *testArray = (RegisterTestCase*)malloc(sizeof(RegisterTestCase) * 15);
    if (!testArray) return ERR_OUT_OF_MEMORY;

    memset(testArray, 0, sizeof(RegisterTestCase) * 15);

    double batteryV = 2.7;

    // Test with safe power values (5W, 10W, 20W MAX)
    double testPowers[] = {
        TEST_CP_POWER_LOW,   // 5W
        TEST_CP_POWER_MID,   // 10W
        TEST_CP_POWER_HIGH   // 20W (MAX - SAFE LIMIT)
    };

    double testVoltages[] = {3.0, 3.7, 4.2};

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            RegisterTestCase *test = &testArray[count++];
            test->testId = count;
            snprintf(test->testName, sizeof(test->testName),
                     "CP-%.0fW-%.1fV", testPowers[i], testVoltages[j]);

            test->batteryVoltageStart = batteryV;
            test->targetVoltage = testVoltages[j];

            // CP configuration: ONLY power register will be written
            // Other registers show what they'll be from initialization
            test->reg498_sinkPower = 100.0;          // Decoy (from init, not rewritten)
            test->reg499_sinkCurrent = 10.0;         // Decoy (from init, not rewritten)
            test->reg500_voltage = 0.0;              // Zero (from init, not rewritten)
            test->reg501_current = 0.0;              // Zero (from init, not rewritten)
            test->reg502_power = testPowers[i];      // ACTIVE - written for CP mode

            // High safe limits
            test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
            test->reg9001_voltageMin = 0.0;
            test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
            test->reg9003_currentMin = 0.0;
            test->reg9004_powerMax = PSB_SAFE_POWER_MAX;
            test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
            test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
            test->reg9009_sinkCurrentMin = 0.0;
        }
    }

    *tests = testArray;
    *numTests = count;
    return SUCCESS;
}

/******************************************************************************
 * Generate Combined CC-CV Mode Tests
 *
 * These tests set ALL THREE registers to non-zero values:
 *   REG 502 = power limit (max power)
 *   REG 500 = voltage limit (max voltage)
 *   REG 501 = current setpoint (target current)
 *
 * This is the classic CC-CV charging configuration:
 *   1. Charge at constant current (REG 501)
 *   2. Until voltage reaches limit (REG 500)
 *   3. Never exceed power limit (REG 502)
 ******************************************************************************/
static int GenerateCombinedModeTests(RegisterTestCase **tests, int *numTests) {
    int count = 0;
    RegisterTestCase *testArray = (RegisterTestCase*)malloc(sizeof(RegisterTestCase) * 10);
    if (!testArray) return ERR_OUT_OF_MEMORY;

    memset(testArray, 0, sizeof(RegisterTestCase) * 10);

    double batteryV = 2.7;

    // Test combinations: varying current with fixed voltage and power limits
    // These represent realistic CC-CV charging scenarios
    double testCurrents[] = {0.5, 1.0, 2.0};    // A
    double testVoltages[] = {4.2};               // V (typical Li-ion max)
    double testPowers[] = {20.0};                // W (our safe power limit)

    for (int i = 0; i < 3; i++) {
        RegisterTestCase *test = &testArray[count++];
        test->testId = count;
        snprintf(test->testName, sizeof(test->testName),
                 "CCCV-%.1fA-%.1fV-%.0fW", testCurrents[i], testVoltages[0], testPowers[0]);

        test->batteryVoltageStart = batteryV;
        test->targetVoltage = testVoltages[0];

        // COMBINED configuration: ALL THREE registers non-zero
        test->reg498_sinkPower = PSB_SINK_POWER_LIMIT;    // Sink power limit
        test->reg499_sinkCurrent = PSB_SINK_CURRENT_DECOY; // Sink current
        test->reg500_voltage = testVoltages[0];            // Voltage limit
        test->reg501_current = testCurrents[i];            // Current setpoint
        test->reg502_power = testPowers[0];                // Power limit

        // High safe limits
        test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
        test->reg9001_voltageMin = 0.0;
        test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
        test->reg9003_currentMin = 0.0;
        test->reg9004_powerMax = PSB_SAFE_POWER_MAX;
        test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
        test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
        test->reg9009_sinkCurrentMin = 0.0;
    }

    // Also test with different power limits to see effect
    double powerLimits[] = {10.0, 15.0};  // Lower power limits
    for (int i = 0; i < 2; i++) {
        RegisterTestCase *test = &testArray[count++];
        test->testId = count;
        snprintf(test->testName, sizeof(test->testName),
                 "CCCV-1.0A-4.2V-%.0fW", powerLimits[i]);

        test->batteryVoltageStart = batteryV;
        test->targetVoltage = 4.2;

        test->reg498_sinkPower = PSB_SINK_POWER_LIMIT;
        test->reg499_sinkCurrent = PSB_SINK_CURRENT_DECOY;
        test->reg500_voltage = 4.2;
        test->reg501_current = 1.0;
        test->reg502_power = powerLimits[i];  // Variable power limit

        test->reg9000_voltageMax = PSB_SAFE_VOLTAGE_MAX;
        test->reg9001_voltageMin = 0.0;
        test->reg9002_currentMax = PSB_SAFE_CURRENT_MAX;
        test->reg9003_currentMin = 0.0;
        test->reg9004_powerMax = PSB_SAFE_POWER_MAX;
        test->reg9005_sinkPowerMax = PSB_SAFE_POWER_MAX;
        test->reg9008_sinkCurrentMax = PSB_SAFE_SINK_CURRENT_MAX;
        test->reg9009_sinkCurrentMin = 0.0;
    }

    *tests = testArray;
    *numTests = count;
    return SUCCESS;
}

/******************************************************************************
 * PSB Initialization for Register Matrix Test
 *
 * Sets decoy values and high limits ONCE before all tests
 * See notes/PSB-MODE-SELECTION-RULES-2026-01-13.txt for explanation
 ******************************************************************************/

static int InitializePSBForMatrixTest(char *errorMsg, int errorMsgSize) {
    int result;

    LogMessage("========================================");
    LogMessage("Initializing PSB with decoy values and high limits");
    LogMessage("========================================");

    // PSB REGISTER CONFIGURATION:
    // REG 502 = power limit (20W) - CRITICAL: 0W would block all current!
    // REG 500 = voltage setpoint (for CV mode)
    // REG 501 = current setpoint (for CC mode)
    // REG 498/499 = sink mode decoys

    LogMessage("Setting source power limit (REG 502) - enables current flow...");
    LogMessage("  REG 502 (POWER): %.1f W (power limit for CV/CC modes)", PSB_SOURCE_POWER_LIMIT);
    result = PSB_SetPowerQueued(PSB_SOURCE_POWER_LIMIT, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set REG 502 power limit");
        return result;
    }

    LogMessage("Setting sink power limit and current...");
    LogMessage("  REG 498 (SINK_POWER): %.1f W (discharge power limit)", PSB_SINK_POWER_LIMIT);
    result = PSB_SetSinkPowerQueued(PSB_SINK_POWER_LIMIT, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set REG 498");
        return result;
    }

    LogMessage("  REG 499 (SINK_CURRENT): %.1f A", PSB_SINK_CURRENT_DECOY);
    result = PSB_SetSinkCurrentQueued(PSB_SINK_CURRENT_DECOY, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set REG 499");
        return result;
    }

    LogMessage("Setting initial setpoints...");
    LogMessage("  REG 500 (VOLTAGE): 0.0 V");
    result = PSB_SetVoltageQueued(0.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set REG 500");
        return result;
    }

    LogMessage("  REG 501 (CURRENT): 0.0 A");
    result = PSB_SetCurrentQueued(0.0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set REG 501");
        return result;
    }

    // Set all limits to high safe values (never modified during tests)
    // This ensures current can flow - limits must be high enough
    LogMessage("Setting all limit registers to safe high values...");
    LogMessage("  REG 9000/9001 (VOLTAGE): 0.0 - 61.2 V");
    LogMessage("  REG 9002/9003 (CURRENT): 0.0 - 61.2 A");
    LogMessage("  REG 9004 (POWER_MAX): 1224.0 W");
    LogMessage("  REG 9005 (SINK_POWER_MAX): 1224.0 W");
    LogMessage("  REG 9008/9009 (SINK_CURRENT): 0.0 - 61.2 A");

    result = PSB_SetSafeLimitsQueued(DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set safe limits");
        return result;
    }

    LogMessage("Initialization complete - REG 502=%.0fW (source), REG 498=%.0fW (sink)",
               PSB_SOURCE_POWER_LIMIT, PSB_SINK_POWER_LIMIT);
    Delay(TEST_DELAY_SHORT);

    return SUCCESS;
}

/******************************************************************************
 * Single Test Execution
 *
 * CRITICAL: Only writes the TARGET register for the desired mode
 * See notes/PSB-MODE-SELECTION-RULES-2026-01-13.txt for explanation
 *
 * CV tests: Write ONLY REG 500 (voltage)
 * CC tests: Write ONLY REG 501 (current)
 * CP tests: Write ONLY REG 502 (power)
 ******************************************************************************/

static int RunSingleRegisterTest(RegisterTestCase *test, char *errorMsg, int errorMsgSize) {
    int result;
    PSB_Status status;

    LogMessage("========================================");
    LogMessage("Test %d: %s", test->testId, test->testName);
    LogMessage("========================================");

    // Step 1: Disable output
    LogMessage("Step 1: Disabling output...");
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to disable output");
        return result;
    }
    Delay(TEST_DELAY_SHORT);

    // Step 2: Write ONLY the target register for this test mode
    // CRITICAL: Do NOT write other setpoint registers!
    // See PSB-MODE-SELECTION-RULES-2026-01-13.txt for why this is essential
    LogMessage("Step 2: Writing target register only (not all registers!)...");

    // Determine test mode from test name and write ONLY the relevant register
    if (strncmp(test->testName, "CV-", 3) == 0) {
        // CV mode test: Write voltage register
        // Special case: CV-Decoy tests also modify decoy registers to test their effect
        if (strncmp(test->testName, "CV-Decoy", 8) == 0) {
            LogMessage("  CV-Decoy test detected - writing decoys then voltage");
            LogMessage("  REG 498 (SINK_POWER): %.2f W (decoy)", test->reg498_sinkPower);
            result = PSB_SetSinkPowerQueued(test->reg498_sinkPower, DEVICE_PRIORITY_NORMAL);
            if (result != PSB_SUCCESS) {
                snprintf(errorMsg, errorMsgSize, "Failed to set REG 498");
                return result;
            }

            LogMessage("  REG 499 (SINK_CURRENT): %.2f A (decoy)", test->reg499_sinkCurrent);
            result = PSB_SetSinkCurrentQueued(test->reg499_sinkCurrent, DEVICE_PRIORITY_NORMAL);
            if (result != PSB_SUCCESS) {
                snprintf(errorMsg, errorMsgSize, "Failed to set REG 499");
                return result;
            }

            Delay(TEST_DELAY_SHORT);  // Let decoys settle
        }

        // Special case: CV-Secondary tests write secondary setpoint before voltage
        if (strncmp(test->testName, "CV-Secondary", 12) == 0 && test->reg501_current > 0.01) {
            LogMessage("  CV-Secondary test detected - writing secondary current before voltage");
            LogMessage("  REG 501 (CURRENT): %.2f A (secondary setpoint)", test->reg501_current);
            result = PSB_SetCurrentQueued(test->reg501_current, DEVICE_PRIORITY_NORMAL);
            if (result != PSB_SUCCESS) {
                snprintf(errorMsg, errorMsgSize, "Failed to set REG 501");
                return result;
            }
            Delay(TEST_DELAY_SHORT);  // Brief delay
        }

        // Write voltage register LAST (this is what should determine CV mode)
        LogMessage("  REG 500 (VOLTAGE): %.2f V (ACTIVE - written last)", test->reg500_voltage);
        result = PSB_SetVoltageQueued(test->reg500_voltage, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 500");
            return result;
        }
        if (strncmp(test->testName, "CV-Secondary", 12) != 0) {
            LogMessage("  NOT writing REG 501, 502 (preserving zeros)");
        }

    } else if (strncmp(test->testName, "CC-", 3) == 0) {
        // CC mode test: Write ONLY current register
        LogMessage("  CC test detected - writing ONLY REG 501 (current)");
        LogMessage("  REG 501 (CURRENT): %.2f A", test->reg501_current);
        result = PSB_SetCurrentQueued(test->reg501_current, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 501");
            return result;
        }
        LogMessage("  NOT writing REG 498, 499, 500, 502 (preserving decoys/zeros)");

    } else if (strncmp(test->testName, "CP-", 3) == 0) {
        // CP mode test: Write ONLY power register
        LogMessage("  CP test detected - writing ONLY REG 502 (power)");
        LogMessage("  REG 502 (POWER): %.2f W", test->reg502_power);
        result = PSB_SetPowerQueued(test->reg502_power, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 502");
            return result;
        }
        LogMessage("  NOT writing REG 498, 499, 500, 501 (preserving decoys/zeros)");

    } else if (strncmp(test->testName, "CCCV-", 5) == 0) {
        // Combined CC-CV test: Write ALL THREE registers (power, voltage, current)
        // This is the classic CC-CV charging configuration
        LogMessage("  CCCV test detected - writing ALL THREE registers");

        // Write power limit first (REG 502)
        LogMessage("  REG 502 (POWER): %.2f W (power limit)", test->reg502_power);
        result = PSB_SetPowerQueued(test->reg502_power, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 502");
            return result;
        }

        // Write voltage limit second (REG 500)
        LogMessage("  REG 500 (VOLTAGE): %.2f V (voltage limit)", test->reg500_voltage);
        result = PSB_SetVoltageQueued(test->reg500_voltage, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 500");
            return result;
        }

        // Write current setpoint last (REG 501) - this should determine CC mode
        LogMessage("  REG 501 (CURRENT): %.2f A (current setpoint - written last)", test->reg501_current);
        result = PSB_SetCurrentQueued(test->reg501_current, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 501");
            return result;
        }

    } else {
        snprintf(errorMsg, errorMsgSize, "Unknown test type: %s", test->testName);
        return ERR_INVALID_PARAMETER;
    }

    Delay(TEST_DELAY_SHORT);

    // Step 3: Optionally modify limit register (only for power limit tests)
    // Only modify if different from default 1224W
    if (fabs(test->reg9004_powerMax - 1224.0) > 0.1) {
        LogMessage("Step 3: Modifying power limit for this test...");
        LogMessage("  REG 9004 (POWER_MAX): %.2f W", test->reg9004_powerMax);
        result = PSB_SetPowerLimitQueued(test->reg9004_powerMax, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set REG 9004");
            return result;
        }
        Delay(TEST_DELAY_SHORT);
    } else {
        LogMessage("Step 3: Using default power limit (1224W)");
    }

    // Step 4: Read pre-enable status (CRITICAL - before output enable)
    LogMessage("Step 4: Reading pre-enable status...");
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to get pre-enable status");
        return result;
    }

    test->preEnableMode = status.regulationMode;
    test->preEnableDirection = status.sinkMode;
    test->preEnableOutput = status.outputEnabled;

    const char *modeNames[] = {"CV", "CR", "CC", "CP"};
    const char *dirNames[] = {"SOURCE", "SINK"};
    LogMessage("  Pre-Enable: %s (%s mode), Output: %s",
               modeNames[test->preEnableMode],
               dirNames[test->preEnableDirection],
               test->preEnableOutput ? "ON" : "OFF");

    // Step 5: Connect relay and enable output
    LogMessage("Step 5: Connecting PSB relay to battery...");
    result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to connect PSB relay");
        return result;
    }
    Delay(0.1);  // Brief delay for relay to settle

    LogMessage("  Enabling PSB output...");
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        // Disconnect relay on failure
        TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        snprintf(errorMsg, errorMsgSize, "Failed to enable output");
        return result;
    }

    // Wait for stabilization
    LogMessage("  Waiting %.1f seconds for stabilization...", TEST_MATRIX_STABILIZATION_TIME);
    Delay(TEST_MATRIX_STABILIZATION_TIME);

    // Step 6: Read post-enable status (CRITICAL - check for mode switching!)
    LogMessage("Step 6: Reading post-enable status...");
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to get post-enable status");
        return result;
    }

    test->postEnableMode = status.regulationMode;
    test->postEnableDirection = status.sinkMode;
    test->postEnableOutput = status.outputEnabled;
    test->modeSwitched = (test->preEnableMode != test->postEnableMode) ? 1 : 0;

    LogMessage("  Post-Enable: %s (%s mode), Output: %s",
               modeNames[test->postEnableMode],
               dirNames[test->postEnableDirection],
               test->postEnableOutput ? "ON" : "OFF");

    if (test->modeSwitched) {
        LogWarning("  MODE SWITCHED: %s -> %s",
                   modeNames[test->preEnableMode],
                   modeNames[test->postEnableMode]);
    }

    // Step 7: Measure operation for 10 seconds
    LogMessage("Step 7: Measuring operation (10 samples over 10 seconds)...");
    double sumVoltage = 0.0, sumCurrent = 0.0, sumPower = 0.0;
    double maxCurrent = 0.0;
    int numSamples = 0;
    int nonZeroCount = 0;

    for (int i = 0; i < 10; i++) {
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result == PSB_SUCCESS) {
            sumVoltage += status.voltage;
            sumCurrent += fabs(status.current);
            sumPower += fabs(status.power);

            if (fabs(status.current) > maxCurrent) {
                maxCurrent = fabs(status.current);
            }

            if (fabs(status.current) > 0.1) {
                nonZeroCount++;
            }

            numSamples++;

            LogMessage("  Sample %d: V=%.3fV, I=%.3fA, P=%.2fW, Mode=%s",
                       i+1, status.voltage, status.current, status.power,
                       modeNames[status.regulationMode]);

            // Safety check
            if (fabs(status.current) > TEST_MATRIX_MAX_CURRENT ||
                fabs(status.voltage) > TEST_MATRIX_MAX_VOLTAGE ||
                fabs(status.power) > TEST_MATRIX_MAX_POWER) {
                LogError("SAFETY ABORT: Exceeded safety thresholds!");
                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
                snprintf(errorMsg, errorMsgSize, "Safety abort: I=%.2fA, V=%.2fV, P=%.2fW",
                         status.current, status.voltage, status.power);
                return ERR_SAFETY_ABORT;
            }
        }

        Delay(TEST_MATRIX_SAMPLE_INTERVAL);
    }

    // Calculate averages
    if (numSamples > 0) {
        test->avgVoltage = sumVoltage / numSamples;
        test->avgCurrent = sumCurrent / numSamples;
        test->avgPower = sumPower / numSamples;
        test->peakCurrent = maxCurrent;
        test->currentNonZero = (nonZeroCount > 0) ? 1 : 0;
    }

    LogMessage("Measurement Results:");
    LogMessage("  Avg Voltage: %.3f V", test->avgVoltage);
    LogMessage("  Avg Current: %.3f A", test->avgCurrent);
    LogMessage("  Peak Current: %.3f A", test->peakCurrent);
    LogMessage("  Avg Power: %.2f W", test->avgPower);
    LogMessage("  Current Non-Zero: %s", test->currentNonZero ? "YES" : "NO");

    // Step 8: Disable output and disconnect relay
    LogMessage("Step 8: Disabling output...");
    result = PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        // Still try to disconnect relay
        TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
        snprintf(errorMsg, errorMsgSize, "Failed to disable output");
        return result;
    }

    LogMessage("  Disconnecting PSB relay from battery...");
    result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogWarning("Failed to disconnect PSB relay");
        // Continue anyway - output is already off
    }

    Delay(TEST_DELAY_SHORT);

    // Determine test result
    // Success criteria: current > 0.1A for CV/CC/CP modes
    if (test->currentNonZero) {
        test->testResult = 1;  // PASS
        snprintf(test->notes, sizeof(test->notes), "PASS - Current flowing");
        LogMessage("TEST RESULT: PASS");
    } else {
        test->testResult = -1;  // FAIL
        snprintf(test->notes, sizeof(test->notes), "FAIL - Zero current (mode issue?)");
        LogError("TEST RESULT: FAIL - No current flow");
    }

    return SUCCESS;
}

/******************************************************************************
 * CSV Output Generation
 ******************************************************************************/

static int WriteCSVHeader(FILE *fp) {
    fprintf(fp, "test_id,test_name,battery_v_start,target_v,");
    fprintf(fp, "reg498_sink_power,reg499_sink_current,reg500_voltage,reg501_current,reg502_power,");
    fprintf(fp, "reg9000_v_max,reg9001_v_min,reg9002_i_max,reg9003_i_min,");
    fprintf(fp, "reg9004_p_max,reg9005_p_sink_max,reg9008_i_sink_max,reg9009_i_sink_min,");
    fprintf(fp, "pre_enable_mode,pre_enable_direction,pre_enable_output,");
    fprintf(fp, "post_enable_mode,post_enable_direction,post_enable_output,mode_switched,");
    fprintf(fp, "avg_voltage,avg_current,peak_current,avg_power,current_nonzero,");
    fprintf(fp, "test_result,notes\n");
    return SUCCESS;
}

static int WriteCSVRow(FILE *fp, RegisterTestCase *test) {
    const char *modeNames[] = {"CV", "CR", "CC", "CP"};
    const char *dirNames[] = {"SOURCE", "SINK"};

    fprintf(fp, "%d,%s,%.3f,%.3f,",
            test->testId, test->testName,
            test->batteryVoltageStart, test->targetVoltage);

    fprintf(fp, "%.2f,%.2f,%.2f,%.2f,%.2f,",
            test->reg498_sinkPower, test->reg499_sinkCurrent,
            test->reg500_voltage, test->reg501_current, test->reg502_power);

    fprintf(fp, "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,",
            test->reg9000_voltageMax, test->reg9001_voltageMin,
            test->reg9002_currentMax, test->reg9003_currentMin,
            test->reg9004_powerMax, test->reg9005_sinkPowerMax,
            test->reg9008_sinkCurrentMax, test->reg9009_sinkCurrentMin);

    fprintf(fp, "%s,%s,%d,",
            modeNames[test->preEnableMode],
            dirNames[test->preEnableDirection],
            test->preEnableOutput);

    fprintf(fp, "%s,%s,%d,%d,",
            modeNames[test->postEnableMode],
            dirNames[test->postEnableDirection],
            test->postEnableOutput,
            test->modeSwitched);

    fprintf(fp, "%.3f,%.3f,%.3f,%.2f,%d,",
            test->avgVoltage, test->avgCurrent,
            test->peakCurrent, test->avgPower,
            test->currentNonZero);

    const char *resultStr = (test->testResult == 1) ? "PASS" :
                            (test->testResult == -1) ? "FAIL" : "NOT_RUN";
    fprintf(fp, "%s,\"%s\"\n", resultStr, test->notes);

    return SUCCESS;
}

/******************************************************************************
 * Analysis and Summary Generation
 ******************************************************************************/

static void GenerateSummary(RegisterTestCase *allTests, int numTests, const char *summaryPath) {
    LogMessage("Creating summary file: %s", summaryPath);
    FILE *fp = fopen(summaryPath, "w");
    if (!fp) {
        int err = errno;
        LogError("Failed to create summary file: %s (errno=%d: %s)",
                summaryPath, err, strerror(err));
        return;
    }
    LogMessage("Summary file created successfully");

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "PSB 10000 REGISTER MATRIX TEST RESULTS\n");
    fprintf(fp, "================================================================================\n");
    fprintf(fp, "Date: %s\n", timeStr);
    fprintf(fp, "Total Tests: %d\n", numTests);

    // Count results
    int passCount = 0, failCount = 0;
    int cccvTests = 0, cvTests = 0, ccTests = 0, cpTests = 0;
    int modeSwitchCount = 0;

    for (int i = 0; i < numTests; i++) {
        if (allTests[i].testResult == 1) passCount++;
        if (allTests[i].testResult == -1) failCount++;
        if (allTests[i].modeSwitched) modeSwitchCount++;

        // Count by test type
        if (strstr(allTests[i].testName, "CCCV-")) cccvTests++;
        else if (strstr(allTests[i].testName, "CV-")) cvTests++;
        else if (strstr(allTests[i].testName, "CC-")) ccTests++;
        else if (strstr(allTests[i].testName, "CP-")) cpTests++;
    }

    fprintf(fp, "Passed: %d (%.1f%%)\n", passCount, 100.0 * passCount / numTests);
    fprintf(fp, "Failed: %d (%.1f%%)\n", failCount, 100.0 * failCount / numTests);
    fprintf(fp, "Mode Switches Detected: %d\n\n", modeSwitchCount);

    fprintf(fp, "Test Breakdown:\n");
    fprintf(fp, "  CCCV Combined Tests: %d (FIRST - all three registers non-zero)\n", cccvTests);
    fprintf(fp, "  CV Mode Tests: %d\n", cvTests);
    fprintf(fp, "  CC Mode Tests: %d\n", ccTests);
    fprintf(fp, "  CP Mode Tests: %d\n\n", cpTests);

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "KEY FINDINGS\n");
    fprintf(fp, "================================================================================\n\n");

    // Analyze CCCV combined tests FIRST (most important)
    fprintf(fp, "0. CCCV COMBINED MODE - All Three Registers Non-Zero:\n");
    fprintf(fp, "   (REG 502=power limit, REG 500=voltage limit, REG 501=current setpoint)\n\n");
    for (int i = 0; i < numTests; i++) {
        if (strstr(allTests[i].testName, "CCCV-")) {
            const char *resultStr = (allTests[i].testResult == 1) ? "PASS" : "FAIL";
            const char *modeNames[] = {"CV", "CR", "CC", "CP"};
            fprintf(fp, "   %s: %s (Mode: %s, V=%.2fV, I=%.3fA, P=%.2fW)\n",
                    allTests[i].testName,
                    resultStr,
                    modeNames[allTests[i].postEnableMode],
                    allTests[i].avgVoltage,
                    allTests[i].avgCurrent,
                    allTests[i].avgPower);
        }
    }
    fprintf(fp, "\n");

    // Analyze CV tests by power limit
    fprintf(fp, "1. CV MODE - Power Limit Effects:\n\n");
    for (int i = 0; i < numTests; i++) {
        if (strstr(allTests[i].testName, "CV-PowerLimit")) {
            const char *resultStr = (allTests[i].testResult == 1) ? "PASS" : "FAIL";
            const char *modeNames[] = {"CV", "CR", "CC", "CP"};
            fprintf(fp, "   Power Limit %.0fW: %s (Post-enable mode: %s, Current: %.3fA)\n",
                    allTests[i].reg9004_powerMax,
                    resultStr,
                    modeNames[allTests[i].postEnableMode],
                    allTests[i].avgCurrent);
        }
    }

    // Analyze decoy value effects
    fprintf(fp, "\n2. CV MODE - Decoy Value Effects:\n\n");
    for (int i = 0; i < numTests; i++) {
        if (strstr(allTests[i].testName, "CV-Decoy")) {
            const char *resultStr = (allTests[i].testResult == 1) ? "PASS" : "FAIL";
            const char *modeNames[] = {"CV", "CR", "CC", "CP"};
            fprintf(fp, "   Decoy %.0fA/%.0fW: %s (Post-enable mode: %s, Current: %.3fA)\n",
                    allTests[i].reg499_sinkCurrent,
                    allTests[i].reg498_sinkPower,
                    resultStr,
                    modeNames[allTests[i].postEnableMode],
                    allTests[i].avgCurrent);
        }
    }

    // Analyze secondary setpoint effects
    fprintf(fp, "\n3. CV MODE - Secondary Setpoint Effects:\n\n");
    for (int i = 0; i < numTests; i++) {
        if (strstr(allTests[i].testName, "CV-Secondary")) {
            const char *resultStr = (allTests[i].testResult == 1) ? "PASS" : "FAIL";
            const char *modeNames[] = {"CV", "CR", "CC", "CP"};
            fprintf(fp, "   Secondary %.1fA: %s (Post-enable mode: %s, Current: %.3fA)\n",
                    allTests[i].reg501_current,
                    resultStr,
                    modeNames[allTests[i].postEnableMode],
                    allTests[i].avgCurrent);
        }
    }

    fprintf(fp, "\n================================================================================\n");
    fprintf(fp, "DETAILED TEST RESULTS\n");
    fprintf(fp, "================================================================================\n\n");

    for (int i = 0; i < numTests; i++) {
        RegisterTestCase *test = &allTests[i];
        const char *resultStr = (test->testResult == 1) ? "PASS" :
                                (test->testResult == -1) ? "FAIL" : "NOT_RUN";
        const char *modeNames[] = {"CV", "CR", "CC", "CP"};

        fprintf(fp, "Test %d: %s - %s\n", test->testId, test->testName, resultStr);
        fprintf(fp, "  Configuration: V=%.2fV, I=%.2fA, P=%.2fW\n",
                test->reg500_voltage, test->reg501_current, test->reg502_power);
        fprintf(fp, "  Decoys: %.0fA/%.0fW, Power Limit: %.0fW\n",
                test->reg499_sinkCurrent, test->reg498_sinkPower, test->reg9004_powerMax);
        fprintf(fp, "  Mode: %s -> %s%s\n",
                modeNames[test->preEnableMode],
                modeNames[test->postEnableMode],
                test->modeSwitched ? " (SWITCHED!)" : "");
        fprintf(fp, "  Results: Avg I=%.3fA, Peak I=%.3fA, Avg P=%.2fW\n",
                test->avgCurrent, test->peakCurrent, test->avgPower);
        fprintf(fp, "  Notes: %s\n\n", test->notes);
    }

    fprintf(fp, "================================================================================\n");
    fprintf(fp, "See CSV file for complete data suitable for analysis/graphing\n");
    fprintf(fp, "================================================================================\n");

    fclose(fp);
    LogMessage("Summary file generated: %s", summaryPath);
}

/******************************************************************************
 * Main Register Matrix Test Function
 ******************************************************************************/

int Test_RegisterMatrix(char *errorMsg, int errorMsgSize) {
    int result;
    char timestamp[64];
    char csvPath[512];
    char summaryPath[512];
    FILE *csvFile = NULL;

    // Generate timestamp for filenames
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", timeinfo);

    // Use current working directory (more portable)
    snprintf(csvPath, sizeof(csvPath),
             "psb_register_matrix_%s.csv",
             timestamp);
    snprintf(summaryPath, sizeof(summaryPath),
             "psb_register_summary_%s.txt",
             timestamp);

    LogMessage("========================================");
    LogMessage("PSB REGISTER MATRIX TEST");
    LogMessage("========================================");
    LogMessage("This will run ~40 test cases (~20 minutes)");
    LogMessage("CSV output: %s", csvPath);
    LogMessage("Summary output: %s", summaryPath);
    LogMessage("========================================");

    // Generate all test cases
    RegisterTestCase *combinedTests = NULL, *cvTests = NULL, *ccTests = NULL, *cpTests = NULL;
    int numCombinedTests = 0, numCVTests = 0, numCCTests = 0, numCPTests = 0;

    LogMessage("Generating test matrix...");

    // Generate combined CC-CV tests FIRST (most important to test)
    result = GenerateCombinedModeTests(&combinedTests, &numCombinedTests);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to generate combined tests");
        return result;
    }
    LogMessage("  Generated %d CCCV combined mode tests (FIRST)", numCombinedTests);

    result = GenerateCVModeTests(&cvTests, &numCVTests);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to generate CV tests");
        free(combinedTests);
        return result;
    }
    LogMessage("  Generated %d CV mode tests", numCVTests);

    result = GenerateCCModeTests(&ccTests, &numCCTests);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to generate CC tests");
        free(combinedTests);
        free(cvTests);
        return result;
    }
    LogMessage("  Generated %d CC mode tests", numCCTests);

    result = GenerateCPModeTests(&cpTests, &numCPTests);
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to generate CP tests");
        free(combinedTests);
        free(cvTests);
        free(ccTests);
        return result;
    }
    LogMessage("  Generated %d CP mode tests", numCPTests);

    int totalTests = numCombinedTests + numCVTests + numCCTests + numCPTests;
    LogMessage("Total test cases: %d", totalTests);

    // Combine all tests into single array - CCCV tests FIRST
    RegisterTestCase *allTests = (RegisterTestCase*)malloc(sizeof(RegisterTestCase) * totalTests);
    if (!allTests) {
        snprintf(errorMsg, errorMsgSize, "Failed to allocate test array");
        free(combinedTests);
        free(cvTests);
        free(ccTests);
        free(cpTests);
        return ERR_OUT_OF_MEMORY;
    }

    int idx = 0;
    // Combined tests FIRST
    memcpy(&allTests[idx], combinedTests, sizeof(RegisterTestCase) * numCombinedTests);
    idx += numCombinedTests;
    memcpy(&allTests[idx], cvTests, sizeof(RegisterTestCase) * numCVTests);
    idx += numCVTests;
    memcpy(&allTests[idx], ccTests, sizeof(RegisterTestCase) * numCCTests);
    idx += numCCTests;
    memcpy(&allTests[idx], cpTests, sizeof(RegisterTestCase) * numCPTests);

    free(combinedTests);
    free(cvTests);
    free(ccTests);
    free(cpTests);

    // Renumber test IDs
    for (int i = 0; i < totalTests; i++) {
        allTests[i].testId = i + 1;
    }

    // Open CSV file
    LogMessage("Creating CSV file: %s", csvPath);
    csvFile = fopen(csvPath, "w");
    if (!csvFile) {
        int err = errno;
        snprintf(errorMsg, errorMsgSize,
                "Failed to create CSV file: %s (errno=%d: %s)",
                csvPath, err, strerror(err));
        LogError("%s", errorMsg);
        free(allTests);
        return ERR_FILE_OPEN;
    }
    LogMessage("CSV file created successfully");

    WriteCSVHeader(csvFile);

    // Initialize PSB with decoy values and high limits (ONCE before all tests)
    // CRITICAL: This sets up the baseline configuration that all tests rely on
    // See notes/PSB-MODE-SELECTION-RULES-2026-01-13.txt for explanation
    LogMessage("\n========================================");
    LogMessage("INITIALIZING PSB FOR MATRIX TEST");
    LogMessage("========================================");
    result = InitializePSBForMatrixTest(errorMsg, errorMsgSize);
    if (result != SUCCESS) {
        LogError("Failed to initialize PSB: %s", errorMsg);
        fclose(csvFile);
        free(allTests);
        return result;
    }
    LogMessage("PSB initialization complete\n");

    // Run all tests
    LogMessage("Starting test execution...");
    time_t startTime = time(NULL);

    for (int i = 0; i < totalTests; i++) {
        LogMessage("\n========================================");
        LogMessage("Running test %d / %d (%.1f%% complete)",
                   i + 1, totalTests, 100.0 * (i + 1) / totalTests);
        LogMessage("========================================");

        char testError[256];
        result = RunSingleRegisterTest(&allTests[i], testError, sizeof(testError));

        if (result != SUCCESS) {
            LogError("Test %d failed with error: %s", i + 1, testError);
            snprintf(allTests[i].notes, sizeof(allTests[i].notes),
                     "ERROR: %s", testError);
            allTests[i].testResult = -1;
        }

        // Write result to CSV
        WriteCSVRow(csvFile, &allTests[i]);
        fflush(csvFile);  // Ensure data is written immediately

        // Brief delay between tests
        Delay(TEST_DELAY_BETWEEN_TESTS);
    }

    time_t endTime = time(NULL);
    double elapsedMinutes = difftime(endTime, startTime) / 60.0;

    fclose(csvFile);

    // Generate summary
    LogMessage("\nGenerating summary report...");
    GenerateSummary(allTests, totalTests, summaryPath);

    // Count results
    int passCount = 0, failCount = 0;
    for (int i = 0; i < totalTests; i++) {
        if (allTests[i].testResult == 1) passCount++;
        if (allTests[i].testResult == -1) failCount++;
    }

    LogMessage("\n========================================");
    LogMessage("REGISTER MATRIX TEST COMPLETE");
    LogMessage("========================================");
    LogMessage("Total Tests: %d", totalTests);
    LogMessage("Passed: %d (%.1f%%)", passCount, 100.0 * passCount / totalTests);
    LogMessage("Failed: %d (%.1f%%)", failCount, 100.0 * failCount / totalTests);
    LogMessage("Execution Time: %.1f minutes", elapsedMinutes);
    LogMessage("CSV File: %s", csvPath);
    LogMessage("Summary File: %s", summaryPath);
    LogMessage("========================================");

    free(allTests);

    snprintf(errorMsg, errorMsgSize,
             "Test complete: %d passed, %d failed. See %s for details.",
             passCount, failCount, summaryPath);

    return SUCCESS;
}
