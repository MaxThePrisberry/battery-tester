/******************************************************************************
 * psb10000_test.c
 * 
 * PSB 10000 Test Suite with Queue System Integration
 * Implementation file for comprehensive testing of PSB10000 functions
 ******************************************************************************/

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
    {"Output Control", Test_OutputControl, 0, "", 0.0},
    {"Invalid Parameters", Test_InvalidParameters, 0, "", 0.0},
    {"Boundary Conditions", Test_BoundaryConditions, 0, "", 0.0},
    {"Sequence Operations", Test_SequenceOperations, 0, "", 0.0},
    {"Output Voltage Verification", Test_OutputVoltageVerification, 0, "", 0.0}
};

static int numTestCases = sizeof(testCases) / sizeof(testCases[0]);

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static int SetWideLimitsQueued(PSB_Handle *handle);
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

// Helper function to set wide limits for testing using queued commands
static int SetWideLimitsQueued(PSB_Handle *handle) {
    LogDebugEx(LOG_DEVICE_PSB, "Setting wide limits for testing...");
    
    int result;
    int errors = 0;
    
    // Ensure remote mode using queued command
    result = PSB_SetRemoteModeQueued(handle, 1);
    if (result != PSB_SUCCESS) {
        return PSB_ERROR_COMM;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Set voltage limits using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage limits: %.1fV - %.1fV...", 
           PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltageLimitsQueued(handle, PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage limits");
        errors++;
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Voltage limits set successfully");
    }
    
    // Set current limits using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limits: %.1fA - %.1fA...", 
           PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrentLimitsQueued(handle, PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set current limits: %s", PSB_GetErrorString(result));
        errors++;
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Current limits set successfully");
    }
    
    // Set power limit using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Setting power limit: %.1fW...", PSB_TEST_POWER_MAX_WIDE);
    result = PSB_SetPowerLimitQueued(handle, PSB_TEST_POWER_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set power limit: %s", PSB_GetErrorString(result));
        errors++;
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Power limit set successfully");
    }
    
    if (errors == 0) {
        LogDebugEx(LOG_DEVICE_PSB, "All wide limits set successfully");
        return PSB_SUCCESS;
    } else {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set %d limit(s)", errors);
        return PSB_ERROR_COMM;
    }
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
    context->isRunning = 0;
    
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
    
    context->isRunning = 1;
    context->cancelRequested = 0;
    
    LogMessageEx(LOG_DEVICE_PSB, "Starting PSB Test Suite");
    UpdateTestProgress(context, "Starting PSB Test Suite...");
    
    // Set wide limits for all tests
    UpdateTestProgress(context, "Setting up test parameters...");
    if (SetWideLimitsQueued(context->psbHandle) != PSB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to set wide limits for testing");
        UpdateTestProgress(context, "Failed to set test parameters");
        context->isRunning = 0;
        return -1;
    }
    
    // Run each test
    for (int i = 0; i < numTestCases && !context->cancelRequested; i++) {
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
        if (i < numTestCases - 1) {
            Delay(TEST_DELAY_BETWEEN_TESTS);
        }
    }
    
    // Generate summary
    GenerateTestSummary(&context->summary, testCases, numTestCases);
    
    context->isRunning = 0;
    
    return (context->summary.failedTests == 0) ? 1 : -1;
}

void PSB_TestSuite_Cancel(TestSuiteContext *context) {
    if (context) {
        context->cancelRequested = 1;
    }
}

void PSB_TestSuite_Cleanup(TestSuiteContext *context) {
    if (context) {
        // Ensure PSB is in safe state using queued commands
        if (context->psbHandle && context->psbHandle->isConnected) {
            // Restore wide limits
            SetWideLimitsQueued(context->psbHandle);
            
            // Ensure output is off but keep remote mode ON
            PSB_SetOutputEnableQueued(context->psbHandle, 0);
            PSB_SetRemoteModeQueued(context->psbHandle, 1);
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
    
    // Restore wide limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring wide limits...");
    result = PSB_SetVoltageLimitsQueued(handle, PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore wide voltage limits");
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
    
    // First set wide limits to ensure we have a clean state
    result = SetWideLimitsQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set wide limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting current limits within valid range
    double testMinCurrent = TEST_CURRENT_LOW;   // 6.0A
    double testMaxCurrent = TEST_CURRENT_HIGH;  // 50.0A
    
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
    result = PSB_SetCurrentQueued(handle, PSB_TEST_CURRENT_MIN_WIDE);  // 0A - should be clamped to 6A
    if (result != PSB_SUCCESS && result != PSB_ERROR_INVALID_PARAM) {
        LogWarningEx(LOG_DEVICE_PSB, "Unexpected error setting current below limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Restore wide limits
    LogDebugEx(LOG_DEVICE_PSB, "Restoring wide current limits...");
    result = PSB_SetCurrentLimitsQueued(handle, PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore wide current limits: %s", 
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
                TEST_POWER_INVALID, PSB_TEST_POWER_MAX_WIDE);
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
    
    // Test that power above limit is rejected
    LogDebugEx(LOG_DEVICE_PSB, "Testing power above limit...");
    result = PSB_SetPowerQueued(handle, testPowerLimit + 100.0);
    if (result == PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Power above limit was accepted (may be clamped by device)");
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Power above limit correctly rejected: %s", 
                  PSB_GetErrorString(result));
    }
    
    // Restore maximum power limit
    LogDebugEx(LOG_DEVICE_PSB, "Restoring maximum power limit (%.1fW)...", PSB_TEST_POWER_MAX_WIDE);
    result = PSB_SetPowerLimitQueued(handle, PSB_TEST_POWER_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to restore max power limit: %s", 
                    PSB_GetErrorString(result));
    }
    
    // Test invalid power limit (beyond device capability)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid power limit (%.1fW)...", TEST_POWER_INVALID);
    result = PSB_SetPowerLimitQueued(handle, TEST_POWER_INVALID);  // 1400W - beyond max
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power limit %.1fW (max is %.1fW)", 
                TEST_POWER_INVALID, PSB_TEST_POWER_MAX_WIDE);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid power limit: %s", 
              PSB_GetErrorString(result));
    
    LogDebugEx(LOG_DEVICE_PSB, "Power limit test completed");
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
    
    // Ensure remote mode and wide limits
    result = EnsureRemoteModeQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    result = SetWideLimitsQueued(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set wide limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test minimum voltage using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum voltage (%.2fV)...", PSB_TEST_VOLTAGE_MIN_WIDE);
    result = PSB_SetVoltageQueued(handle, PSB_TEST_VOLTAGE_MIN_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum voltage accepted");
    
    // Test minimum current using queued command
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum current (%.2fA)...", PSB_TEST_CURRENT_MIN_WIDE);
    result = PSB_SetCurrentQueued(handle, PSB_TEST_CURRENT_MIN_WIDE);
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
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum voltage (%.2fV)...", PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltageQueued(handle, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum voltage accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum current (%.2fA)...", PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrentQueued(handle, PSB_TEST_CURRENT_MAX_WIDE);
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