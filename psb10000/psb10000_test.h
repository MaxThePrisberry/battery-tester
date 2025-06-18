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

// Wide limits for testing - set before and after test suite
#define PSB_TEST_VOLTAGE_MIN_WIDE   0.0     // V
#define PSB_TEST_VOLTAGE_MAX_WIDE   60.0    // V
#define PSB_TEST_CURRENT_MIN_WIDE   0.0     // A
#define PSB_TEST_CURRENT_MAX_WIDE   61.2    // A
#define PSB_TEST_POWER_MAX_WIDE     1224.0  // W

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
    int (*testFunction)(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
} TestCase;

/******************************************************************************
 * Test Suite Control
 ******************************************************************************/

typedef struct {
    PSB_Handle *psbHandle;
    int panelHandle;
    int statusStringControl;
    int cancelRequested;
    int isRunning;
    TestSummary summary;
    void (*progressCallback)(const char *message);
} TestSuiteContext;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Main test suite functions
int PSB_TestSuite_Initialize(TestSuiteContext *context, PSB_Handle *handle, 
                            int panel, int statusControl);
int PSB_TestSuite_Run(TestSuiteContext *context);
void PSB_TestSuite_Cancel(TestSuiteContext *context);
void PSB_TestSuite_Cleanup(TestSuiteContext *context);

// Individual test functions
int Test_RemoteMode(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_StatusRegisterReading(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_VoltageControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_VoltageLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_CurrentControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_CurrentLimits(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_PowerControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_PowerLimit(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_OutputControl(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_InvalidParameters(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_SequenceOperations(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_BoundaryConditions(PSB_Handle *handle, char *errorMsg, int errorMsgSize);
int Test_OutputVoltageVerification(PSB_Handle *handle, char *errorMsg, int errorMsgSize);

// Utility functions
void UpdateTestProgress(TestSuiteContext *context, const char *message);

#endif // PSB10000_TEST_H