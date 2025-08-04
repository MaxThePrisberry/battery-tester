/******************************************************************************
 * PSB 10000 Test Suite
 * Header file for comprehensive testing of PSB10000 functions
 * 
 * Note: Connection Status test has been removed as it's redundant with the
 * status.c module which handles device connection management.
 ******************************************************************************/

#ifndef PSB10000_TEST_H
#define PSB10000_TEST_H

#include "psb10000_dll.h"
#include <userint.h>

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test voltage/current/power values (respecting PSB limits)
// Voltage values (min limit is 0V)
#define TEST_VOLTAGE_LOW        1.0     // V (above 0V minimum)
#define TEST_VOLTAGE_MID        30.0    // V
#define TEST_VOLTAGE_HIGH       45.0    // V (below 60V default max)
#define TEST_VOLTAGE_MAX        60.0    // V
#define TEST_VOLTAGE_INVALID    67.0    // V (beyond OVP limit of 66V)

// Current values (min limit is 0A)  
#define TEST_CURRENT_LOW        6.0     // A (above 0A minimum)
#define TEST_CURRENT_MID        30.0    // A
#define TEST_CURRENT_HIGH       50.0    // A
#define TEST_CURRENT_MAX        60.0    // A
#define TEST_CURRENT_INVALID    67.0    // A (beyond OCP limit of 66A)

// Power values
#define TEST_POWER_LOW          100.0   // W
#define TEST_POWER_MID          600.0   // W (below 1224W default max)
#define TEST_POWER_HIGH         1000.0  // W (below 1224W default max)
#define TEST_POWER_MAX          1200.0  // W (below 1224W default max)
#define TEST_POWER_INVALID      1400.0  // W (beyond OPP limit of 1320W)

// Test timing
#define TEST_DELAY_SHORT        0.5     // seconds
#define TEST_DELAY_MEDIUM       1.0     // seconds
#define TEST_DELAY_LONG         2.0     // seconds

// Sink mode test values - same ranges as source mode
#define TEST_SINK_CURRENT_LOW        5.0     // A
#define TEST_SINK_CURRENT_MID        15.0    // A  
#define TEST_SINK_CURRENT_HIGH       30.0    // A
#define TEST_SINK_CURRENT_MAX        60.0    // A

#define TEST_SINK_POWER_LOW          100.0   // W
#define TEST_SINK_POWER_MID          400.0   // W
#define TEST_SINK_POWER_HIGH         800.0   // W
#define TEST_SINK_POWER_MAX          1200.0  // W

// Sink mode limit test values
#define TEST_SINK_CURRENT_LIMIT_MIN  5.0     // A
#define TEST_SINK_CURRENT_LIMIT_MAX  40.0    // A
#define TEST_SINK_CURRENT_LIMIT_TEST 20.0    // A (within limits)

#define TEST_SINK_POWER_LIMIT_1      1000.0  // W
#define TEST_SINK_POWER_LIMIT_2      600.0   // W
#define TEST_SINK_POWER_LIMIT_TEST   800.0   // W (below limit)

// Invalid sink mode test values
#define TEST_SINK_CURRENT_NEGATIVE   -10.0   // A
#define TEST_SINK_POWER_NEGATIVE     -100.0  // W
#define TEST_SINK_CURRENT_MIN_NEG    -5.0    // A
#define TEST_SINK_POWER_ABOVE_LIMIT  100.0   // W (added to limit)

// Inverted limits for error testing
#define TEST_SINK_CURRENT_LIMIT_MIN_INV  30.0   // A (intentionally > max)
#define TEST_SINK_CURRENT_LIMIT_MAX_INV  10.0   // A (intentionally < min)

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    int totalTests;
    int passedTests;
    int failedTests;
    char lastError[256];
    double executionTime;
} TestSummary;

typedef struct {
    const char *testName;
    int (*testFunction)(char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
} TestCase;

/******************************************************************************
 * Test Suite Control
 ******************************************************************************/

typedef struct {
    int panelHandle;
    int statusStringControl;
    int cancelRequested;
    TestState state;
    TestSummary summary;
    void (*progressCallback)(const char *message);
} TestSuiteContext;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
int CVICALLBACK TestPSBWorkerThread(void *functionData);

// Main test suite functions
int PSB_TestSuite_Initialize(TestSuiteContext *context, int panel, int statusControl);
int PSB_TestSuite_Run(TestSuiteContext *context);
void PSB_TestSuite_Cancel(TestSuiteContext *context);
void PSB_TestSuite_Cleanup(TestSuiteContext *context);

// Individual test functions
int Test_RemoteMode(char *errorMsg, int errorMsgSize);
int Test_StatusRegisterReading(char *errorMsg, int errorMsgSize);
int Test_VoltageControl(char *errorMsg, int errorMsgSize);
int Test_VoltageLimits(char *errorMsg, int errorMsgSize);
int Test_CurrentControl(char *errorMsg, int errorMsgSize);
int Test_CurrentLimits(char *errorMsg, int errorMsgSize);
int Test_PowerControl(char *errorMsg, int errorMsgSize);
int Test_PowerLimit(char *errorMsg, int errorMsgSize);
int Test_OutputControl(char *errorMsg, int errorMsgSize);
int Test_InvalidParameters(char *errorMsg, int errorMsgSize);
int Test_SequenceOperations(char *errorMsg, int errorMsgSize);
int Test_BoundaryConditions(char *errorMsg, int errorMsgSize);
int Test_OutputVoltageVerification(char *errorMsg, int errorMsgSize);
int Test_SinkCurrentControl(char *errorMsg, int errorMsgSize);
int Test_SinkPowerControl(char *errorMsg, int errorMsgSize);
int Test_SinkCurrentLimits(char *errorMsg, int errorMsgSize);
int Test_SinkPowerLimit(char *errorMsg, int errorMsgSize);

// Utility functions
void UpdateTestProgress(TestSuiteContext *context, const char *message);

#endif // PSB10000_TEST_H