/******************************************************************************
 * biologic_test.c
 * 
 * BioLogic Test Suite Implementation
 * Comprehensive testing of BioLogic functions with queue system integration
 * 
 * Currently implements basic connection testing - full suite to be added later
 ******************************************************************************/

#include "BatteryTester.h"
#include "biologic_test.h"
#include "biologic_queue.h"
#include "common.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/******************************************************************************
 * Additional Test Constants (not in header)
 ******************************************************************************/
#define TEST_DELAY_VERY_SHORT       0.1     // seconds
#define TEST_DELAY_BETWEEN_TESTS    0.2     // seconds

/******************************************************************************
 * Global variables
 ******************************************************************************/
static BioTestSuiteContext *g_biologicTestSuiteContext = NULL;

/******************************************************************************
 * Test Cases Array
 ******************************************************************************/

static BioTestCase testCases[] = {
    {"Connection Test", Test_BIO_Connection, 0, "", 0.0}
};

static int numTestCases = sizeof(testCases) / sizeof(testCases[0]);

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static double GetTime(void) {
    return Timer();
}

void BIO_UpdateTestProgress(BioTestSuiteContext *context, const char *message) {
    if (context && context->progressCallback) {
        context->progressCallback(message);
    }
    
    if (context && context->statusStringControl > 0 && context->panelHandle > 0) {
        SetCtrlVal(context->panelHandle, context->statusStringControl, message);
        ProcessDrawEvents();
    }
}

static void GenerateBioTestSummary(BioTestSummary *summary, BioTestCase *tests, int numTests) {
    if (!summary || !tests) return;
    
    // Calculate total execution time from individual test times
    double totalTime = 0.0;
    for (int i = 0; i < numTests; i++) {
        totalTime += tests[i].executionTime;
    }
    
    summary->executionTime = totalTime;
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "BioLogic Test Suite Summary:");
    LogMessageEx(LOG_DEVICE_BIO, "Total Tests: %d", summary->totalTests);
    LogMessageEx(LOG_DEVICE_BIO, "Passed: %d", summary->passedTests);
    LogMessageEx(LOG_DEVICE_BIO, "Failed: %d", summary->failedTests);
    LogMessageEx(LOG_DEVICE_BIO, "Total Time: %.2f seconds", totalTime);
    LogMessageEx(LOG_DEVICE_BIO, "Average Time: %.2f seconds", 
                 (numTests > 0) ? (totalTime / numTests) : 0.0);
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    
    if (summary->failedTests > 0) {
        LogMessageEx(LOG_DEVICE_BIO, "Failed Tests:");
        for (int i = 0; i < numTests; i++) {
            if (tests[i].result <= 0) {
                LogMessageEx(LOG_DEVICE_BIO, "  - %s: %s", 
                           tests[i].testName, tests[i].errorMessage);
            }
        }
    }
}

/******************************************************************************
 * Test Button Callback and Worker Thread
 ******************************************************************************/

int CVICALLBACK TestBiologicCallback(int panel, int control, int event,
                                    void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if this is a cancel request (test is running)
            if (g_biologicTestSuiteContext != NULL) {
                LogMessageEx(LOG_DEVICE_BIO, "User requested to cancel BioLogic test suite");
                BIO_TestSuite_Cancel(g_biologicTestSuiteContext);
                
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
                LogWarningEx(LOG_DEVICE_BIO, "Cannot start test - system is busy");
                MessagePopup("System Busy", 
                           "Another operation is in progress.\n"
                           "Please wait for it to complete before starting a test.");
                return 0;
            }
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);
            
            // Check if BioLogic is connected through queue manager
            BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
            if (!bioQueueMgr) {
                LogErrorEx(LOG_DEVICE_BIO, "BioLogic queue manager not initialized");
                MessagePopup("BioLogic Not Available", 
                           "The BioLogic queue manager is not initialized.\n"
                           "Please check the system configuration.");
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }
            
            BioQueueStats stats;
            BIO_QueueGetStats(bioQueueMgr, &stats);
            
            if (!stats.isConnected) {
                LogErrorEx(LOG_DEVICE_BIO, "BioLogic not connected - cannot run test suite");
                MessagePopup("BioLogic Not Connected", 
                           "The BioLogic device is not connected.\n"
                           "Please ensure it is connected before running tests.");
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
                return 0;
            }
            
            // Dim EXPERIMENTS tab control
            SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 1);
            
            // Change Test BioLogic button text to "Cancel"
            SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");
            
            // Create test context
            BioTestSuiteContext *context = calloc(1, sizeof(BioTestSuiteContext));
            if (context) {
                BIO_TestSuite_Initialize(context, bioQueueMgr, panel, 
                                       PANEL_STR_BIOLOGIC_STATUS, PANEL_LED_BIOLOGIC_STATUS);
                context->state = TEST_STATE_PREPARING;
                
                // Store pointer to running context
                g_biologicTestSuiteContext = context;
                
                // Start test in worker thread
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool, 
                    TestBiologicWorkerThread, context, &threadID);
            } else {
                // Failed to allocate - restore UI
                SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test BioLogic");
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
            break;
    }
    return 0;
}

