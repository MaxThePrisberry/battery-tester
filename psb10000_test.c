/******************************************************************************
 * PSB 10000 Test Suite
 * Implementation file for comprehensive testing of PSB10000 functions
 ******************************************************************************/

#include "psb10000_test.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/******************************************************************************
 * Test Cases Array
 ******************************************************************************/

static TestCase testCases[] = {
    {"Connection Status", Test_ConnectionStatus, 0, "", 0.0},
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
    {"Sequence Operations", Test_SequenceOperations, 0, "", 0.0}
};

static int numTestCases = sizeof(testCases) / sizeof(testCases[0]);

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static int SetWideLimits(PSB_Handle *handle);

static double GetTime(void) {
    return Timer();
}

void PrintTestHeader(const char *testName) {
    printf("\n--- Testing: %s ---\n", testName);
}

void PrintTestResult(const char *testName, int passed, const char *errorMsg) {
    if (passed) {
        printf("[PASS] %s\n", testName);
    } else {
        printf("[FAIL] %s\n", testName);
        if (errorMsg && strlen(errorMsg) > 0) {
            printf("       Error: %s\n", errorMsg);
        }
    }
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
    
    printf("Acknowledging any pending alarms...\n");
    
    // Clear input buffer first
    FlushInQ(handle->comPort);
    
    if (ComWrt(handle->comPort, (char*)txBuffer, 8) != 8) {
        printf("Failed to send alarm acknowledge command\n");
        return PSB_ERROR_COMM;
    }
    
    Delay(0.1); // Give device time to process
    
    // Read response (we don't really care about it, just need to clear the buffer)
    ComRd(handle->comPort, (char*)rxBuffer, 8);
    
    return PSB_SUCCESS;
}

