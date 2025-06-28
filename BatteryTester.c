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
#include "exp_capacity.h"
#include "logging.h"
#include "status.h"

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define THREAD_POOL_SIZE        4
#define PSB_TARGET_SERIAL       "2872380001"  // Target PSB serial number

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
static int CVICALLBACK UpdateThread(void *functionData);
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
// Test suite contexts for different devices
static TestSuiteContext g_psbTestContext = {0};
static TestSuiteContext g_bioTestContext = {0};
static TestSuiteContext *g_psbRunningContext = NULL;  // Pointer to the actual running context

// Queue managers
static PSBQueueManager *g_psbQueueMgr = NULL;
static BioQueueManager *g_bioQueueMgr = NULL;

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
    SetCtrlVal(g_mainPanelHandle, PANEL_STR_PSB_STATUS, statusMsg);
    
    // Log detailed results
    if (result > 0) {
        LogMessageEx(LOG_DEVICE_PSB, "PSB test suite completed successfully (%d tests passed)", result);
    } else if (result == -2) {
        LogMessageEx(LOG_DEVICE_PSB, "PSB test suite cancelled by user");
    } else if (result == 0) {
        LogWarningEx(LOG_DEVICE_PSB, "PSB test suite completed with failures");
    } else {
        LogErrorEx(LOG_DEVICE_PSB, "PSB test suite failed with error: %d", result);
    }
    
    // Clean up
    PSB_TestSuite_Cleanup(context);
    
    // Update global context state
    g_psbTestContext.state = context->state;
    g_psbTestContext.summary = context->summary;
    
    // Clear the running context pointer
    g_psbRunningContext = NULL;
    
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
    
    // Re-enable manual controls
    SetCtrlAttribute(g_mainPanelHandle, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
    
    // Restore Test PSB button
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_LABEL_TEXT, "Test PSB");
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 0);
    
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
            // Check if this is a cancel request (test is running)
            if (g_psbRunningContext != NULL) {
                LogMessage("User requested to cancel PSB test suite");
                PSB_TestSuite_Cancel(g_psbRunningContext);
                
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
            
            // Dim EXPERIMENTS tab control
            SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 1);
            
            // Dim all tabs
            int numTabs;
            GetNumTabPages(panel, PANEL_EXPERIMENTS, &numTabs);
            for (int i = 0; i < numTabs; i++) {
                SetTabPageAttribute(panel, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 1);
            }
            
            // Dim manual controls except Test PSB button
            SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 1);
            SetCtrlAttribute(panel, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 1);
            
            // Change Test PSB button text to "Cancel"
            SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Cancel");
            
            // Create test context
            TestSuiteContext *context = calloc(1, sizeof(TestSuiteContext));
            if (context) {
                PSB_TestSuite_Initialize(context, psbHandle, panel, PANEL_STR_PSB_STATUS);
                context->state = TEST_STATE_PREPARING;
                
                // Store pointer to running context
                g_psbRunningContext = context;
                
                // Store copy in global context for status tracking
                g_psbTestContext = *context;
                
                // Start test in worker thread
                CmtThreadFunctionID threadID;
                CmtScheduleThreadPoolFunction(g_threadPool, 
                    TestPSBWorkerThread, context, &threadID);
            } else {
                // Failed to allocate - restore UI
                SetCtrlAttribute(panel, PANEL_EXPERIMENTS, ATTR_DIMMED, 0);
                
                // Re-enable all tabs
                for (int i = 0; i < numTabs; i++) {
                    SetTabPageAttribute(panel, PANEL_EXPERIMENTS, i, ATTR_DIMMED, 0);
                }
                
                SetCtrlAttribute(panel, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
                SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Test PSB");
                
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
            
            // Check if capacity test is running and abort it
            if (CapacityTest_IsRunning()) {
                LogMessage("Aborting running capacity test...");
                CapacityTest_Abort();
                
                // Give it a moment to clean up properly
                ProcessSystemEvents();
                Delay(0.5);
            }
            
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
            
            // Clean up capacity test module
            LogMessage("Cleaning up capacity test module...");
            CapacityTest_Cleanup();
            
            // Clean up status module
            Status_Cleanup();
            
            // Clean up thread pool
            if (g_threadPool) {
                LogMessage("Shutting down thread pool...");
                
                // All worker threads should have completed by now since:
                // - Status_Stop() waits for its threads
                // - PSB_QueueShutdown() waits for its thread
                // - BIO_QueueShutdown() waits for its thread
                // - CapacityTest_Abort() waits for its thread
                
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