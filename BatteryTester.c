/******************************************************************************
 * BatteryTester.c
 * 
 * Main application file for PSB 10000 Power Supply and Bio-Logic SP-150e
 * Battery Tester with Status Monitoring Module
 ******************************************************************************/

#include "common.h"
#include "BatteryTester.h"  
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "psb10000_test.h"
#include "logging.h"
#include "status.h"

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define THREAD_POOL_SIZE        3
#define PSB_TARGET_SERIAL       "2872380001"  // Target PSB serial number

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
static int CVICALLBACK UpdateThread(void *functionData);
static int CVICALLBACK PSBTestSuiteThread(void *functionData);
void PSB_SetGlobalQueueManager(PSBQueueManager *mgr);

/******************************************************************************
 * Global Variables (defined here, declared extern in common.h)
 ******************************************************************************/
int g_mainPanelHandle = 0;
int g_debugMode = 0;
CmtThreadPoolHandle g_threadPool = 0;
CmtThreadLockHandle g_busyLock = 0;
int g_systemBusy = 0;

/******************************************************************************
 * Module-Specific Global Variables
 ******************************************************************************/
static CmtThreadFunctionID testSuiteThreadID = 0;
static int testButtonControl = 0;

// Test suite context
static TestSuiteContext testContext;
static TestState testSuiteState = TEST_STATE_IDLE;

// Queue managers
static PSBQueueManager *g_psbQueueMgr = NULL;
static BioQueueManager *g_bioQueueMgr = NULL;

/******************************************************************************
 * Test Suite Thread Function
 ******************************************************************************/