// Helper function to set wide limits for testing
static int SetWideLimits(PSB_Handle *handle) {
    printf("\nSetting wide limits for testing...\n");
    
    int result;
    int errors = 0;
    
    // For voltage limits, we need to be careful about order
    printf("Setting voltage limits: %.1fV - %.1fV...\n", 
           PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltageLimits(handle, PSB_TEST_VOLTAGE_MIN_WIDE, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        // If that fails, we might need to set max first (if current max is too low)
        // or min first (if current min is too high)
        printf("WARNING: Failed to set voltage limits\n");
        errors++;
    } else {
        printf("? Voltage limits set successfully\n");
    }
    
    // Set current limits
    printf("Setting current limits: %.1fA - %.1fA...\n", 
           PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrentLimits(handle, PSB_TEST_CURRENT_MIN_WIDE, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        printf("WARNING: Failed to set current limits: %s\n", PSB_GetErrorString(result));
        errors++;
    } else {
        printf("? Current limits set successfully\n");
    }
    
    // Set power limit
    printf("Setting power limit: %.1fW...\n", PSB_TEST_POWER_MAX_WIDE);
    result = PSB_SetPowerLimit(handle, PSB_TEST_POWER_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        printf("WARNING: Failed to set power limit: %s\n", PSB_GetErrorString(result));
        errors++;
    } else {
        printf("? Power limit set successfully\n");
    }
    
    if (errors == 0) {
        printf("? All wide limits set successfully\n");
        return PSB_SUCCESS;
    } else {
        printf("? Failed to set %d limit(s)\n", errors);
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
    
    printf("\n========================================\n");
    printf("PSB 10000 TEST SUITE STARTING\n");
    printf("========================================\n");
    
    UpdateTestProgress(context, "PSB Test Suite Starting...");
    
    // Enable debug output for detailed analysis
    PSB_EnableDebugOutput(1);
    
    // IMPORTANT: Acknowledge any pending alarms before starting tests
    printf("\nPreparing device for testing...\n");
    AcknowledgeAlarms(context->psbHandle);
    Delay(0.5); // Give device time to settle
    
    // CRITICAL: Set wide limits before starting any tests
    UpdateTestProgress(context, "Setting wide limits for testing...");
    SetWideLimits(context->psbHandle);
    Delay(0.5);
    
    // Run each test
    for (int i = 0; i < numTestCases; i++) {
        if (context->cancelRequested) {
            printf("\n*** TEST SUITE CANCELLED BY USER ***\n");
            UpdateTestProgress(context, "Test suite cancelled");
            break;
        }
        
        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s", 
                i + 1, numTestCases, testCases[i].testName);
        UpdateTestProgress(context, progressMsg);
        
        PrintTestHeader(testCases[i].testName);
        
        double testStartTime = GetTime();
        
        // Run the test
        testCases[i].result = testCases[i].testFunction(
            context->psbHandle, 
            testCases[i].errorMessage, 
            sizeof(testCases[i].errorMessage)
        );
        
        testCases[i].executionTime = GetTime() - testStartTime;
        
        // Update counts
        if (testCases[i].result > 0) {
            context->summary.passedTests++;
            PrintTestResult(testCases[i].testName, 1, NULL);
        } else {
            context->summary.failedTests++;
            PrintTestResult(testCases[i].testName, 0, testCases[i].errorMessage);
            strncpy(context->summary.lastError, testCases[i].errorMessage, 
                   sizeof(context->summary.lastError) - 1);
        }
        
        printf("Test execution time: %.2f seconds\n", testCases[i].executionTime);
        
        // Small delay between tests
        Delay(0.5);
    }
    
    // Restore wide limits AFTER all tests complete
    printf("\n--- Cleanup ---\n");
    UpdateTestProgress(context, "Restoring wide limits...");
    SetWideLimits(context->psbHandle);
    
    // Ensure safe state
    PSB_SetOutputEnable(context->psbHandle, 0);
    PSB_SetRemoteMode(context->psbHandle, 0);
    
    context->summary.executionTime = GetTime() - suiteStartTime;
    
    // Print summary
    printf("\n========================================\n");
    printf("TEST SUITE SUMMARY\n");
    printf("========================================\n");
    printf("Total Tests: %d\n", context->summary.totalTests);
    printf("Passed: %d\n", context->summary.passedTests);
    printf("Failed: %d\n", context->summary.failedTests);
    printf("Total Time: %.2f seconds\n", context->summary.executionTime);
    
    if (context->summary.failedTests > 0) {
        printf("\nLast Error: %s\n", context->summary.lastError);
    }
    
    printf("\nDetailed Results:\n");
    for (int i = 0; i < numTestCases; i++) {
        printf("  %-30s: %s (%.2fs)", 
               testCases[i].testName,
               testCases[i].result > 0 ? "PASS" : "FAIL",
               testCases[i].executionTime);
        if (testCases[i].result <= 0 && strlen(testCases[i].errorMessage) > 0) {
            printf(" - %s", testCases[i].errorMessage);
        }
        printf("\n");
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
        // Ensure PSB is in safe state
        if (context->psbHandle && context->psbHandle->isConnected) {
            // Restore wide limits
            SetWideLimits(context->psbHandle);
            
            // Turn off output and remote mode
            PSB_SetOutputEnable(context->psbHandle, 0);
            PSB_SetRemoteMode(context->psbHandle, 0);
        }
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_ConnectionStatus(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing connection to PSB...\n");
    
    if (!handle->isConnected) {
        snprintf(errorMsg, errorMsgSize, "PSB handle reports not connected");
        return -1;
    }
    
    // Try to read device status to verify connection
    PSB_Status status;
    int result = PSB_GetStatus(handle, &status);
    
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("Connection verified - PSB is responding\n");
    PSB_PrintStatus(&status);
    
    return 1;
}

int Test_RemoteMode(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing remote mode control...\n");
    
    PSB_Status status;
    int result;
    
    // First, check initial state
    printf("Reading initial state...\n");
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read initial status: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("Initial state - Remote mode: %s, Control location: 0x%02X\n", 
           status.remoteMode ? "ON" : "OFF", 
           status.controlLocation);
    
    // Check if device is in local mode (which would prevent remote control)
    if (status.controlLocation == CONTROL_LOCAL) {
        printf("WARNING: Device is in LOCAL mode - remote control may be blocked\n");
        printf("Please ensure 'Allow remote control' is enabled on the device\n");
    }
    
    // Acknowledge any pending alarms
    AcknowledgeAlarms(handle);
    Delay(0.2);
    
    // First, turn OFF remote mode
    printf("Setting remote mode OFF...\n");
    result = PSB_SetRemoteMode(handle, 0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn off remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(1.0); // Increased delay to ensure device processes the command
    
    // Verify it's off
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status after turning off remote: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("After OFF command - Remote mode: %s, Raw state: 0x%08lX\n", 
           status.remoteMode ? "ON" : "OFF", status.rawState);
    
    if (status.remoteMode != 0) {
        snprintf(errorMsg, errorMsgSize, "Remote mode should be OFF but status shows ON");
        return -1;
    }
    printf("? Remote mode successfully turned OFF\n");
    
    // Now turn ON remote mode
    printf("Setting remote mode ON...\n");
    result = PSB_SetRemoteMode(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn on remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(1.0); // Increased delay
    
    // Verify it's on
    result = PSB_GetStatus(handle, &status);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to read status after turning on remote: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("After ON command - Remote mode: %s, Raw state: 0x%08lX\n", 
           status.remoteMode ? "ON" : "OFF", status.rawState);
    
    if (status.remoteMode != 1) {
        // Print more diagnostic information
        printf("ERROR: Remote mode not set correctly\n");
        printf("Control location: 0x%02X\n", status.controlLocation);
        printf("Alarms active: %s\n", status.alarmsActive ? "YES" : "NO");
        
        snprintf(errorMsg, errorMsgSize, 
                "Remote mode should be ON but status shows OFF. Control location: 0x%02X", 
                status.controlLocation);
        return -1;
    }
    printf("? Remote mode successfully turned ON\n");
    
    return 1;
}

int Test_StatusRegisterReading(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing status register reading (debugging focus)...\n");
    
    PSB_Status status;
    int result;
    
    // Read status multiple times to check consistency
    printf("Reading status 5 times to check consistency...\n");
    
    for (int i = 0; i < 5; i++) {
        result = PSB_GetStatus(handle, &status);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to read status on attempt %d: %s", 
                    i + 1, PSB_GetErrorString(result));
            return -1;
        }
        
        printf("\nRead #%d:\n", i + 1);
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
    
    printf("\n? Status register reading is consistent and valid\n");
    return 1;
}

int Test_VoltageControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing voltage control...\n");
    
    // Ensure remote mode is on
    int result = PSB_SetRemoteMode(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Test setting different voltage values
    double testVoltages[] = {TEST_VOLTAGE_LOW, TEST_VOLTAGE_MID, TEST_VOLTAGE_HIGH};
    
    for (int i = 0; i < 3; i++) {
        printf("Setting voltage to %.2fV...\n", testVoltages[i]);
        
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
        
        printf("? Voltage set command accepted for %.2fV\n", testVoltages[i]);
    }
    
    return 1;
}

int Test_VoltageLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing voltage limits...\n");
    
    // Test valid limits
    double minVoltage = 15.0;
    double maxVoltage = 45.0;
    
    printf("Setting voltage limits: min=%.2fV, max=%.2fV\n", minVoltage, maxVoltage);
    
    int result = PSB_SetVoltageLimits(handle, minVoltage, maxVoltage);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set voltage limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("? Voltage limits set successfully\n");
    
    // Test invalid limits (min > max)
    printf("Testing invalid limits (min > max)...\n");
    result = PSB_SetVoltageLimits(handle, 40.0, 20.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have failed with min > max");
        return -1;
    }
    printf("? Correctly rejected invalid limits\n");
    
    // IMPORTANT: Reset to wide limits after test
    printf("Resetting to wide limits...\n");
    SetWideLimits(handle);
    
    return 1;
}

int Test_CurrentControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing current control...\n");
    
    // Ensure remote mode is on
    int result = PSB_SetRemoteMode(handle, 1);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to enable remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    // Test setting different current values
    double testCurrents[] = {TEST_CURRENT_LOW, TEST_CURRENT_MID, TEST_CURRENT_HIGH};
    
    for (int i = 0; i < 3; i++) {
        printf("Setting current to %.2fA...\n", testCurrents[i]);
        
        result = PSB_SetCurrent(handle, testCurrents[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set current to %.2fA: %s", 
                    testCurrents[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        printf("? Current set command accepted for %.2fA\n", testCurrents[i]);
    }
    
    return 1;
}

int Test_CurrentLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing current limits...\n");
    
    // Test valid limits
    double minCurrent = 10.0;
    double maxCurrent = 50.0;
    
    printf("Setting current limits: min=%.2fA, max=%.2fA\n", minCurrent, maxCurrent);
    
    int result = PSB_SetCurrentLimits(handle, minCurrent, maxCurrent);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set current limits: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("? Current limits set successfully\n");
    
    // IMPORTANT: Reset to wide limits after test
    printf("Resetting to wide limits...\n");
    SetWideLimits(handle);
    
    return 1;
}

int Test_PowerControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing power control...\n");
    
    // Test setting different power values
    double testPowers[] = {TEST_POWER_LOW, TEST_POWER_MID, TEST_POWER_HIGH};
    
    for (int i = 0; i < 3; i++) {
        printf("Setting power to %.2fW...\n", testPowers[i]);
        
        int result = PSB_SetPower(handle, testPowers[i]);
        if (result != PSB_SUCCESS) {
            snprintf(errorMsg, errorMsgSize, "Failed to set power to %.2fW: %s", 
                    testPowers[i], PSB_GetErrorString(result));
            return -1;
        }
        
        Delay(TEST_DELAY_SHORT);
        printf("? Power set command accepted for %.2fW\n", testPowers[i]);
    }
    
    return 1;
}

int Test_PowerLimit(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing power limit...\n");
    
    double maxPower = 1000.0;
    
    printf("Setting power limit to %.2fW\n", maxPower);
    
    int result = PSB_SetPowerLimit(handle, maxPower);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set power limit: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    printf("? Power limit set successfully\n");
    
    // IMPORTANT: Reset to wide limit after test
    printf("Resetting to wide power limit...\n");
    result = PSB_SetPowerLimit(handle, PSB_TEST_POWER_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        printf("Warning: Failed to reset power limit\n");
    }
    
    return 1;
}

int Test_OutputControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing output control...\n");
    
    PSB_Status status;
    int result;
    
    // Ensure output is OFF first
    printf("Turning output OFF...\n");
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
    printf("? Output successfully turned OFF\n");
    
    // Note: We won't turn output ON during testing for safety
    printf("(Skipping output ON test for safety)\n");
    
    return 1;
}

int Test_InvalidParameters(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing invalid parameter handling...\n");
    
    int result;
    
    // Test voltage beyond OVP limit
    printf("Testing voltage beyond limit (%.2fV)...\n", TEST_VOLTAGE_INVALID);
    result = PSB_SetVoltage(handle, TEST_VOLTAGE_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage %.2fV", 
                TEST_VOLTAGE_INVALID);
        return -1;
    }
    printf("? Correctly rejected invalid voltage\n");
    
    // Test negative voltage
    printf("Testing negative voltage...\n");
    result = PSB_SetVoltage(handle, -5.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected negative voltage");
        return -1;
    }
    printf("? Correctly rejected negative voltage\n");
    
    // Test current beyond OCP limit
    printf("Testing current beyond limit (%.2fA)...\n", TEST_CURRENT_INVALID);
    result = PSB_SetCurrent(handle, TEST_CURRENT_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current %.2fA", 
                TEST_CURRENT_INVALID);
        return -1;
    }
    printf("? Correctly rejected invalid current\n");
    
    // Test power beyond OPP limit
    printf("Testing power beyond limit (%.2fW)...\n", TEST_POWER_INVALID);
    result = PSB_SetPower(handle, TEST_POWER_INVALID);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected power %.2fW", 
                TEST_POWER_INVALID);
        return -1;
    }
    printf("? Correctly rejected invalid power\n");
    
    return 1;
}

