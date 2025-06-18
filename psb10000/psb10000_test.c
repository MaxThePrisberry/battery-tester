/******************************************************************************
 * PSB 10000 Test Suite
 * Implementation file for comprehensive testing of PSB10000 functions
 ******************************************************************************/

#include "psb10000_test.h"
#include "common.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

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

static int SetWideLimits(PSB_Handle *handle);
static int EnsureRemoteMode(PSB_Handle *handle);

static double GetTime(void) {
    return Timer();
}

void UpdateTestProgress(TestSuiteContext *context, const char *message) {
    if (context && context->progressCallback) {
        context->progressCallback(message);
    }
    
    if (context && context->panelHandle > 0 && context->statusStringControl > 0) {
        // Create a static copy of the message to pass to deferred call
        static char statusMessage[256];
        strncpy(statusMessage, message, sizeof(statusMessage) - 1);
        statusMessage[sizeof(statusMessage) - 1] = '\0';
        
        // Post deferred call with SetCtrlVal
        // Note: This is a simplified approach - in production code you might need
        // a more sophisticated mechanism to pass multiple parameters
        SetCtrlVal(context->panelHandle, context->statusStringControl, statusMessage);
        ProcessSystemEvents();
    }
}

static int CompareDouble(double a, double b, double tolerance) {
    return fabs(a - b) <= tolerance;
}

// Helper function to acknowledge any pending alarms
static int AcknowledgeAlarms(PSB_Handle *handle) {
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_COIL;
    txBuffer[2] = 0x01; // Register 411 high byte
    txBuffer[3] = 0x9B; // Register 411 low byte
    txBuffer[4] = 0xFF; // Coil value high
    txBuffer[5] = 0x00; // Coil value low
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    LogDebugEx(LOG_DEVICE_PSB, "Acknowledging any pending alarms...");
    
    // Clear input buffer first
    FlushInQ(handle->comPort);
    
    if (ComWrt(handle->comPort, (char*)txBuffer, 8) != 8) {
        LogDebugEx(LOG_DEVICE_PSB, "Failed to send alarm acknowledge command");
        return PSB_ERROR_COMM;
    }
    
    Delay(0.1); // Give device time to process
    
    // Read response (we don't really care about it, just need to clear the buffer)
    ComRd(handle->comPort, (char*)rxBuffer, 8);
    
    return PSB_SUCCESS;
}