static int CVICALLBACK PSBTestSuiteThread(void *functionData) {
    testSuiteState = TEST_STATE_RUNNING;
    
    LogMessageEx(LOG_DEVICE_PSB, "Initializing test suite...");
    
    // Pause status monitoring to prevent concurrent access
    Status_Pause();
	
	// Wait for the status to pause completely to avoid data corruption
	Delay(UI_UPDATE_RATE_SLOW);
    
    // Get PSB handle from queue manager
    PSB_Handle* psb = PSB_QueueGetHandle(g_psbQueueMgr);
    if (psb == NULL) {
        // Update UI and log error
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, "PSB not connected at thread execution");
        }
        LogErrorEx(LOG_DEVICE_PSB, "PSB not connected at thread execution");
        testSuiteState = TEST_STATE_ERROR;
        
        // Resume status monitoring
        Status_Resume();
        
        // Re-enable the test button
        if (testButtonControl > 0) {
            SetCtrlAttribute(g_mainPanelHandle, testButtonControl, ATTR_DIMMED, 0);
        }
        return -1;
    }
    
    // Initialize test context (no progress callback needed since we're logging directly)
    PSB_TestSuite_Initialize(&testContext, psb, g_mainPanelHandle, PANEL_STR_PSB_STATUS);
    testContext.progressCallback = NULL;
    
    // Run the test suite
    int result = PSB_TestSuite_Run(&testContext);
    
    // Cleanup
    PSB_TestSuite_Cleanup(&testContext);
    
    // Final status
    char finalMsg[MEDIUM_BUFFER_SIZE];
    if (result > 0) {
        SAFE_SPRINTF(finalMsg, sizeof(finalMsg), 
                "Test Suite PASSED! All %d tests completed successfully.", 
                testContext.summary.totalTests);
        testSuiteState = TEST_STATE_COMPLETED;
        
        // Update UI and log success
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, finalMsg);
        }
        LogMessageEx(LOG_DEVICE_PSB, "%s", finalMsg);
    } else {
        SAFE_SPRINTF(finalMsg, sizeof(finalMsg), 
                "Test Suite FAILED: %d passed, %d failed out of %d tests.", 
                testContext.summary.passedTests,
                testContext.summary.failedTests,
                testContext.summary.totalTests);
        testSuiteState = TEST_STATE_ERROR;
        
        // Update UI and log error
        if (g_mainPanelHandle > 0) {
            SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, finalMsg);
        }
        LogErrorEx(LOG_DEVICE_PSB, "%s", finalMsg);
    }
    
    // Resume status monitoring
    Status_Resume();
    
    // Re-enable the test button
    if (testButtonControl > 0) {
        SetCtrlAttribute(g_mainPanelHandle, testButtonControl, ATTR_DIMMED, 0);
    }
    
    return 0;
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main (int argc, char *argv[]) {    
    if (InitCVIRTE (0, argv, 0) == 0)
        return -1;    /* out of memory */
    
    // Create thread pool first
    CmtNewThreadPool(DEFAULT_THREAD_POOL_SIZE, &g_threadPool);
    
    // Create busy lock
    CmtNewLock(NULL, 0, &g_busyLock);
    
    // Initialize logging
    RegisterLoggingCleanup();
    
    // Load panel
    if ((g_mainPanelHandle = LoadPanel (0, "BatteryTester.uir", PANEL)) < 0)
        return -1;
    
    // Initialize status monitoring BEFORE queue managers
    Status_Initialize(g_mainPanelHandle);
    
    // Initialize PSB queue manager with auto-discovery
    if (STATUS_MONITOR_PSB) {
        LogMessage("Initializing PSB queue manager...");
        g_psbQueueMgr = PSB_QueueInit(PSB_TARGET_SERIAL);
        
        if (g_psbQueueMgr) {
		    PSB_SetGlobalQueueManager(g_psbQueueMgr);
		    
		    // Just check if connected, don't set handle
		    PSBQueueStats stats;
		    PSB_QueueGetStats(g_psbQueueMgr, &stats);
		    if (stats.isConnected) {
		        LogMessage("PSB queue manager initialized and connected");
		    } else {
		        LogWarning("PSB queue manager initialized but not connected");
		    }
		} else {
            LogError("Failed to initialize PSB queue manager");
        }
    }
    
    // Initialize BioLogic queue manager if BioLogic monitoring is enabled
	if (STATUS_MONITOR_BIOLOGIC) {
	    LogMessage("Initializing BioLogic queue manager...");
	    g_bioQueueMgr = BIO_QueueInit(BIOLOGIC_DEFAULT_ADDRESS);  // Pass address, not -1
	    
	    if (g_bioQueueMgr) {
	        BIO_SetGlobalQueueManager(g_bioQueueMgr);
	        LogMessage("BioLogic queue manager initialized");
	    }
	}
    
    // Now start status monitoring (which will use the queue managers)
    Status_Start();
    
    // Display panel
    DisplayPanel (g_mainPanelHandle);
    
    // Run the UI
    RunUserInterface ();
    
    // Cleanup
    if (g_psbQueueMgr) {
        PSB_QueueShutdown(g_psbQueueMgr);
        PSB_SetGlobalQueueManager(NULL);
    }
    
    if (g_bioQueueMgr) {
        BIO_QueueShutdown(g_bioQueueMgr);
        BIO_SetGlobalQueueManager(NULL);
    }
    
    Status_Cleanup();
    
    // Dispose lock
    if (g_busyLock) {
        CmtDiscardLock(g_busyLock);
    }
    
    // Discard panel and thread pool
    DiscardPanel (g_mainPanelHandle);
    if (g_threadPool) {
        CmtDiscardThreadPool(g_threadPool);
    }
    
    return 0;
}

/******************************************************************************
 * Modified Remote Mode Toggle Callback
 ******************************************************************************/

// Worker thread data structure
typedef struct {
    int panel;
    int control;
    int enable;
} RemoteModeData;

// Worker thread function
int CVICALLBACK RemoteModeWorkerThread(void *functionData) {
    RemoteModeData *data = (RemoteModeData*)functionData;
    PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
    
    if (psbHandle) {
        // Use queued version
        int result = PSB_SetRemoteModeQueued(psbHandle, data->enable);
        
        if (result != PSB_SUCCESS) {
            LogError("Failed to set remote mode: %s", PSB_GetErrorString(result));
            
            // On failure, get current state and update LED
            PSB_Status status;
            int currentState = 0;  // Default to off
            if (PSB_GetStatusQueued(psbHandle, &status) == PSB_SUCCESS) {
                currentState = status.remoteMode;
            }
            Status_UpdateRemoteLED(currentState);
        } else {
            // Update LED to new state
            Status_UpdateRemoteLED(data->enable);
        }
    } else {
        LogWarning("PSB not connected - cannot change remote mode");
    }
    
    // Clear pending state
    Status_SetRemoteModeChangePending(0, 0);
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    free(data);
    return 0;
}

