/******************************************************************************
 * biologic_test.h
 * 
 * BioLogic Test Suite
 * Updated to use high-level technique functions
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

// OCV test parameters
#define BIO_TEST_OCV_DURATION       10.0    // 10 seconds

// PEIS test parameters
#define BIO_TEST_PEIS_START_FREQ    100000.0 // 100kHz
#define BIO_TEST_PEIS_END_FREQ      10.0    // 10Hz

// SPEIS test parameters
#define BIO_TEST_SPEIS_INIT_V       -0.5    // Initial voltage -0.5V
#define BIO_TEST_SPEIS_FINAL_V      0.5     // Final voltage +0.5V
#define BIO_TEST_SPEIS_STEPS        10      // 10 

// GEIS test parameters
#define BIO_TEST_GEIS_INIT_I        0.0     // Initial current 0A
#define BIO_TEST_GEIS_AMPLITUDE_I   0.010   // 10mA amplitude
#define BIO_TEST_GEIS_START_FREQ    1000.0  // 1kHz
#define BIO_TEST_GEIS_END_FREQ      100.0   // 100Hz

// SGEIS test parameters  
#define BIO_TEST_SGEIS_INIT_I       0.0     // Initial current 0A
#define BIO_TEST_SGEIS_FINAL_I      0.100   // Final current 100mA
#define BIO_TEST_SGEIS_AMPLITUDE_I  0.010   // 10mA amplitude
#define BIO_TEST_SGEIS_STEPS        10      // 10 steps

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
int Test_BIO_OCV(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);
int Test_BIO_PEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);
int Test_BIO_SPEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);
int Test_BIO_GEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);
int Test_BIO_SGEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize);

// Utility functions
void BIO_UpdateTestProgress(BioTestSuiteContext *context, const char *message);

// Progress callback for techniques
void Test_BIO_TechniqueProgress(double elapsedTime, int memFilled, void *userData);

#endif // BIOLOGIC_TEST_H