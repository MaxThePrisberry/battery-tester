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
#include "biologic_test.h"
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

// Queue managers
PSBQueueManager *g_psbQueueMgr = NULL;
BioQueueManager *g_bioQueueMgr = NULL;

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




/******************************************************************************
 * Test GEIS Callback - Quick GEIS test that matches working test suite
 ******************************************************************************/
int CVICALLBACK TestGEISCallback(int panel, int control, int event,
                                 void *callbackData, int eventData1, int eventData2) {
    switch (event) {
        case EVENT_COMMIT: {
            LogMessage("Starting GEIS Test");
            
            // Get queue manager
            BioQueueManager *bioMgr = BIO_GetGlobalQueueManager();
            if (!bioMgr) {
                LogError("BioLogic queue manager not initialized");
                return 0;
            }
            
            // Check connection
            BioQueueStats stats;
            BIO_QueueGetStats(bioMgr, &stats);
            if (!stats.isConnected) {
                LogError("BioLogic not connected");
                return 0;
            }
            
            int deviceID = BIO_QueueGetDeviceID(bioMgr);
            uint8_t channel = 0;
            
            // Stop channel if running
            BL_StopChannel(deviceID, channel);
            Delay(0.2);
            
            // GEIS parameters - matching the working test
            bool vs_initial = true;  // vs initial current
            double initial_current_step = 0.0;  // 0A DC bias
            double duration_step = 0.0;  // Not used for single step
            double record_every_dT = 0.1;  // 100ms
            double record_every_dE = 0.010;  // 10mV
            double initial_freq = 1000.0;  // 1kHz
            double final_freq = 100.0;  // 100Hz (not going too low)
            bool sweep_linear = false;  // Logarithmic
            double amplitude_current = 0.010;  // 10mA amplitude
            int frequency_number = 5;  // 5 frequencies
            int average_n_times = 1;  // No averaging
            bool correction = false;  // No correction
            double wait_for_steady = 0.0;  // No wait
            int i_range = KBIO_IRANGE_100mA;  // 100mA range
            
            LogMessage("Running GEIS: %.0f-%.0f Hz, %d points, %.0f mA amplitude", 
                       initial_freq, final_freq, frequency_number, amplitude_current * 1000);
            
            // Run GEIS
            BL_RawDataBuffer *rawData = NULL;
            int result = BL_RunGEISQueued(
                deviceID, channel,
                vs_initial, initial_current_step, duration_step,
                record_every_dT, record_every_dE,
                initial_freq, final_freq, sweep_linear,
                amplitude_current, frequency_number, average_n_times,
                correction, wait_for_steady, i_range, true,  // processData=true
                &rawData, 60000, NULL, NULL  // 60 second timeout
            );
            
            if (result != SUCCESS && result != BL_ERR_PARTIAL_DATA) {
                LogError("GEIS failed: %s (code %d)", BL_GetErrorString(result), result);
                return 0;
            }
            
            if (!rawData || rawData->numPoints == 0) {
                LogError("No data received");
                if (rawData) BL_FreeTechniqueResult(rawData);
                return 0;
            }
            
            LogMessage("Got %d data points, processing...", rawData->numPoints);
            
            // Process data
            uint32_t channelType;
            BL_GetChannelBoardType(deviceID, channel, &channelType);
            
            TCurrentValues_t currentValues;
            BL_GetCurrentValues(deviceID, channel, &currentValues);
            
            BL_ConvertedData *data = NULL;
            result = BL_ProcessTechniqueData(rawData, KBIO_TECHID_GEIS, 1,
                                           channelType, currentValues.TimeBase, &data);
            
            if (!data) {
                LogError("Failed to process data");
                BL_FreeTechniqueResult(rawData);
                return 0;
            }
            
            // Find impedance data
            int reIdx = 4, imIdx = 5, freqIdx = 0;  // Standard positions for GEIS
            
            // Verify we have the expected columns
            if (data->numVariables < 6) {
                LogError("Unexpected data format: only %d variables", data->numVariables);
                BL_FreeConvertedData(data);
                BL_FreeTechniqueResult(rawData);
                return 0;
            }
            
            // Plot Nyquist
            DeleteGraphPlot(panel, PANEL_GRAPH_BIOLOGIC, -1, VAL_IMMEDIATE_DRAW);
            
            double *realZ = malloc(data->numPoints * sizeof(double));
            double *negImagZ = malloc(data->numPoints * sizeof(double));
            
            if (realZ && negImagZ) {
                for (int i = 0; i < data->numPoints; i++) {
                    realZ[i] = data->data[reIdx][i];
                    negImagZ[i] = -data->data[imIdx][i];
                    LogDebug("Point %d: f=%.1f Hz, Z=%.3f-j%.3f Ohm", 
                             i, data->data[freqIdx][i], realZ[i], -negImagZ[i]);
                }
                
                PlotXY(panel, PANEL_GRAPH_BIOLOGIC, realZ, negImagZ,
                       data->numPoints, VAL_DOUBLE, VAL_DOUBLE,
                       VAL_SCATTER, VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_BLUE);
                
                // Connect the points with lines
                PlotXY(panel, PANEL_GRAPH_BIOLOGIC, realZ, negImagZ,
                       data->numPoints, VAL_DOUBLE, VAL_DOUBLE,
                       VAL_THIN_LINE, VAL_NO_POINT, VAL_SOLID, 1, VAL_BLUE);
                
                SetCtrlAttribute(panel, PANEL_GRAPH_BIOLOGIC, ATTR_XNAME, "Re(Z) [Ohm]");
                SetCtrlAttribute(panel, PANEL_GRAPH_BIOLOGIC, ATTR_YNAME, "-Im(Z) [Ohm]");
                SetCtrlAttribute(panel, PANEL_GRAPH_BIOLOGIC, ATTR_LABEL_TEXT, "GEIS Nyquist Plot");
            }
            
            // Save to CSV
            char filename[256];
            sprintf(filename, "GEIS_%ld.csv", time(NULL));
            FILE *f = fopen(filename, "w");
            if (f) {
                fprintf(f, "GEIS Test Results\n");
                fprintf(f, "Frequency Range: %.0f to %.0f Hz\n", initial_freq, final_freq);
                fprintf(f, "Amplitude: %.0f mA\n\n", amplitude_current * 1000);
                fprintf(f, "Freq[Hz],Re(Z)[Ohm],Im(Z)[Ohm],|Z|[Ohm],Phase[deg]\n");
                
                for (int i = 0; i < data->numPoints; i++) {
                    double re = data->data[reIdx][i];
                    double im = data->data[imIdx][i];
                    double mag = sqrt(re*re + im*im);
                    double phase = atan2(im, re) * 180.0 / M_PI;
                    
                    fprintf(f, "%.3f,%.6f,%.6f,%.6f,%.2f\n", 
                            data->data[freqIdx][i], re, im, mag, phase);
                }
                fclose(f);
                LogMessage("Data saved to %s", filename);
            }
            
            free(realZ);
            free(negImagZ);
            BL_FreeConvertedData(data);
            BL_FreeTechniqueResult(rawData);
            
            LogMessage("GEIS test completed");
            break;
        }
    }
    return 0;
}