// Helper function to ensure remote mode is enabled without spamming commands
static int EnsureRemoteMode(PSB_Handle *handle) {
    PSB_Status status;
    int result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_PSB, "Failed to get status for remote mode check: %s", 
                   PSB_GetErrorString(result));
        return result;
    }
    
    // Only set remote mode if it's not already enabled
    if (!status.remoteMode) {
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode is OFF, enabling it...");
        result = PSB_SetRemoteMode(handle, 1);
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

// Helper function to set wide limits for testing
static int SetWideLimits(PSB_Handle *handle) {
    LogDebugEx(LOG_DEVICE_PSB, "Setting wide limits for testing...");
    
    int result;
    int errors = 0;
    
    // Ensure remote mode is on (but don't spam if already on)
    result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        return PSB_ERROR_COMM;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // For voltage limits, we need to be careful about order
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage limits: %.1fV - %.1fV...", 
           PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltageLimits(handle, PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        // If that fails, we might need to set max first (if current max is too low)
        // or min first (if current min is too high)
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set voltage limits");
        errors++;
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Voltage limits set successfully");
    }
    
    // Set current limits
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limits: %.1fA - %.1fA...", 
           PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrentLimits(handle, PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to set current limits: %s", PSB_GetErrorString(result));
        errors++;
    } else {
        LogDebugEx(LOG_DEVICE_PSB, "Current limits set successfully");
    }
    
    // Set power limit
    LogDebugEx(LOG_DEVICE_PSB, "Setting power limit: %.1fW...", PSB_TEST_POWER_MAX_WIDE);
    result = PSB_SetPowerLimit(handle, PSB_TEST_POWER_MAX_WIDE);
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
    
    context->isRunning = 1;
    context->cancelRequested = 0;
    context->summary.totalTests = numTestCases;
    context->summary.passedTests = 0;
    context->summary.failedTests = 0;
    
    double suiteStartTime = GetTime();
    
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    LogMessageEx(LOG_DEVICE_PSB, "PSB 10000 TEST SUITE STARTING");
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    
    UpdateTestProgress(context, "PSB Test Suite Starting...");
    
    // Enable debug output for detailed analysis
    PSB_EnableDebugOutput(g_debugMode);
    
    // IMPORTANT: Acknowledge any pending alarms before starting tests
    LogDebugEx(LOG_DEVICE_PSB, "Preparing device for testing...");
    AcknowledgeAlarms(context->psbHandle);
    Delay(0.5); // Give device time to settle
    
    // CRITICAL: Set wide limits before starting any tests
    UpdateTestProgress(context, "Setting wide limits for testing...");
    SetWideLimits(context->psbHandle);
    Delay(0.5);
    
    // Run each test
    for (int i = 0; i < numTestCases; i++) {
        if (context->cancelRequested) {
            LogWarningEx(LOG_DEVICE_PSB, "TEST SUITE CANCELLED BY USER");
            UpdateTestProgress(context, "Test suite cancelled");
            break;
        }
        
        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s", 
                i + 1, numTestCases, testCases[i].testName);
        UpdateTestProgress(context, progressMsg);
        
        LogMessageEx(LOG_DEVICE_PSB, "--- Testing: %s ---", testCases[i].testName);
        
        double testStartTime = GetTime();
        
        // Run the test
        testCases[i].result = testCases[i].testFunction(
            context->psbHandle, 
            testCases[i].errorMessage, 
            sizeof(testCases[i].errorMessage)
        );
        
        testCases[i].executionTime = GetTime() - testStartTime;
        
        // Update counts and log result
        if (testCases[i].result > 0) {
            context->summary.passedTests++;
            LogMessageEx(LOG_DEVICE_PSB, "[PASS] %s", testCases[i].testName);
        } else {
            context->summary.failedTests++;
            LogMessageEx(LOG_DEVICE_PSB, "[FAIL] %s", testCases[i].testName);
            if (strlen(testCases[i].errorMessage) > 0) {
                LogMessageEx(LOG_DEVICE_PSB, "       Error: %s", testCases[i].errorMessage);
            }
            strncpy(context->summary.lastError, testCases[i].errorMessage, 
                   sizeof(context->summary.lastError) - 1);
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Test execution time: %.2f seconds", testCases[i].executionTime);
        
        // Small delay between tests
        Delay(0.5);
    }
    
    // Restore wide limits AFTER all tests complete
    LogDebugEx(LOG_DEVICE_PSB, "--- Cleanup ---");
    UpdateTestProgress(context, "Restoring wide limits...");
    SetWideLimits(context->psbHandle);
    
    // Leave the PSB in the state that status.c expects (remote mode ON, output OFF)
    EnsureRemoteMode(context->psbHandle);
    PSB_SetOutputEnable(context->psbHandle, 0);
    
    context->summary.executionTime = GetTime() - suiteStartTime;
    
    // Print summary
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    LogMessageEx(LOG_DEVICE_PSB, "TEST SUITE SUMMARY");
    LogMessageEx(LOG_DEVICE_PSB, "========================================");
    LogMessageEx(LOG_DEVICE_PSB, "Total Tests: %d", context->summary.totalTests);
    LogMessageEx(LOG_DEVICE_PSB, "Passed: %d", context->summary.passedTests);
    LogMessageEx(LOG_DEVICE_PSB, "Failed: %d", context->summary.failedTests);
    LogMessageEx(LOG_DEVICE_PSB, "Total Time: %.2f seconds", context->summary.executionTime);
    
    if (context->summary.failedTests > 0) {
        LogMessageEx(LOG_DEVICE_PSB, "Last Error: %s", context->summary.lastError);
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Detailed Results:");
    for (int i = 0; i < numTestCases; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "  %-30s: %s (%.2fs)", 
               testCases[i].testName,
               testCases[i].result > 0 ? "PASS" : "FAIL",
               testCases[i].executionTime);
        if (testCases[i].result <= 0 && strlen(testCases[i].errorMessage) > 0) {
            LogDebugEx(LOG_DEVICE_PSB, "                                  %s", testCases[i].errorMessage);
        }
    }
    
    char summaryMsg[256];
    snprintf(summaryMsg, sizeof(summaryMsg), 
             "Test Suite Complete: %d/%d passed (%.1f%%)", 
             context->summary.passedTests, 
             context->summary.totalTests,
             (context->summary.passedTests * 100.0) / context->summary.totalTests);
    UpdateTestProgress(context, summaryMsg);
    
    context->isRunning = 0;
    return context->summary.failedTests == 0 ? 1 : -1;
}

void PSB_TestSuite_Cancel(TestSuiteContext *context) {
    if (context) {
        context->cancelRequested = 1;
    }
}

void PSB_TestSuite_Cleanup(TestSuiteContext *context) {
    if (context) {
        // Ensure PSB is in safe state expected by status.c
        if (context->psbHandle && context->psbHandle->isConnected) {
            // Restore wide limits
            SetWideLimits(context->psbHandle);
            
            // Ensure output is off but keep remote mode ON (expected by status.c)
            PSB_SetOutputEnable(context->psbHandle, 0);
            EnsureRemoteMode(context->psbHandle);
        }
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

// Connection Status test removed - handled by status.c module

int Test_RemoteMode(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing remote mode control...");
    
    PSB_Status status;
    int result;
    int initialRemoteState;
    
    // First, check initial state
    LogDebugEx(LOG_DEVICE_PSB, "Reading initial state...");
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read initial status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    initialRemoteState = status.remoteMode;
    LogDebugEx(LOG_DEVICE_PSB, "Initial state - Remote mode: %s, Control location: 0x%02X", 
           status.remoteMode ? "ON" : "OFF", 
           status.controlLocation);
    
    // Check if device is in local mode (which would prevent remote control)
    if (status.controlLocation == CONTROL_LOCAL) {
        LogWarningEx(LOG_DEVICE_PSB, "Device is in LOCAL mode - remote control may be blocked");
        LogWarningEx(LOG_DEVICE_PSB, "Please ensure 'Allow remote control' is enabled on the device");
    }
    
    // Acknowledge any pending alarms
    AcknowledgeAlarms(handle);
    Delay(0.2);
    
    // Only test toggling if we won't break the system state
    // Since status.c expects remote mode to be ON, we'll test turning it OFF and back ON
    // but ensure we leave it in the ON state
    
    if (initialRemoteState) {
        // Remote mode is already ON (expected from status.c)
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode is already ON (expected state)");
        
        // Test turning it OFF briefly
        LogDebugEx(LOG_DEVICE_PSB, "Testing toggle: turning remote mode OFF...");
        result = PSB_SetRemoteMode(handle, 0);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn off remote mode: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(0.5); // Brief delay
        
        // Verify it's off
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after turning off remote: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        if (status.remoteMode != 0) {
            snprintf(errorMsg, errorMsgSize, "Remote mode should be OFF but status shows ON");
            return -1;
        }
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode successfully turned OFF");
        
        // Turn it back ON immediately
        LogDebugEx(LOG_DEVICE_PSB, "Restoring remote mode to ON...");
        result = PSB_SetRemoteMode(handle, 1);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to restore remote mode: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(0.5);
        
        // Verify it's back on
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after restoring remote: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        if (status.remoteMode != 1) {
            snprintf(errorMsg, errorMsgSize, "Failed to restore remote mode to ON");
            return -1;
        }
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode successfully restored to ON");
        
    } else {
        // Remote mode is OFF (unexpected, but let's handle it)
        LogWarningEx(LOG_DEVICE_PSB, "Remote mode is OFF (unexpected) - turning it ON");
        
        result = PSB_SetRemoteMode(handle, 1);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to turn on remote mode: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(0.5);
        
        // Verify it's on
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status after turning on remote: %s", 
                    PSB_GetErrorString(result));
            return -1;
        }
        
        if (status.remoteMode != 1) {
            snprintf(errorMsg, errorMsgSize, "Remote mode should be ON but status shows OFF");
            return -1;
        }
        LogDebugEx(LOG_DEVICE_PSB, "Remote mode successfully turned ON");
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Remote mode control test completed - left in ON state");
    return 1;
}

int Test_StatusRegisterReading(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing status register reading (debugging focus)...");
    
    PSB_Status status;
    int result;
    
    // Read status multiple times to check consistency
    LogDebugEx(LOG_DEVICE_PSB, "Reading status 5 times to check consistency...");
    
    for (int i = 0; i < 5; i++) {
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status on attempt %d: %s", 
                    i + 1, PSB_GetErrorString(result));
            return -1;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Read #%d:", i + 1);
        PSB_PrintStatus(&status);
        
        // Sanity checks
        if (status.voltage < 0 || status.voltage > PSB_NOMINAL_VOLTAGE * 1.25) {
            snprintf(errorMsg, errorMsgSize, "Invalid voltage reading: %.2fV", status.voltage);
            return -1;
        }
        
        if (status.current < 0 || status.current > PSB_NOMINAL_CURRENT * 1.25) {
            snprintf(errorMsg, errorMsgSize, "Invalid current reading: %.2fA", status.current);
            return -1;
        }
        
        if (status.controlLocation > 0x1F) {  // 5 bits max
            snprintf(errorMsg, errorMsgSize, "Invalid control location: %d", status.controlLocation);
            return -1;
        }
        
        Delay(0.2);
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Status register reading is consistent and valid");
    return 1;
}

int Test_VoltageControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage control...");
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different voltage values
    double testVoltages[] = {TEST_VOLTAGE_LOW, TEST_VOLTAGE_MID, TEST_VOLTAGE_HIGH};
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to %.2fV...", testVoltages[i]);
        
        result = PSB_SetVoltage(handle, testVoltages[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.2fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Read back the set value (note: without output enabled, actual voltage will be 0)
        PSB_Status status;
        result = PSB_GetStatus(handle, &status);
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
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test valid limits
    double minVoltage = 15.0;
    double maxVoltage = 45.0;
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage limits: min=%.2fV, max=%.2fV", minVoltage, maxVoltage);
    
    result = PSB_SetVoltageLimits(handle, minVoltage, maxVoltage);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Voltage limits set successfully");
    
    // Test invalid limits (min > max)
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid limits (min > max)...");
    result = PSB_SetVoltageLimits(handle, 40.0, 20.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have failed with min > max");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid limits");
    
    // IMPORTANT: Reset to wide limits after test
    LogDebugEx(LOG_DEVICE_PSB, "Resetting to wide limits...");
    SetWideLimits(handle);
    
    return 1;
}

int Test_CurrentControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current control...");
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different current values
    double testCurrents[] = {TEST_CURRENT_LOW, TEST_CURRENT_MID, TEST_CURRENT_HIGH};
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting current to %.2fA...", testCurrents[i]);
        
        result = PSB_SetCurrent(handle, testCurrents[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set current to %.2fA: %s", 
                    testCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        LogDebugEx(LOG_DEVICE_PSB, "Current set command accepted for %.2fA", testCurrents[i]);
    }
    
    return 1;
}

int Test_CurrentLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing current limits...");
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test valid limits
    double minCurrent = 10.0;
    double maxCurrent = 50.0;
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limits: min=%.2fA, max=%.2fA", minCurrent, maxCurrent);
    
    result = PSB_SetCurrentLimits(handle, minCurrent, maxCurrent);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Current limits set successfully");
    
    // IMPORTANT: Reset to wide limits after test
    LogDebugEx(LOG_DEVICE_PSB, "Resetting to wide limits...");
    SetWideLimits(handle);
    
    return 1;
}

int Test_PowerControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power control...");
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different power values
    double testPowers[] = {TEST_POWER_LOW, TEST_POWER_MID, TEST_POWER_HIGH};
    
    for (int i = 0; i < 3; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "Setting power to %.2fW...", testPowers[i]);
        
        result = PSB_SetPower(handle, testPowers[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set power to %.2fW: %s", 
                    testPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        LogDebugEx(LOG_DEVICE_PSB, "Power set command accepted for %.2fW", testPowers[i]);
    }
    
    return 1;
}

int Test_PowerLimit(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing power limit...");
    
    // Ensure remote mode is on (without spamming if already on)
    int result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    double maxPower = 1000.0;
    
    LogDebugEx(LOG_DEVICE_PSB, "Setting power limit to %.2fW", maxPower);
    
    result = PSB_SetPowerLimit(handle, maxPower);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Power limit set successfully");
    
    // IMPORTANT: Reset to wide limit after test
    LogDebugEx(LOG_DEVICE_PSB, "Resetting to wide power limit...");
    result = PSB_SetPowerLimit(handle, PSB_TEST_POWER_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to reset power limit");
    }
    
    return 1;
}

int Test_OutputControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing output control...");
    
    PSB_Status status;
    int result;
    
    // Ensure remote mode is on (without spamming if already on)
    result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Ensure output is OFF first
    LogDebugEx(LOG_DEVICE_PSB, "Turning output OFF...");
    result = PSB_SetOutputEnable(handle, 0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn off output: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify it's off
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    if (status.outputEnabled != 0) {
        snprintf(errorMsg, errorMsgSize, "Output should be OFF but status shows ON");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Output successfully turned OFF");
    
    return 1;
}

int Test_InvalidParameters(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing invalid parameter handling...");
    
    int result;
    
    // Ensure remote mode is on (without spamming if already on)
    result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test voltage beyond OVP limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing voltage beyond limit (%.2fV)...", TEST_VOLTAGE_INVALID);
    result = PSB_SetVoltage(handle, TEST_VOLTAGE_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage %.2fV", 
                TEST_VOLTAGE_INVALID);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid voltage");
    
    // Test negative voltage
    LogDebugEx(LOG_DEVICE_PSB, "Testing negative voltage...");
    result = PSB_SetVoltage(handle, -5.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative voltage");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected negative voltage");
    
    // Test current beyond OCP limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing current beyond limit (%.2fA)...", TEST_CURRENT_INVALID);
    result = PSB_SetCurrent(handle, TEST_CURRENT_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current %.2fA", 
                TEST_CURRENT_INVALID);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid current");
    
    // Test power beyond OPP limit
    LogDebugEx(LOG_DEVICE_PSB, "Testing power beyond limit (%.2fW)...", TEST_POWER_INVALID);
    result = PSB_SetPower(handle, TEST_POWER_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power %.2fW", 
                TEST_POWER_INVALID);
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected invalid power");
    
    return 1;
}

int Test_BoundaryConditions(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing boundary conditions...");
    
    int result;
    
    // Ensure remote mode is on (without spamming if already on)
    result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // At this point, we should have wide limits from the previous tests
    // But let's make sure
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring wide limits are set...");
    SetWideLimits(handle);
    Delay(0.5);
    
    // Test minimum allowed values
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum voltage...");
    result = PSB_SetVoltage(handle, PSB_TEST_VOLTAGE_MIN_WIDE);  // Device minimum
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum voltage accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing minimum current...");
    result = PSB_SetCurrent(handle, PSB_TEST_CURRENT_MIN_WIDE);  // Device minimum
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Minimum current accepted");
    
    // Test values below minimum (should fail)
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum voltage...");
    result = PSB_SetVoltage(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected voltage below minimum");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing below minimum current...");
    result = PSB_SetCurrent(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current below minimum");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Correctly rejected current below minimum");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum voltage (%.2fV)...", PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltage(handle, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum voltage accepted");
    
    LogDebugEx(LOG_DEVICE_PSB, "Testing maximum current (%.2fA)...", PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrent(handle, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Maximum current accepted");
    
    return 1;
}

int Test_SequenceOperations(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_PSB, "Testing sequence of operations...");
    
    int result;
    PSB_Status status;
    
    // This test explicitly tests remote mode transitions, but respects the final state
    
    // Step 1: Turn remote mode OFF to test the sequence
    LogDebugEx(LOG_DEVICE_PSB, "Step 1: Setting remote mode OFF for sequence test...");
    result = PSB_SetRemoteMode(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn off remote mode, continuing anyway: %s", 
                PSB_GetErrorString(result));
    } else {
        Delay(TEST_DELAY_SHORT);
    }
    
    // Step 2: Enable remote mode
    LogDebugEx(LOG_DEVICE_PSB, "Step 2: Enabling remote mode...");
    result = PSB_SetRemoteMode(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Verify remote mode is active
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS || !status.remoteMode) {
        snprintf(errorMsg, errorMsgSize, "Remote mode not active after enabling");
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Remote mode active");
    
    // Step 3: Now we can control output
    LogDebugEx(LOG_DEVICE_PSB, "Step 3: Turning output OFF...");
    result = PSB_SetOutputEnable(handle, 0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn off output: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    LogDebugEx(LOG_DEVICE_PSB, "Output turned OFF");
    
    // Step 4: Set operating parameters
    LogDebugEx(LOG_DEVICE_PSB, "Step 4: Setting operating parameters...");
    
    result = PSB_SetVoltage(handle, 25.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage in remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    result = PSB_SetCurrent(handle, 10.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current in remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_PSB, "Parameters set successfully");
    
    // Step 5: Clean up - ensure output is off and remote mode stays ON
    LogDebugEx(LOG_DEVICE_PSB, "Step 5: Cleaning up sequence test...");
    result = PSB_SetOutputEnable(handle, 0);
    if (result != PSB_SUCCESS) {
        LogWarningEx(LOG_DEVICE_PSB, "Failed to turn off output");
    }
    
    // Ensure remote mode is left ON (expected by status.c)
    EnsureRemoteMode(handle);
    
    LogDebugEx(LOG_DEVICE_PSB, "Sequence completed successfully");
    
    return 1;
}

int Test_OutputVoltageVerification(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    LogWarningEx(LOG_DEVICE_PSB, "Testing output voltage verification (CAUTION: Output will be enabled)");
    LogWarningEx(LOG_DEVICE_PSB, "WARNING: Ensure nothing is connected to the PSB output terminals!");
    
    int result;
    PSB_Status status;
    double actualVoltage, actualCurrent, actualPower;
    
    // First, check current device state
    LogDebugEx(LOG_DEVICE_PSB, "Checking initial device state...");
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read initial status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    PSB_PrintStatus(&status);
    
    // Check if there are active alarms
    if (status.alarmsActive) {
        LogDebugEx(LOG_DEVICE_PSB, "Active alarms detected - acknowledging...");
        AcknowledgeAlarms(handle);
        Delay(TEST_DELAY_SHORT);
    }
    
    // Check control location
    if (status.controlLocation == CONTROL_LOCAL) {
        snprintf(errorMsg, errorMsgSize, 
                "Device is in LOCAL mode - remote control blocked. "
                "Please enable 'Allow remote control' on the device front panel");
        return -1;
    }
    
    // Ensure remote mode is enabled
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring remote mode is enabled...");
    result = EnsureRemoteMode(handle);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to ensure remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Now try to ensure output is OFF
    LogDebugEx(LOG_DEVICE_PSB, "Ensuring output is OFF...");
    if (status.outputEnabled) {
        result = PSB_SetOutputEnable(handle, 0);
        if (result != PSB_SUCCESS) {
            // If this fails, it might be because the device won't accept output control
            // Let's continue anyway and see if we can at least read values
            LogWarningEx(LOG_DEVICE_PSB, "Failed to turn off output initially: %s", 
                   PSB_GetErrorString(result));
            LogWarningEx(LOG_DEVICE_PSB, "Device might require manual output control");
            
            // Ask user to manually turn off output using popup
            LogMessageEx(LOG_DEVICE_PSB, "*** MANUAL INTERVENTION REQUIRED ***");
            MessagePopup("Manual Intervention Required", 
                        "Please ensure the PSB output is OFF using the front panel,\n"
                        "then click OK to continue.");
        }
    }
    
    // Set safe operating parameters before enabling output
    LogDebugEx(LOG_DEVICE_PSB, "Setting safe operating parameters...");
    
    // Set current limit to a safe value (1A) to protect against accidental shorts
    LogDebugEx(LOG_DEVICE_PSB, "Setting current limit to 1.0A...");
    result = PSB_SetCurrent(handle, 1.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Set initial voltage to 0V
    LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to 0V...");
    result = PSB_SetVoltage(handle, 0.0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set initial voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test voltage values - use conservative values for safety
    double testVoltages[] = {5.0, 12.0, 24.0, 48.0};
    double tolerance = 0.5; // 0.5V tolerance for voltage accuracy
    
    LogWarningEx(LOG_DEVICE_PSB, "*** READY TO BEGIN OUTPUT TESTS ***");
    LogWarningEx(LOG_DEVICE_PSB, "The test will enable the PSB output with low current limit (1A)");
    LogWarningEx(LOG_DEVICE_PSB, "Ensure nothing is connected to the output terminals!");
    
    // Use ConfirmPopup instead of getchar()
    int userResponse = ConfirmPopup("Output Test Warning",
                                   "WARNING: This test will enable the PSB output!\n\n"
                                   "The output will be limited to 1A for safety.\n"
                                   "Ensure NOTHING is connected to the output terminals!\n\n"
                                   "Do you want to continue with the test?");
    
    if (userResponse == 0) {  // User clicked Cancel
        LogMessageEx(LOG_DEVICE_PSB, "Output test cancelled by user");
        snprintf(errorMsg, errorMsgSize, "Test cancelled by user");
        return -1;
    }
    
    for (int i = 0; i < 4; i++) {
        LogDebugEx(LOG_DEVICE_PSB, "--- Testing %.1fV output ---", testVoltages[i]);
        
        // Set the voltage setpoint
        LogDebugEx(LOG_DEVICE_PSB, "Setting voltage to %.2fV...", testVoltages[i]);
        result = PSB_SetVoltage(handle, testVoltages[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set voltage to %.2fV: %s", 
                    testVoltages[i], PSB_GetErrorString(result));
            goto cleanup;
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Enable output
        LogDebugEx(LOG_DEVICE_PSB, "Enabling output...");
        result = PSB_SetOutputEnable(handle, 1);
        if (result != PSB_SUCCESS) {
            // If remote output control fails, ask user to enable manually
            LogWarningEx(LOG_DEVICE_PSB, "Remote output control failed: %s", PSB_GetErrorString(result));
            LogMessageEx(LOG_DEVICE_PSB, "*** MANUAL INTERVENTION REQUIRED ***");
            MessagePopup("Manual Intervention Required",
                        "Please turn ON the output using the front panel,\n"
                        "then click OK when the output is ON.");
        }
        
        // Wait for voltage to stabilize
        LogDebugEx(LOG_DEVICE_PSB, "Waiting for voltage to stabilize...");
        Delay(TEST_DELAY_MEDIUM);
        
        // Read actual values
        result = PSB_GetActualValues(handle, &actualVoltage, &actualCurrent, &actualPower);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read actual values: %s", 
                    PSB_GetErrorString(result));
            goto cleanup;
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Actual values: V=%.3fV, I=%.3fA, P=%.3fW", 
               actualVoltage, actualCurrent, actualPower);
        
        // Verify voltage is within tolerance
        if (!CompareDouble(actualVoltage, testVoltages[i], tolerance)) {
            snprintf(errorMsg, errorMsgSize, 
                    "Voltage mismatch: Set=%.2fV, Actual=%.3fV (tolerance=%.2fV)", 
                    testVoltages[i], actualVoltage, tolerance);
            goto cleanup;
        }
        
        // Verify current is near zero (no load connected)
        if (actualCurrent > 0.1) {  // 100mA threshold
            LogWarningEx(LOG_DEVICE_PSB, "Current detected (%.3fA) - possible load connected?", 
                   actualCurrent);
        }
        
        // Get full status for additional verification
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status: %s", 
                    PSB_GetErrorString(result));
            goto cleanup;
        }
        
        // Verify output is enabled in status
        if (!status.outputEnabled) {
            LogWarningEx(LOG_DEVICE_PSB, "Output status shows OFF but we're reading voltage");
        }
        
        // Verify regulation mode (should be CV - Constant Voltage)
        if (status.regulationMode != 0) {
            LogDebugEx(LOG_DEVICE_PSB, "Note: Regulation mode is %d (expected 0 for CV mode)", 
                   status.regulationMode);
        }
        
        LogDebugEx(LOG_DEVICE_PSB, "Voltage %.2fV verified successfully", testVoltages[i]);
        
        // Turn output OFF before next test
        LogDebugEx(LOG_DEVICE_PSB, "Disabling output...");
        result = PSB_SetOutputEnable(handle, 0);
        if (result != PSB_SUCCESS) {
            LogWarningEx(LOG_DEVICE_PSB, "Remote output disable failed");
            MessagePopup("Manual Intervention Required",
                        "Please turn OFF the output using the front panel,\n"
                        "then click OK when the output is OFF.");
        }
        
        Delay(TEST_DELAY_SHORT);
        
        // Verify voltage drops to zero
        result = PSB_GetActualValues(handle, &actualVoltage, &actualCurrent, &actualPower);
        if (result == PSB_SUCCESS && actualVoltage > 1.0) {
            LogWarningEx(LOG_DEVICE_PSB, "Voltage still present after output disabled: %.3fV", 
                   actualVoltage);
        }
    }
    
    // Success - ensure clean shutdown
    LogDebugEx(LOG_DEVICE_PSB, "Test completed - ensuring safe shutdown...");
    PSB_SetVoltage(handle, 0.0);
    PSB_SetOutputEnable(handle, 0);
    Delay(TEST_DELAY_SHORT);
    
    // Restore wide current limit
    PSB_SetCurrent(handle, PSB_TEST_CURRENT_MAX_WIDE);
    
    LogDebugEx(LOG_DEVICE_PSB, "All output voltage tests passed successfully");
    return 1;
    
cleanup:
    // Emergency shutdown - ensure output is OFF
    LogErrorEx(LOG_DEVICE_PSB, "CLEANUP: Ensuring safe state...");
    PSB_SetVoltage(handle, 0.0);
    PSB_SetOutputEnable(handle, 0);
    
    MessagePopup("Safety Check",
                "If the output is still ON, please turn it OFF manually,\n"
                "then click OK when safe.");
    
    // Restore wide current limit
    PSB_SetCurrent(handle, PSB_TEST_CURRENT_MAX_WIDE);
    
    return -1;
}