int CVICALLBACK TestBiologicWorkerThread(void *functionData) {
    BioTestSuiteContext *context = (BioTestSuiteContext*)functionData;
    
    // Run the test suite
    int result = BIO_TestSuite_Run(context);
    
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
    SetCtrlVal(g_mainPanelHandle, PANEL_STR_BIOLOGIC_STATUS, statusMsg);
    
    // Update LED based on results
    if (context->state == TEST_STATE_COMPLETED && context->summary.failedTests == 0) {
        SetCtrlAttribute(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_GREEN);
        SetCtrlVal(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, 1);
    } else if (context->state == TEST_STATE_ABORTED) {
        SetCtrlAttribute(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_YELLOW);
        SetCtrlVal(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, 1);
    } else {
        SetCtrlAttribute(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_RED);
        SetCtrlVal(g_mainPanelHandle, PANEL_LED_BIOLOGIC_STATUS, 1);
    }
    
    // Log detailed results
    if (result > 0) {
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic test suite completed successfully (%d tests passed)", result);
    } else if (result == -2) {
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic test suite cancelled by user");
    } else if (result == 0) {
        LogWarningEx(LOG_DEVICE_BIO, "BioLogic test suite completed with failures");
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "BioLogic test suite failed with error: %d", result);
    }
    
    // Clean up
    BIO_TestSuite_Cleanup(context);
    
    // Clear the running context pointer
    g_biologicTestSuiteContext = NULL;
    
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
    
    // Restore Test BioLogic button
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_LABEL_TEXT, "Test BioLogic");
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    return 0;
}

/******************************************************************************
 * Test Suite Functions
 ******************************************************************************/

int BIO_TestSuite_Initialize(BioTestSuiteContext *context, BioQueueManager *bioQueueMgr, 
                            int panel, int statusControl, int ledControl) {
    if (!context || !bioQueueMgr) return -1;
    
    memset(context, 0, sizeof(BioTestSuiteContext));
    context->bioQueueMgr = bioQueueMgr;
    context->panelHandle = panel;
    context->statusStringControl = statusControl;
    context->ledControl = ledControl;
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

int BIO_TestSuite_Run(BioTestSuiteContext *context) {
    if (!context || !context->bioQueueMgr) return -1;
    
    context->state = TEST_STATE_RUNNING;
    context->cancelRequested = 0;
    
    LogMessageEx(LOG_DEVICE_BIO, "Starting BioLogic Test Suite");
    BIO_UpdateTestProgress(context, "Starting BioLogic Test Suite...");
    
    // Run each test
    for (int i = 0; i < numTestCases; i++) {
        // Check for cancellation before starting each test
        if (context->cancelRequested) {
            LogMessageEx(LOG_DEVICE_BIO, "Test suite cancelled before test %d", i + 1);
            break;
        }
        
        BioTestCase* test = &testCases[i];
        
        char progressMsg[256];
        snprintf(progressMsg, sizeof(progressMsg), "Running test %d/%d: %s", 
                i + 1, numTestCases, test->testName);
        BIO_UpdateTestProgress(context, progressMsg);
        
        LogMessageEx(LOG_DEVICE_BIO, "Running test: %s", test->testName);
        
        double startTime = GetTime();
        test->result = test->testFunction(context->bioQueueMgr, test->errorMessage, 
                                     sizeof(test->errorMessage));
        test->executionTime = GetTime() - startTime;
        
        if (test->result > 0) {
            LogMessageEx(LOG_DEVICE_BIO, "Test PASSED: %s (%.2f seconds)", 
                       test->testName, test->executionTime);
            context->summary.passedTests++;
        } else {
            LogErrorEx(LOG_DEVICE_BIO, "Test FAILED: %s - %s", 
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
    GenerateBioTestSummary(&context->summary, testCases, numTestCases);
    
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

void BIO_TestSuite_Cancel(BioTestSuiteContext *context) {
    if (context) {
        context->cancelRequested = 1;
        LogMessageEx(LOG_DEVICE_BIO, "Test suite cancellation requested");
    }
}

void BIO_TestSuite_Cleanup(BioTestSuiteContext *context) {
    if (context) {
        // No specific cleanup needed for BioLogic tests currently
        // This function exists for future expansion
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic test suite cleanup complete");
    }
}

/******************************************************************************
 * Individual Test Implementations
 ******************************************************************************/

int Test_BIO_Connection(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_BIO, "Testing BioLogic connection...");
    
    // Verify queue manager is valid
    if (!bioQueueMgr) {
        snprintf(errorMsg, errorMsgSize, "BioLogic queue manager is NULL");
        return -1;
    }
    
    // Check if BioLogic is connected
    BioQueueStats stats;
    BIO_QueueGetStats(bioQueueMgr, &stats);
    
    if (!stats.isConnected) {
        snprintf(errorMsg, errorMsgSize, "BioLogic is not connected");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "BioLogic queue manager is connected, testing communication...");
    
    // Test the connection using queued command
    BioCommandParams params = {0};
    BioCommandResult cmdResult;
    
    int result = BIO_QueueCommandBlocking(bioQueueMgr, BIO_CMD_TEST_CONNECTION,
                                        &params, BIO_PRIORITY_HIGH, &cmdResult,
                                        BIO_TEST_TIMEOUT_MS);
    
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Connection test failed: %s", GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "BioLogic connection test passed successfully");
    return 1;
}