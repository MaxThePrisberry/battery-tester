/******************************************************************************
 * BatteryTester.c
 * 
 * Main application file for PSB 10000 Power Supply and Bio-Logic SP-150e
 * Battery Tester with Status Monitoring Module
 ******************************************************************************/

#include "common.h"
#include "BatteryTester.h"
#include "biologic_queue.h"
#include "biologic_abstract.h"
#include "psb10000_queue.h"
#include "dtb4848_queue.h"
#include "teensy_queue.h"
#include "cdaq_utils.h"
#include "exp_baseline.h"
#include "exp_cdc.h"
#include "exp_ocv.h"
#include "logging.h"
#include "status.h"
#include "controls.h"

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
	
	// Load and display loading panel first
    int loadingPanelHandle = LoadPanel(0, "BatteryTester.uir", PANEL_LOAD);
    DisplayPanel(loadingPanelHandle);
    ProcessSystemEvents(); // Ensure panel displays immediately
	
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    
    // Create thread pool first
    CmtNewThreadPool(DEFAULT_THREAD_POOL_SIZE, &g_threadPool);
    
    // Create busy lock
    CmtNewLock(NULL, 0, &g_busyLock);
    
    // Initialize logging
    RegisterLoggingCleanup();
    
	// Initialize cDAQ module if enabled
	if (ENABLE_CDAQ) {
	    LogMessage("Initializing cDAQ module...");
	    int result = CDAQ_Initialize();
	    if (result == SUCCESS) {
	        LogMessage("cDAQ module initialized successfully");
	    } else {
	        LogError("Failed to initialize cDAQ module: %s", GetErrorString(result));
	    }
	}
	
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
	            int limitResult = PSB_SetSafeLimitsQueued(DEVICE_PRIORITY_NORMAL);
	            if (limitResult != PSB_SUCCESS) {
	                LogWarning("Failed to set all PSB safe limits: %s", PSB_GetErrorString(limitResult));
	            }
	            
	            // Then zero all values
	            int zeroResult = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
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

	// Initialize BioLogic using abstraction layer
	if (ENABLE_BIOLOGIC) {
	    LogMessage("Initializing BioLogic abstraction layer...");

	    BIO_Config bioConfig = {0};

#if BIOLOGIC_CONTROL_MODE == 1
	    // EC-Lab OLE COM mode
	    LogMessage("  Mode: EC-Lab OLE COM");
	    bioConfig.mode = BIO_MODE_ECLAB_OLECOM;
	    strncpy(bioConfig.eclab.settingsDir, ECLAB_SETTINGS_DIR, MAX_PATH - 1);
	    strncpy(bioConfig.eclab.dataDir, ECLAB_DATA_DIR, MAX_PATH - 1);
	    bioConfig.eclab.deviceNumber = ECLAB_DEVICE_NUMBER;
	    bioConfig.eclab.channelNumber = ECLAB_CHANNEL_NUMBER;
	    strncpy(bioConfig.eclab.ocvTemplate, ECLAB_OCV_TEMPLATE, MAX_PATH - 1);
	    strncpy(bioConfig.eclab.peisTemplate, ECLAB_PEIS_TEMPLATE, MAX_PATH - 1);
	    strncpy(bioConfig.eclab.geisTemplate, ECLAB_GEIS_TEMPLATE, MAX_PATH - 1);
#else
	    // Direct DLL mode
	    LogMessage("  Mode: Direct DLL");
	    bioConfig.mode = BIO_MODE_DIRECT_DLL;
	    strncpy(bioConfig.dll.deviceAddress, BIOLOGIC_DEFAULT_ADDRESS, 63);
	    bioConfig.dll.timeout = BIOLOGIC_CONNECTION_TIMEOUT;
#endif

	    int result = BIO_InitializeAbstract(&bioConfig);
	    if (result == SUCCESS) {
	        LogMessage("BioLogic abstraction layer initialized successfully");
	    } else {
	        LogError("Failed to initialize BioLogic: error %d", result);
	    }
	}

	// Initialize DTB queue manager with multiple devices
	if (ENABLE_DTB) {
	    LogMessage("Initializing DTB queue manager on COM%d with %d devices...", 
	               DTB_COM_PORT, DTB_NUM_DEVICES);
	    
	    // Setup slave addresses array
	    int dtbSlaveAddresses[DTB_NUM_DEVICES] = {DTB1_SLAVE_ADDRESS, DTB2_SLAVE_ADDRESS};
	    
	    g_dtbQueueMgr = DTB_QueueInit(DTB_COM_PORT, DTB_BAUD_RATE, 
	                                  dtbSlaveAddresses, DTB_NUM_DEVICES);
	    
	    if (g_dtbQueueMgr) {
	        DTB_SetGlobalQueueManager(g_dtbQueueMgr);
	        
	        // Check if connected
	        DTBQueueStats stats;
	        DTB_QueueGetStats(g_dtbQueueMgr, &stats);
	        if (stats.isConnected) {
	            LogMessage("DTB queue manager initialized and connected on COM%d", DTB_COM_PORT);
	            
	            // Enable write access for all devices
	            LogMessage("Enabling DTB write access for all devices...");
	            int writeResult = DTB_EnableWriteAccessAllQueued(DEVICE_PRIORITY_NORMAL);
	            if (writeResult != DTB_SUCCESS) {
	                LogError("Failed to enable DTB write access for all devices: %s", 
	                        DTB_GetErrorString(writeResult));
	            } else {
	                LogMessage("DTB write access enabled successfully for all devices");
	            }
	            
	            // Configure all DTB devices for K-type thermocouple with PID control
	            LogMessage("Configuring all DTB4848 devices for K-type thermocouple with PID control...");
	            
	            // Try to configure each device individually to provide better error reporting
	            for (int i = 0; i < DTB_NUM_DEVICES; i++) {
	                int slaveAddr = dtbSlaveAddresses[i];
	                
	                // Check current write access status for this device
	                int writeEnabled = 0;
	                int statusResult = DTB_GetWriteAccessStatusQueued(slaveAddr, &writeEnabled, DEVICE_PRIORITY_NORMAL);
	                if (statusResult == DTB_SUCCESS) {
	                    LogMessage("DTB slave %d write access currently: %s", 
	                              slaveAddr, writeEnabled ? "ENABLED" : "DISABLED");
	                }
	                
	                // Configure this device
	                int configResult = DTB_ConfigureDefaultQueued(slaveAddr, DEVICE_PRIORITY_NORMAL);
	                if (configResult == DTB_SUCCESS) {
	                    LogMessage("DTB4848 slave %d configured successfully", slaveAddr);
	                } else {
	                    LogWarning("DTB4848 slave %d configuration failed: %s", 
	                              slaveAddr, DTB_GetErrorString(configResult));
	                    // Device may still work with existing configuration
	                }
	            }
	            
	            LogMessage("DTB initialization complete");
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
	            TNY_InitializePins(lowPins, 2, NULL, 0, DEVICE_PRIORITY_NORMAL);
	        } else {
	            LogWarning("Teensy queue manager initialized but not connected on COM%d", TNY_COM_PORT);
	        }
	    } else {
	        LogError("Failed to initialize Teensy queue manager on COM%d", TNY_COM_PORT);
	    }
	}
	
	// Load main panel
	DiscardPanel(loadingPanelHandle);
    if ((g_mainPanelHandle = LoadPanel(0, "BatteryTester.uir", PANEL)) < 0)
        return -1;
	
    // Initialize status and control modules
    Status_Initialize(g_mainPanelHandle);
	Controls_Initialize(g_mainPanelHandle);
    
    // Start both modules, which will use queue managers
    Status_Start();
    Controls_Start();
	
    // Display panel
    DisplayPanel(g_mainPanelHandle);
    SetActiveCtrl(g_mainPanelHandle, PANEL_STR_CMD_PROMPT_INPUT);
    
    // Run the UI
    RunUserInterface();
	
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
			if (CDCExperiment_IsRunning()) {
			    LogMessage("Aborting running CDC test...");
			    CDCExperiment_Abort();

			    // Give it a moment to clean up properly
			    ProcessSystemEvents();
			    Delay(0.5);
			}

			// Check if Baseline test is running and abort it
			if (BaselineExperiment_IsRunning()) {
			    LogMessage("Aborting running Baseline test...");
			    BaselineExperiment_Abort();

			    // Give it a moment to clean up properly
			    ProcessSystemEvents();
			    Delay(0.5);
			}

			// Check if OCV test is running and abort it
			if (OCVExperiment_IsRunning()) {
			    LogMessage("Aborting running OCV test...");
			    OCVExperiment_Abort();

			    // Give it a moment to clean up properly
			    ProcessSystemEvents();
			    Delay(0.5);
			}
			
			// Disconnect all physical relay lines
			const int pins[2] = {TNY_PSB_PIN, TNY_BIOLOGIC_PIN};
			const int vals[2] = {TNY_STATE_DISCONNECTED, TNY_STATE_DISCONNECTED};
			TNY_SetMultiplePinsQueued(pins, vals, 2, DEVICE_PRIORITY_HIGH);
            
            // Stop status monitoring first
            LogMessage("Stopping status monitoring...");
            Status_Stop();
            
            // The Status_Stop() function already waits for its threads to complete
            // using CmtWaitForThreadPoolFunctionCompletion, so we just need a small
            // delay to ensure everything is settled
            ProcessSystemEvents();
            Delay(0.2);
			
			// Clean up cDAQ module
			if (ENABLE_CDAQ) {
			    LogMessage("Cleaning up cDAQ module...");
			    CDAQ_Cleanup();
			}
            
            // Shutdown PSB queue manager
			if (g_psbQueueMgr) {
			    LogMessage("Shutting down PSB queue manager...");
			    PSBQueueManager *tempMgr = g_psbQueueMgr;
			    g_psbQueueMgr = NULL;  // Clear global pointer FIRST
			    PSB_SetGlobalQueueManager(NULL);  // Clear global reference
			    PSB_QueueShutdown(tempMgr);  // Then shutdown
			}

			// Shutdown BioLogic abstraction layer
			// The abstraction layer owns and manages the queue manager (if in DLL mode)
			if (BIO_IsAbstractInitialized()) {
			    LogMessage("Shutting down BioLogic abstraction layer...");
			    BIO_ShutdownAbstract();
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
			LogMessage("Cleaning up CDC experiment module...");
			CDCExperiment_Cleanup();

			// Clean up Baseline test module
			LogMessage("Cleaning up Baseline experiment module...");
			BaselineExperiment_Cleanup();

			// Clean up OCV test module
			LogMessage("Cleaning up OCV experiment module...");
			OCVExperiment_Cleanup();
            
			LogMessage("Cleaning up controls module...");
			Controls_Cleanup();

			LogMessage("Cleaning up status monitoring...");
			Status_Cleanup();
            
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
            
            // Final cleanup
            LogMessage("Cleanup complete. Exiting application.");
            LogMessage("========================================");
            
            // Discard the panel
			if (g_mainPanelHandle > 0) {
			    DiscardPanel(g_mainPanelHandle);
			    g_mainPanelHandle = 0;
			}
            
            // Quit the user interface
            QuitUserInterface(0);
			
			SetThreadExecutionState(ES_CONTINUOUS);
            break;
    }
    return 0;
}