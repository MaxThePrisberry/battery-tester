/******************************************************************************
 * biologic_test.h
 * 
 * BioLogic Test Suite
 * Header file for comprehensive testing of BioLogic functions
 * 
 * Currently implements basic connection testing - full suite to be added later
 ******************************************************************************/

#ifndef BIOLOGIC_TEST_H
#define BIOLOGIC_TEST_H

#include "common.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include <userint.h>

/******************************************************************************
 * Test Configuration
 ******************************************************************************/

// Test timing
#define BIO_TEST_DELAY_SHORT        0.5     // seconds
#define BIO_TEST_DELAY_MEDIUM       1.0     // seconds
#define BIO_TEST_DELAY_LONG         2.0     // seconds

// Test timeout
#define BIO_TEST_TIMEOUT_MS         5000    // 5 seconds

/******************************************************************************
 * Test Result Structure
 ******************************************************************************/

typedef struct {
    int totalTests;
    int passedTests;
    int failedTests;
    char lastError[256];
    double executionTime;
} BioTestSummary;

typedef struct {
    const char *testName;
    int (*testFunction)(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);
    int result;  // 0 = not run, 1 = pass, -1 = fail
    char errorMessage[256];
    double executionTime;
} BioTestCase;

/******************************************************************************
 * Test Suite Control
 ******************************************************************************/

typedef struct {
    BioQueueManager *bioQueueMgr;
    int panelHandle;
    int statusStringControl;
    int ledControl;
    volatile int cancelRequested;
    TestState state;
    BioTestSummary summary;
    void (*progressCallback)(const char *message);
} BioTestSuiteContext;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Main callback and worker thread
int CVICALLBACK TestBiologicCallback(int panel, int control, int event,
                                    void *callbackData, int eventData1, int eventData2);
int CVICALLBACK TestBiologicWorkerThread(void *functionData);

// Test suite functions
int BIO_TestSuite_Initialize(BioTestSuiteContext *context, BioQueueManager *bioQueueMgr, 
                            int panel, int statusControl, int ledControl);
int BIO_TestSuite_Run(BioTestSuiteContext *context);
void BIO_TestSuite_Cancel(BioTestSuiteContext *context);
void BIO_TestSuite_Cleanup(BioTestSuiteContext *context);

// Individual test functions
int Test_BIO_Connection(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);

// Utility functions
void BIO_UpdateTestProgress(BioTestSuiteContext *context, const char *message);

#endif // BIOLOGIC_TEST_H