int CVICALLBACK RemoteModeToggle (int panel, int control, int event,
                                 void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if system is busy
            CmtGetLock(g_busyLock);
            if (g_systemBusy) {
                CmtReleaseLock(g_busyLock);
                LogWarning("System is busy - please wait for current operation to complete");
                // Reset toggle to current state
                PSB_Handle *psbHandle = PSB_QueueGetHandle(g_psbQueueMgr);
                if (psbHandle) {
                    PSB_Status status;
                    if (PSB_GetStatusQueued(psbHandle, &status) == PSB_SUCCESS) {
                        SetCtrlVal(panel, control, status.remoteMode);
                    }
                }
                return 0;
            }
            
            // Mark system as busy
            g_systemBusy = 1;
            CmtReleaseLock(g_busyLock);
            
            // Get toggle value
            int enable;
            GetCtrlVal(panel, control, &enable);
            
            // Set pending flag in status module
            Status_SetRemoteModeChangePending(1, enable);
            
            // Create worker thread data
            RemoteModeData *data = malloc(sizeof(RemoteModeData));
            if (data) {
                data->panel = panel;
                data->control = control;
                data->enable = enable;
                
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool, 
                    RemoteModeWorkerThread, data, &threadID);
            } else {
                // Failed to allocate - clear busy flag and pending state
                Status_SetRemoteModeChangePending(0, 0);
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
            break;
    }
    return 0;
}

/******************************************************************************
 * Modified Test PSB Callback
 ******************************************************************************/

// Test suite worker thread
int CVICALLBACK TestPSBWorkerThread(void *functionData) {
    TestSuiteContext *context = (TestSuiteContext*)functionData;
    
    // Pause status monitoring during test
    Status_Pause();
    
    // Run the test suite
    int result = PSB_TestSuite_Run(context);
    
    // Resume status monitoring
    Status_Resume();
    
    // Clean up
    PSB_TestSuite_Cleanup(context);
    free(context);
    
    // Re-enable UI controls
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 0);
    SetCtrlAttribute(g_mainPanelHandle, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
    // ... enable other controls
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    return 0;
}

int CVICALLBACK TestPSBCallback (int panel, int control, int event,
                                void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT:
            // Check if system is busy
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
            
            // Disable UI controls during test
            SetCtrlAttribute(panel, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 1);
            SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 1);
            SetCtrlAttribute(panel, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 1);
            // ... disable other controls
            
            // Create test context
            TestSuiteContext *context = calloc(1, sizeof(TestSuiteContext));
            if (context) {
                PSB_TestSuite_Initialize(context, psbHandle, panel, PANEL_STR_PSB_STATUS);
                
                // Start test in worker thread
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool, 
                    TestPSBWorkerThread, context, &threadID);
            } else {
                // Failed to allocate
                SetCtrlAttribute(panel, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
                
                CmtGetLock(g_busyLock);
                g_systemBusy = 0;
                CmtReleaseLock(g_busyLock);
            }
            break;
    }
    return 0;
}

/******************************************************************************
 * Test BioLogic Button Callback
 ******************************************************************************/
int CVICALLBACK TestBiologicCallback(int panel, int control, int event,
                                     void *callbackData, int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    int result;
    char message[LARGE_BUFFER_SIZE];
    
    // Check if BioLogic is connected through queue manager
    BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
    if (!bioQueueMgr) {
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "BioLogic queue manager not initialized");
        LogErrorEx(LOG_DEVICE_BIO, "BioLogic queue manager not initialized");
        return 0;
    }
    
    BioQueueStats stats;
    BIO_QueueGetStats(bioQueueMgr, &stats);
    
    if (!stats.isConnected) {
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "BioLogic not connected");
        LogErrorEx(LOG_DEVICE_BIO, "BioLogic not connected");
        return 0;
    }
    
    // Disable the button to prevent multiple clicks
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);
    
    // Update UI
    SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Testing BioLogic connection...");
    ProcessDrawEvents();
    
    // Test the connection using queued command
    BioCommandParams params = {0};
    BioCommandResult cmdResult;
    
    result = BIO_QueueCommandBlocking(bioQueueMgr, BIO_CMD_TEST_CONNECTION,
                                    &params, BIO_PRIORITY_HIGH, &cmdResult,
                                    BIO_QUEUE_COMMAND_TIMEOUT_MS);
    
    if (result == SUCCESS) {
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, "Connection test passed!");
        LogMessageEx(LOG_DEVICE_BIO, "BioLogic connection test PASSED!");
        
        // Update LED to show success
        SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_GREEN);
        SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
    } else {
        SAFE_SPRINTF(message, sizeof(message), 
                    "Connection test failed: %s", GetErrorString(result));
        SetCtrlVal(panel, PANEL_STR_BIOLOGIC_STATUS, message);
        LogErrorEx(LOG_DEVICE_BIO, "Test Failed: %s", message);
        
        // Update LED to show error
        SetCtrlAttribute(panel, PANEL_LED_BIOLOGIC_STATUS, ATTR_ON_COLOR, VAL_RED);
        SetCtrlVal(panel, PANEL_LED_BIOLOGIC_STATUS, 1);
    }
    
    // Re-enable the button
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 0);
    
    return 0;
}

