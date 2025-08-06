/******************************************************************************
 * BatteryTester.c
 * 
 * Main application file for PSB 10000 Power Supply and Bio-Logic SP-150e
 * Battery Tester with Status Monitoring Module
 ******************************************************************************/

#include "common.h"
#include "BatteryTester.h"  
#include "biologic_queue.h"
#include "psb10000_queue.h"
#include "dtb4848_queue.h"
#include "teensy_queue.h"
#include "exp_cdc.h"
#include "exp_capacity.h"
#include "exp_soceis.h"
#include "logging.h"
#include "status.h"
#include "controls.h"

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define THREAD_POOL_SIZE        10
#define PSB_TARGET_SERIAL       "2872380001"  // Target PSB serial number

/******************************************************************************
 * Global Variables (defined here, declared extern in common.h)
 ******************************************************************************/
int g_mainPanelHandle = 0;
int g_debugMode = 0;
CmtThreadPoolHandle g_threadPool = 0;
CmtThreadLockHandle g_busyLock = 0;
int g_systemBusy = 0;

// Queue managers
PSBQueueManager *g_psbQueueMgr = NULL;
BioQueueManager *g_bioQueueMgr = NULL;
DTBQueueManager *g_dtbQueueMgr = NULL;
TNYQueueManager *g_tnyQueueMgr = NULL;

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
	
	// Initialize controls module
	Controls_Initialize(g_mainPanelHandle);
    
    // Initialize PSB queue manager with specific port
	if (ENABLE_PSB) {
	    LogMessage("Initializing PSB queue manager on COM%d...", PSB_COM_PORT);
	    g_psbQueueMgr = PSB_QueueInit(PSB_COM_PORT, PSB_SLAVE_ADDRESS, PSB_BAUD_RATE);
	    
	    if (g_psbQueueMgr) {
	        PSB_SetGlobalQueueManager(g_psbQueueMgr);
	        
	        // Check if connected
	        PSBQueueStats stats;
	        PSB_QueueGetStats(g_psbQueueMgr, &stats);
	        if (stats.isConnected) {
	            LogMessage("PSB queue manager initialized and connected on COM%d", PSB_COM_PORT);
	            
	            // Initialize PSB to safe state
	            LogMessage("Initializing PSB to safe state...");
	            
	            // First set safe limits
	            int limitResult = PSB_SetSafeLimitsQueued();
	            if (limitResult != PSB_SUCCESS) {
	                LogWarning("Failed to set all PSB safe limits: %s", PSB_GetErrorString(limitResult));
	            }
	            
	            // Then zero all values
	            int zeroResult = PSB_ZeroAllValuesQueued();
	            if (zeroResult != PSB_SUCCESS) {
	                LogWarning("Failed to zero all PSB values: %s", PSB_GetErrorString(zeroResult));
	            }
	            
	            LogMessage("PSB initialization complete");
	        } else {
	            LogWarning("PSB queue manager initialized but not connected on COM%d", PSB_COM_PORT);
	        }
	    } else {
	        LogError("Failed to initialize PSB queue manager on COM%d", PSB_COM_PORT);
	    }
	}

	// Initialize BioLogic queue manager if BioLogic monitoring is enabled
	if (ENABLE_BIOLOGIC) {
	    LogMessage("Initializing BioLogic queue manager...");
	    g_bioQueueMgr = BIO_QueueInit(BIOLOGIC_DEFAULT_ADDRESS);
	    
	    if (g_bioQueueMgr) {
	        BIO_SetGlobalQueueManager(g_bioQueueMgr);
	        LogMessage("BioLogic queue manager initialized");
	    }
	}

	// Initialize DTB queue manager with specific port
	if (ENABLE_DTB) {
	    LogMessage("Initializing DTB queue manager on COM%d...", DTB_COM_PORT);
	    g_dtbQueueMgr = DTB_QueueInit(DTB_COM_PORT, DTB_SLAVE_ADDRESS, DTB_BAUD_RATE);
	    
	    if (g_dtbQueueMgr) {
	        DTB_SetGlobalQueueManager(g_dtbQueueMgr);
	        
	        // Check if connected
	        DTBQueueStats stats;
	        DTB_QueueGetStats(g_dtbQueueMgr, &stats);
	        if (stats.isConnected) {
	            LogMessage("DTB queue manager initialized and connected on COM%d", DTB_COM_PORT);
	            
	            // Check current write access status
	            int writeEnabled = 0;
	            int statusResult = DTB_GetWriteAccessStatusQueued(&writeEnabled);
	            if (statusResult == DTB_SUCCESS) {
	                LogMessage("DTB write access currently: %s", writeEnabled ? "ENABLED" : "DISABLED");
	            }
	            
	            // Enable write access if needed
	            if (!writeEnabled) {
	                LogMessage("Enabling DTB write access...");
	                int writeResult = DTB_EnableWriteAccessQueued();
	                if (writeResult != DTB_SUCCESS) {
	                    LogError("Failed to enable DTB write access: %s", 
	                            DTB_GetErrorString(writeResult));
	                } else {
	                    LogMessage("DTB write access enabled successfully");
	                }
	            }
	            
	            // Configure DTB for K-type thermocouple with PID control
	            LogMessage("Configuring DTB4848 for K-type thermocouple with PID control...");
	            int configResult = DTB_ConfigureDefaultQueued();
	            
	            if (configResult == DTB_SUCCESS) {
	                LogMessage("DTB4848 configured successfully");
	                
	                // Optionally re-enable write protection for safety
	                // LogMessage("Re-enabling DTB write protection...");
	                // DTB_DisableWriteAccessQueued(dtbHandle);
	            } else {
	                LogWarning("DTB4848 configuration failed: %s", 
	                          DTB_GetErrorString(configResult));
	                // Device may still work with existing configuration
	            }
	        } else {
	            LogWarning("DTB queue manager initialized but not connected on COM%d", DTB_COM_PORT);
	        }
	    } else {
	        LogError("Failed to initialize DTB queue manager on COM%d", DTB_COM_PORT);
	    }
	}
	
	// Initialize teensy manager
	if (ENABLE_TNY) {
	    LogMessage("Initializing Teensy queue manager on COM%d...", TNY_COM_PORT);
	    g_tnyQueueMgr = TNY_QueueInit(TNY_COM_PORT, TNY_DEFAULT_BAUD_RATE);
	    
	    if (g_tnyQueueMgr) {
	        TNY_SetGlobalQueueManager(g_tnyQueueMgr);
	        
	        // Check if connected
	        TNYQueueStats stats;
	        TNY_QueueGetStats(g_tnyQueueMgr, &stats);
	        if (stats.isConnected) {
	            LogMessage("Teensy queue manager initialized and connected on COM%d", TNY_COM_PORT);
	            
	            // Optional: Initialize pins to known state
	            int lowPins[] = {0, 1};
	            TNY_InitializePins(lowPins, 2, NULL, 0);
	        } else {
	            LogWarning("Teensy queue manager initialized but not connected on COM%d", TNY_COM_PORT);
	        }
	    } else {
	        LogError("Failed to initialize Teensy queue manager on COM%d", TNY_COM_PORT);
	    }
	}
    
    // Now start status monitoring (which will use the queue managers)
    Status_Start();
    Controls_Start();
	
    // Display panel
    DisplayPanel(g_mainPanelHandle);
    SetActiveCtrl(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT);
    
    // Run the UI
    RunUserInterface();
    
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
            
			// Check if CDC test is running and abort it
			if (CDCTest_IsRunning()) {
			    LogMessage("Aborting running CDC test...");
			    CDCTest_Abort();
			    
			    // Give it a moment to clean up properly
			    ProcessSystemEvents();
			    Delay(0.5);
			}
			
            // Check if capacity test is running and abort it
			if (CapacityTest_IsRunning()) {
			    LogMessage("Aborting running capacity test...");
			    CapacityTest_Abort();
			    
			    // Give it a moment to clean up properly
			    ProcessSystemEvents();
			    Delay(0.5);
			}

			// Check if SOCEIS test is running and abort it
			if (SOCEISTest_IsRunning()) {
			    LogMessage("Aborting running SOCEIS test...");
			    SOCEISTest_Abort();
			    
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

			// Shutdown DTB queue manager
			if (g_dtbQueueMgr) {
			    LogMessage("Shutting down DTB queue manager...");
			    DTBQueueManager *tempMgr = g_dtbQueueMgr;
			    g_dtbQueueMgr = NULL;  // Clear global pointer FIRST
			    DTB_SetGlobalQueueManager(NULL);  // Clear global reference
			    DTB_QueueShutdown(tempMgr);  // Then shutdown
			}
			
			// Shutdown Teensy queue manager
			if (g_tnyQueueMgr) {
			    LogMessage("Shutting down Teensy queue manager...");
			    TNYQueueManager *tempMgr = g_tnyQueueMgr;
			    g_tnyQueueMgr = NULL;  // Clear global pointer FIRST
			    TNY_SetGlobalQueueManager(NULL);  // Clear global reference
			    TNY_QueueShutdown(tempMgr);  // Then shutdown
			}

			// All queue shutdown functions already wait for their threads
			ProcessSystemEvents();
			Delay(0.2);
			
			// Clean up CDC test module
			LogMessage("Cleaning up CDC test module...");
			CDCTest_Cleanup();
            
            // Clean up capacity test module
            LogMessage("Cleaning up capacity test module...");
            CapacityTest_Cleanup();
			
			// Clean up SOCEIS test module
			LogMessage("Cleaning up SOCEIS test module...");
			SOCEISTest_Cleanup();
            
			LogMessage("Stopping controls module...");
			Controls_Cleanup();

			LogMessage("Stopping status monitoring...");
			Status_Stop();
            
            // Clean up thread pool
            if (g_threadPool) {
                LogMessage("Shutting down thread pool...");
                
                // All worker threads should have completed by now since they all wait for their threads
                
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