int Test_BoundaryConditions(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing boundary conditions...\n");
    
    int result;
    
    // At this point, we should have wide limits from the previous tests
    // But let's make sure
    printf("Ensuring wide limits are set...\n");
    SetWideLimits(handle);
    Delay(0.5);
    
    // Test minimum allowed values
    printf("Testing minimum voltage...\n");
    result = PSB_SetVoltage(handle, PSB_TEST_VOLTAGE_MIN_WIDE);  // Device minimum
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    printf("? Minimum voltage accepted\n");
    
    printf("Testing minimum current...\n");
    result = PSB_SetCurrent(handle, PSB_TEST_CURRENT_MIN_WIDE);  // Device minimum
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set minimum current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    printf("? Minimum current accepted\n");
    
    // Test values below minimum (should fail)
    printf("Testing below minimum voltage...\n");
    result = PSB_SetVoltage(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected voltage below minimum");
        return -1;
    }
    printf("? Correctly rejected voltage below minimum\n");
    
    printf("Testing below minimum current...\n");
    result = PSB_SetCurrent(handle, -2.0);
    if (result == PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Should have rejected current below minimum");
        return -1;
    }
    printf("? Correctly rejected current below minimum\n");
    
    printf("Testing maximum voltage (%.2fV)...\n", PSB_TEST_VOLTAGE_MAX_WIDE);
    result = PSB_SetVoltage(handle, PSB_TEST_VOLTAGE_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max voltage: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    printf("? Maximum voltage accepted\n");
    
    printf("Testing maximum current (%.2fA)...\n", PSB_TEST_CURRENT_MAX_WIDE);
    result = PSB_SetCurrent(handle, PSB_TEST_CURRENT_MAX_WIDE);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to set max current: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    printf("? Maximum current accepted\n");
    
    return 1;
}

int Test_SequenceOperations(PSB_Handle *handle, char *errorMsg, int errorMsgSize) {
    printf("Testing sequence of operations...\n");
    
    int result;
    PSB_Status status;
    
    // Step 1: Start with remote mode OFF
    printf("Step 1: Setting remote mode OFF...\n");
    result = PSB_SetRemoteMode(handle, 0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn off remote mode: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    
    Delay(TEST_DELAY_SHORT);
    
    // Step 2: Enable remote mode first
    printf("Step 2: Enabling remote mode...\n");
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
    printf("? Remote mode active\n");
    
    // Step 3: Now we can control output
    printf("Step 3: Turning output OFF...\n");
    result = PSB_SetOutputEnable(handle, 0);
    if (result != PSB_SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Failed to turn off output: %s", 
                PSB_GetErrorString(result));
        return -1;
    }
    printf("? Output turned OFF\n");
    
    // Step 4: Set operating parameters
    printf("Step 4: Setting operating parameters...\n");
    
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
    
    printf("? Parameters set successfully\n");
    
    // Step 5: Return to safe state
    printf("Step 5: Returning to safe state...\n");
    result = PSB_SetOutputEnable(handle, 0);
    if (result != PSB_SUCCESS) {
        printf("Warning: Failed to turn off output\n");
    }
    
    result = PSB_SetRemoteMode(handle, 0);
    if (result != PSB_SUCCESS) {
        printf("Warning: Failed to turn off remote mode\n");
    }
    
    printf("? Sequence completed successfully\n");
    
    return 1;
}