/******************************************************************************
 * Panel Callback - Cleanly shuts down the entire program
 ******************************************************************************/

int CVICALLBACK PanelCallback(int panel, int event, void *callbackData, 
                                   int eventData1, int eventData2) {
    switch (event) {
        case EVENT_CLOSE:
        case EVENT_COMMIT:  // For Exit/Quit buttons
            
            // Check if system is busy
            if (g_busyLock) {
                CmtGetLock(g_busyLock);
                if (g_systemBusy) {
                    CmtReleaseLock(g_busyLock);
                    
                    // Ask user if they really want to quit
                    int response = ConfirmPopup("System Busy", 
                        "An operation is in progress.\n\n"
                        "Are you sure you want to exit?");
                    
                    if (!response) {
                        return 0;  // Cancel the close
                    }
                    
                    // User wants to force quit - mark system as not busy
                    CmtGetLock(g_busyLock);
                    g_systemBusy = 0;
                    CmtReleaseLock(g_busyLock);
                } else {
                    CmtReleaseLock(g_busyLock);
                }
            }
            
            // Log shutdown
            LogMessage("========================================");
            LogMessage("Shutting down Battery Tester application");
            LogMessage("========================================");
            
            // Stop status monitoring first
            LogMessage("Stopping status monitoring...");
            Status_Stop();
            
            // The Status_Stop() function already waits for its threads to complete
            // using CmtWaitForThreadPoolFunctionCompletion, so we just need a small
            // delay to ensure everything is settled
            ProcessSystemEvents();
            Delay(0.2);
            
            // Shutdown PSB queue manager
			if (g_psbQueueMgr) {
			    LogMessage("Shutting down PSB queue manager...");
			    PSBQueueManager *tempMgr = g_psbQueueMgr;
			    g_psbQueueMgr = NULL;  // Clear global pointer FIRST
			    PSB_SetGlobalQueueManager(NULL);  // Clear global reference
			    PSB_QueueShutdown(tempMgr);  // Then shutdown
			}
            
            // Shutdown BioLogic queue manager
			if (g_bioQueueMgr) {
			    LogMessage("Shutting down BioLogic queue manager...");
			    BioQueueManager *tempMgr = g_bioQueueMgr;
			    g_bioQueueMgr = NULL;  // Clear global pointer FIRST
			    BIO_SetGlobalQueueManager(NULL);  // Clear global reference
			    BIO_QueueShutdown(tempMgr);  // Then shutdown
			}
            
            // Both queue shutdown functions already wait for their threads
            ProcessSystemEvents();
            Delay(0.2);
            
            // Clean up status module
            Status_Cleanup();
            
            // Clean up thread pool
            if (g_threadPool) {
                LogMessage("Shutting down thread pool...");
                
                // All worker threads should have completed by now since:
                // - Status_Stop() waits for its threads
                // - PSB_QueueShutdown() waits for its thread
                // - BIO_QueueShutdown() waits for its thread
                
                // Just give a small delay to ensure everything is cleaned up
                ProcessSystemEvents();
                Delay(0.1);
                
                CmtDiscardThreadPool(g_threadPool);
                g_threadPool = 0;
            }
            
            // Dispose of locks
            if (g_busyLock) {
                CmtDiscardLock(g_busyLock);
                g_busyLock = 0;
            }
            
            // Save any configuration or state if needed
            // SaveConfiguration();  // Implement if needed
            
            // Final cleanup
            LogMessage("Cleanup complete. Exiting application.");
            LogMessage("========================================");
            
            // Close logging system
            // CloseLogging();  // If you have a logging cleanup function
            
            // Quit the user interface
            QuitUserInterface(0);
            break;
    }
    return 0;
}