/******************************************************************************
 * biologic_test.c
 * 
 * BioLogic Test Suite Implementation
 * Updated to use high-level technique functions
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
    {"Connection Test", Test_BIO_Connection, 0, "", 0.0},
    {"OCV Test", Test_BIO_OCV, 0, "", 0.0},
    {"PEIS Test", Test_BIO_PEIS, 0, "", 0.0},
    {"GEIS Test", Test_BIO_GEIS, 0, "", 0.0}
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

void Test_BIO_TechniqueProgress(double elapsedTime, int memFilled, void *userData) {
    BioTestSuiteContext *context = (BioTestSuiteContext*)userData;
    if (!context) return;
    
    char progressMsg[256];
    snprintf(progressMsg, sizeof(progressMsg), 
             "Technique running: %.1f s elapsed, %d bytes collected", 
             elapsedTime, memFilled);
    BIO_UpdateTestProgress(context, progressMsg);
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
    
    // Test the connection
	int result = BIO_TestConnectionQueued(0, DEVICE_PRIORITY_NORMAL);
    
    if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "Connection test failed: %s", GetErrorString(result));
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "BioLogic connection test passed successfully");
    return 1;
}

int Test_BIO_OCV(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_BIO, "Testing BioLogic OCV functionality...");
    
    const uint8_t TEST_CHANNEL = 0;
    
    // Get device ID from queue manager
    int deviceID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (deviceID < 0) {
        snprintf(errorMsg, errorMsgSize, "No device connected");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "Running OCV measurement for %.1f seconds...", BIO_TEST_OCV_DURATION);
    
    // Run OCV measurement using high-level function
    BIO_TechniqueData *ocvData = NULL;
    int result = BIO_RunOCVQueued(
        deviceID,
        TEST_CHANNEL,
        BIO_TEST_OCV_DURATION,  // duration_s
        0.1,                    // sample_interval_s (100ms)
        10.0,                   // record_every_dE (10mV)
        0.1,                    // record_every_dT (100ms)
        2,                      // e_range (10V range)
        true,                   // Process the data
        &ocvData,
        0,                      // Use default timeout
		DEVICE_PRIORITY_NORMAL,
        Test_BIO_TechniqueProgress,  // Progress callback
        g_biologicTestSuiteContext,  // Pass test context
		&(g_biologicTestSuiteContext->cancelRequested) // Pass cancellation flag
    );
    
    if (result == BIO_ERR_PARTIAL_DATA) {
        LogWarningEx(LOG_DEVICE_BIO, "OCV measurement stopped with error, but partial data retrieved");
    } else if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "OCV measurement failed: %s", BIO_GetErrorString(result));
        return -1;
    }
    
    // Verify we got data
    if (!ocvData || !ocvData->rawData || ocvData->rawData->numPoints == 0) {
        snprintf(errorMsg, errorMsgSize, "No data received from OCV measurement");
        if (ocvData) BIO_FreeTechniqueData(ocvData);
        return -1;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "OCV Test Results:");
    LogMessageEx(LOG_DEVICE_BIO, "  Data Points: %d", ocvData->rawData->numPoints);
    LogMessageEx(LOG_DEVICE_BIO, "  Variables per Point: %d", ocvData->rawData->numVariables);
    LogMessageEx(LOG_DEVICE_BIO, "  Technique ID: %d", ocvData->rawData->techniqueID);
    LogMessageEx(LOG_DEVICE_BIO, "  Process Index: %d", ocvData->rawData->processIndex);
    
    // If we have converted data, display some sample values
    if (ocvData->convertedData && ocvData->convertedData->numPoints > 0) {
        LogMessageEx(LOG_DEVICE_BIO, "  Converted Variables: %d", ocvData->convertedData->numVariables);
        
        // Find time and Ewe columns
        int timeCol = -1, eweCol = -1;
        for (int i = 0; i < ocvData->convertedData->numVariables; i++) {
            if (strcmp(ocvData->convertedData->variableNames[i], "Time") == 0) timeCol = i;
            if (strcmp(ocvData->convertedData->variableNames[i], "Ewe") == 0) eweCol = i;
        }
        
        if (timeCol >= 0 && eweCol >= 0 && ocvData->convertedData->numPoints >= 3) {
            LogMessageEx(LOG_DEVICE_BIO, "  Sample Values:");
            // First point
            LogMessageEx(LOG_DEVICE_BIO, "    t=%.3f s, Ewe=%.3f V", 
                        ocvData->convertedData->data[timeCol][0],
                        ocvData->convertedData->data[eweCol][0]);
            // Middle point
            int mid = ocvData->convertedData->numPoints / 2;
            LogMessageEx(LOG_DEVICE_BIO, "    t=%.3f s, Ewe=%.3f V", 
                        ocvData->convertedData->data[timeCol][mid],
                        ocvData->convertedData->data[eweCol][mid]);
            // Last point
            int last = ocvData->convertedData->numPoints - 1;
            LogMessageEx(LOG_DEVICE_BIO, "    t=%.3f s, Ewe=%.3f V", 
                        ocvData->convertedData->data[timeCol][last],
                        ocvData->convertedData->data[eweCol][last]);
        }
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    
    // Clean up
    BIO_FreeTechniqueData(ocvData);
    
    LogDebugEx(LOG_DEVICE_BIO, "OCV test completed successfully");
    return 1;  // Test passed
}

int Test_BIO_PEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_BIO, "Testing BioLogic PEIS functionality...");
    
    const uint8_t TEST_CHANNEL = 0;
    
    // Get device ID from queue manager
    int deviceID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (deviceID < 0) {
        snprintf(errorMsg, errorMsgSize, "No device connected");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "Running PEIS measurement from %.0fHz to %.0fHz...", 
               BIO_TEST_PEIS_START_FREQ, BIO_TEST_PEIS_END_FREQ);
    
    // Run PEIS measurement using high-level function with proper parameters
    BIO_TechniqueData *peisData = NULL;
    int result = BIO_RunPEISQueued(
        deviceID,
        TEST_CHANNEL,
        true,                           // vs_initial (vs OCV)
        0.0,                           // initial_voltage_step (0V vs OCV)
        0.0,                           // duration_step (not used for single step)
        0.1,                           // record_every_dT (100ms)
        0.0,                           // record_every_dI (not used)
        BIO_TEST_PEIS_START_FREQ,      // initial_freq (100kHz)
        BIO_TEST_PEIS_END_FREQ,        // final_freq (10Hz)
        false,                         // sweep_linear (FALSE = logarithmic)
        0.010,                         // amplitude_voltage (10mV)
        10,                            // frequency_number (10 points per decade)
        1,                             // average_n_times (1 repeat)
        false,                         // correction (no non-stationary correction)
        0.0,                           // wait_for_steady (0 periods)
        true,                          // Process the data
        &peisData,
        0,                             // Use default timeout
		DEVICE_PRIORITY_NORMAL,
        Test_BIO_TechniqueProgress,    // Progress callback
        g_biologicTestSuiteContext,    // Pass test context
		&(g_biologicTestSuiteContext->cancelRequested) // Pass cancellation flag
    );
    
    if (result == BIO_ERR_PARTIAL_DATA) {
        LogWarningEx(LOG_DEVICE_BIO, "PEIS measurement stopped with error, but partial data retrieved");
    } else if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "PEIS measurement failed: %s", BIO_GetErrorString(result));
        return -1;
    }
    
    // Verify we got data
    if (!peisData || !peisData->rawData || peisData->rawData->numPoints == 0) {
        snprintf(errorMsg, errorMsgSize, "No data received from PEIS measurement");
        if (peisData) BIO_FreeTechniqueData(peisData);
        return -1;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "PEIS Test Results:");
    LogMessageEx(LOG_DEVICE_BIO, "  Data Points: %d", peisData->rawData->numPoints);
    LogMessageEx(LOG_DEVICE_BIO, "  Variables per Point: %d", peisData->rawData->numVariables);
    LogMessageEx(LOG_DEVICE_BIO, "  Technique ID: %d", peisData->rawData->techniqueID);
    LogMessageEx(LOG_DEVICE_BIO, "  Process Index: %d", peisData->rawData->processIndex);
    
    // If we have converted data, display some impedance values
    if (peisData->convertedData && peisData->convertedData->numPoints > 0) {
        LogMessageEx(LOG_DEVICE_BIO, "  Converted Variables: %d", peisData->convertedData->numVariables);
        
        // Find frequency, Re(Z), and Im(Z) columns
        int freqCol = -1, reCol = -1, imCol = -1;
        for (int i = 0; i < peisData->convertedData->numVariables; i++) {
            if (strcmp(peisData->convertedData->variableNames[i], "Frequency") == 0) freqCol = i;
            if (strcmp(peisData->convertedData->variableNames[i], "Re(Zwe)") == 0) reCol = i;
            if (strcmp(peisData->convertedData->variableNames[i], "Im(Zwe)") == 0) imCol = i;
        }
        
        if (freqCol >= 0 && reCol >= 0 && imCol >= 0 && peisData->convertedData->numPoints >= 3) {
            LogMessageEx(LOG_DEVICE_BIO, "  Sample Impedance Values:");
            // First point (high frequency)
            double mag0 = sqrt(pow(peisData->convertedData->data[reCol][0], 2) + 
                              pow(peisData->convertedData->data[imCol][0], 2));
            LogMessageEx(LOG_DEVICE_BIO, "    f=%.1f Hz, |Z|=%.3f Ohm, Re(Z)=%.3f Ohm, Im(Z)=%.3f Ohm", 
                        peisData->convertedData->data[freqCol][0],
                        mag0,
                        peisData->convertedData->data[reCol][0],
                        peisData->convertedData->data[imCol][0]);
            
            // Last point (low frequency)
            int last = peisData->convertedData->numPoints - 1;
            double magLast = sqrt(pow(peisData->convertedData->data[reCol][last], 2) + 
                                 pow(peisData->convertedData->data[imCol][last], 2));
            LogMessageEx(LOG_DEVICE_BIO, "    f=%.1f Hz, |Z|=%.3f Ohm, Re(Z)=%.3f Ohm, Im(Z)=%.3f Ohm", 
                        peisData->convertedData->data[freqCol][last],
                        magLast,
                        peisData->convertedData->data[reCol][last],
                        peisData->convertedData->data[imCol][last]);
        }
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    
    // Clean up
    BIO_FreeTechniqueData(peisData);
    
    LogDebugEx(LOG_DEVICE_BIO, "PEIS test completed successfully");
    return 1;
}

int Test_BIO_GEIS(BioQueueManager *bioQueueMgr, char *errorMsg, int errorMsgSize) {
    LogDebugEx(LOG_DEVICE_BIO, "Testing BioLogic GEIS functionality...");
    
    const uint8_t TEST_CHANNEL = 0;
    
    // Get device ID from queue manager
    int deviceID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (deviceID < 0) {
        snprintf(errorMsg, errorMsgSize, "No device connected");
        return -1;
    }
    
    LogDebugEx(LOG_DEVICE_BIO, "Running GEIS measurement at %.1fmA from %.0fHz to %.0fHz...", 
               BIO_TEST_GEIS_INIT_I * 1000, BIO_TEST_GEIS_START_FREQ, BIO_TEST_GEIS_END_FREQ);
    
    // Run GEIS measurement using high-level function
    BIO_TechniqueData *geisData = NULL;
    int result = BIO_RunGEISQueued(
        deviceID,
        TEST_CHANNEL,
        true,                          // vs_initial (vs initial current)
        BIO_TEST_GEIS_INIT_I,         // initial_current_step (0A)
        0.0,                          // duration_step (not used for single step)
        0.1,                          // record_every_dT (100ms)
        0.010,                        // record_every_dE (10mV)
        BIO_TEST_GEIS_START_FREQ,     // initial_freq (1kHz)
        BIO_TEST_GEIS_END_FREQ,       // final_freq (100Hz)
        false,                        // sweep_linear (FALSE = logarithmic)
        BIO_TEST_GEIS_AMPLITUDE_I,    // amplitude_current (10mA)
        5,                            // frequency_number (5 frequencies per decade)
        1,                            // average_n_times (1 repeat)
        false,                        // correction (no non-stationary correction)
        0.0,                          // wait_for_steady (0 periods)
        KBIO_IRANGE_100mA,           // i_range (100mA range)
        true,                         // Process the data
        &geisData,
        0,                            // Use default timeout
		DEVICE_PRIORITY_NORMAL,
        Test_BIO_TechniqueProgress,   // Progress callback
        g_biologicTestSuiteContext,    // Pass test context
		&(g_biologicTestSuiteContext->cancelRequested) // Pass cancellation flag
    );
    
    if (result == BIO_ERR_PARTIAL_DATA) {
        LogWarningEx(LOG_DEVICE_BIO, "GEIS measurement stopped with error, but partial data retrieved");
    } else if (result != SUCCESS) {
        snprintf(errorMsg, errorMsgSize, "GEIS measurement failed: %s", BIO_GetErrorString(result));
        return -1;
    }
    
    // Verify we got data
    if (!geisData || !geisData->rawData || geisData->rawData->numPoints == 0) {
        snprintf(errorMsg, errorMsgSize, "No data received from GEIS measurement");
        if (geisData) BIO_FreeTechniqueData(geisData);
        return -1;
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    LogMessageEx(LOG_DEVICE_BIO, "GEIS Test Results:");
    LogMessageEx(LOG_DEVICE_BIO, "  Data Points: %d", geisData->rawData->numPoints);
    LogMessageEx(LOG_DEVICE_BIO, "  Variables per Point: %d", geisData->rawData->numVariables);
    LogMessageEx(LOG_DEVICE_BIO, "  Technique ID: %d", geisData->rawData->techniqueID);
    LogMessageEx(LOG_DEVICE_BIO, "  Process Index: %d", geisData->rawData->processIndex);
    
    // If we have converted data, display some impedance values
    if (geisData->convertedData && geisData->convertedData->numPoints > 0) {
        LogMessageEx(LOG_DEVICE_BIO, "  Converted Variables: %d", geisData->convertedData->numVariables);
        
        // Find frequency, Re(Z), and Im(Z) columns
        int freqCol = -1, reCol = -1, imCol = -1;
        for (int i = 0; i < geisData->convertedData->numVariables; i++) {
            if (strcmp(geisData->convertedData->variableNames[i], "Frequency") == 0) freqCol = i;
            if (strcmp(geisData->convertedData->variableNames[i], "Re(Zwe)") == 0) reCol = i;
            if (strcmp(geisData->convertedData->variableNames[i], "Im(Zwe)") == 0) imCol = i;
        }
        
        if (freqCol >= 0 && reCol >= 0 && imCol >= 0 && geisData->convertedData->numPoints >= 2) {
            LogMessageEx(LOG_DEVICE_BIO, "  Sample GEIS Impedance Values:");
            // First point (high frequency)
            double mag0 = sqrt(pow(geisData->convertedData->data[reCol][0], 2) + 
                              pow(geisData->convertedData->data[imCol][0], 2));
            LogMessageEx(LOG_DEVICE_BIO, "    f=%.1f Hz, |Z|=%.3f Ohm, Re(Z)=%.3f Ohm, Im(Z)=%.3f Ohm", 
                        geisData->convertedData->data[freqCol][0],
                        mag0,
                        geisData->convertedData->data[reCol][0],
                        geisData->convertedData->data[imCol][0]);
            
            // Last point (low frequency)
            int last = geisData->convertedData->numPoints - 1;
            double magLast = sqrt(pow(geisData->convertedData->data[reCol][last], 2) + 
                                 pow(geisData->convertedData->data[imCol][last], 2));
            LogMessageEx(LOG_DEVICE_BIO, "    f=%.1f Hz, |Z|=%.3f Ohm, Re(Z)=%.3f Ohm, Im(Z)=%.3f Ohm", 
                        geisData->convertedData->data[freqCol][last],
                        magLast,
                        geisData->convertedData->data[reCol][last],
                        geisData->convertedData->data[imCol][last]);
        }
    }
    
    LogMessageEx(LOG_DEVICE_BIO, "========================================");
    
    // Clean up
    BIO_FreeTechniqueData(geisData);
    
    LogDebugEx(LOG_DEVICE_BIO, "GEIS test completed successfully");
    return 1;
}
