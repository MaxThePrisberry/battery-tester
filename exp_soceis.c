/******************************************************************************
 * exp_soceis.c
 * 
 * Battery State of Charge EIS (SOCEIS) Experiment Module Implementation
 ******************************************************************************/

#include "common.h"
#include "exp_soceis.h"
#include "BatteryTester.h"
#include "logging.h"
#include "status.h"
#include "battery_utils.h"
#include <ansi_c.h>
#include <analysis.h>
#include <utility.h>
#include <time.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

// Experiment context and thread management
static SOCEISExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

static const int numControls = 6;
static const int controls[numControls] = {SOCEIS_NUM_CURRENT_THRESHOLD,
                                          SOCEIS_NUM_INTERVAL,
                                          SOCEIS_CHECKBOX_DISCHARGE,
                                          SOCEIS_NUM_CAPACITY,
                                          SOCEIS_NUM_EIS_INTERVAL,
                                          SOCEIS_BTN_IMPORT_SETTINGS};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CVICALLBACK SOCEISExperimentThread(void *functionData);
static int VerifyBatteryDischarged(SOCEISExperimentContext *ctx);
static int ConfigureGraphs(SOCEISExperimentContext *ctx);
static int CreateExperimentDirectory(SOCEISExperimentContext *ctx);
static int CalculateTargetSOCs(SOCEISExperimentContext *ctx);
static int SetRelayState(int pin, int state);
static int SwitchToBioLogic(SOCEISExperimentContext *ctx);
static int SwitchToPSB(SOCEISExperimentContext *ctx);
static int PerformEISMeasurement(SOCEISExperimentContext *ctx, double targetSOC);
static int RunOCVMeasurement(SOCEISExperimentContext *ctx, EISMeasurement *measurement);
static int RunGEISMeasurement(SOCEISExperimentContext *ctx, EISMeasurement *measurement);
static int ProcessGEISData(BIO_TechniqueData *geisData, EISMeasurement *measurement);
static int SaveMeasurementData(SOCEISExperimentContext *ctx, EISMeasurement *measurement);
static int RunChargingPhase(SOCEISExperimentContext *ctx);
static int UpdateSOCTracking(SOCEISExperimentContext *ctx, double voltage, double current);
static void UpdateGraphs(SOCEISExperimentContext *ctx, double current, double time);
static void UpdateOCVGraph(SOCEISExperimentContext *ctx, EISMeasurement *measurement);
static void UpdateNyquistPlot(SOCEISExperimentContext *ctx, EISMeasurement *measurement);
static int WriteResultsFile(SOCEISExperimentContext *ctx);
static void RestoreUI(SOCEISExperimentContext *ctx);
static void ClearGraphs(SOCEISExperimentContext *ctx);
static int DischargeToFiftyPercent(SOCEISExperimentContext *ctx);
static int AddDynamicTargetSOC(SOCEISExperimentContext *ctx, double targetSOC);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartSOCEISExperimentCallback(int panel, int control, int event,
                                             void *callbackData, int eventData1, 
                                             int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if SOCEIS experiment is already running
    if (SOCEISExperiment_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop SOCEIS experiment");
        g_experimentContext.state = SOCEIS_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the SOCEIS experiment.");
        return 0;
    }
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);
    
    // Check PSB connection
    PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
    if (!psbQueueMgr) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the SOCEIS experiment.");
        return 0;
    }
    
    PSB_Handle *psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!psbHandle || !psbHandle->isConnected) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the SOCEIS experiment.");
        return 0;
    }
    
    // Check BioLogic connection
    BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
    if (!bioQueueMgr) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before running the SOCEIS experiment.");
        return 0;
    }
    
    int biologicID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (biologicID < 0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before running the SOCEIS experiment.");
        return 0;
    }
    
    // Check Teensy connection
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Teensy Not Connected", 
                     "The Teensy relay controller is not connected.\n"
                     "Please ensure it is connected before running the SOCEIS experiment.");
        return 0;
    }
    
    // Initialize experiment context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
    g_experimentContext.state = SOCEIS_STATE_PREPARING;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.buttonControl = control;
    g_experimentContext.socControl = SOCEIS_NUM_SOC;
    g_experimentContext.psbHandle = psbHandle;
    g_experimentContext.biologicID = biologicID;
    g_experimentContext.graph1Handle = PANEL_GRAPH_1;
    g_experimentContext.graph2Handle = PANEL_GRAPH_2;
    g_experimentContext.graphBiologicHandle = PANEL_GRAPH_BIOLOGIC;
    
    // Read experiment parameters from UI
    GetCtrlVal(panel, SOCEIS_NUM_EIS_INTERVAL, &g_experimentContext.params.eisInterval);
    GetCtrlVal(panel, SOCEIS_NUM_CAPACITY, &g_experimentContext.params.batteryCapacity_mAh);
    GetCtrlVal(panel, SOCEIS_NUM_CURRENT_THRESHOLD, &g_experimentContext.params.currentThreshold);
    GetCtrlVal(panel, SOCEIS_NUM_INTERVAL, &g_experimentContext.params.logInterval);
    GetCtrlVal(panel, SOCEIS_CHECKBOX_DISCHARGE, &g_experimentContext.params.dischargeAfter);
    
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_experimentContext.params.chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_experimentContext.params.dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_experimentContext.params.chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_experimentContext.params.dischargeCurrent);
    
    // Check if battery capacity is valid
    if (g_experimentContext.params.batteryCapacity_mAh <= 0.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Invalid Battery Capacity", 
                     "Battery capacity must be greater than 0 mAh.\n\n"
                     "Please enter a valid battery capacity or use the\n"
                     "'Import Settings' button to load capacity from a\n"
                     "previous capacity experiment.");
        
        g_experimentContext.state = SOCEIS_STATE_CANCELLED;
        
        LogError("SOCEIS experiment aborted - battery capacity is 0");
        return 0;
    }
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Dim appropriate controls
    DimExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);
    
    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, SOCEISExperimentThread, 
                                            &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        // Failed to start thread
        g_experimentContext.state = SOCEIS_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start SOCEIS experiment thread.");
        return 0;
    }
    
    return 0;
}

int CVICALLBACK ImportSOCEISSettingsCallback(int panel, int control, int event,
                                            void *callbackData, int eventData1, 
                                            int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    char filename[MAX_PATH_LENGTH];
    int status;
    
    // Show file dialog
    status = FileSelectPopup("", "results.txt", "*.txt",
                            "Select Capacity Experiment Results File",
                            VAL_LOAD_BUTTON, 0, 0, 1, 0, filename);
    
    if (status != 1) {
        return 0;
    }
    
    // Open file
    FILE *file = fopen(filename, "r");
    if (!file) {
        MessagePopup("Error", "Failed to open results file.");
        return 0;
    }
    
    // Variables to track what was found
    int foundItems = 0;
    char missingItems[LARGE_BUFFER_SIZE] = "The following values were not found:\n";
    int hasMissing = 0;
    
    // Parse the file line by line
    char line[MEDIUM_BUFFER_SIZE];
    char currentSection[MEDIUM_BUFFER_SIZE] = "";
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Skip empty lines and comments
        if (strlen(line) == 0 || line[0] == '#') continue;
        
        // Check for section header
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strcpy(currentSection, line + 1);
            }
            continue;
        }
        
        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        // Trim whitespace
        key = TrimWhitespace(key);
        value = TrimWhitespace(value);
        
        // Check for our values
        if (strcmp(currentSection, "Charge_Results") == 0) {
            if (strcmp(key, "Charge_Capacity_mAh") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(panel, SOCEIS_NUM_CAPACITY, val);
                    foundItems++;
                }
            }
        } else if (strcmp(currentSection, "Experiment_Parameters") == 0 ||
                   strcmp(currentSection, "Test_Parameters") == 0) {
            if (strcmp(key, "Charge_Voltage_V") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, val);
                    foundItems++;
                }
            } else if (strcmp(key, "Discharge_Voltage_V") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, val);
                    foundItems++;
                }
            } else if (strcmp(key, "Charge_Current_A") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, val);
                    foundItems++;
                }
            } else if (strcmp(key, "Discharge_Current_A") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, val);
                    foundItems++;
                }
            } else if (strcmp(key, "Current_Threshold_A") == 0) {
                double val;
                if (sscanf(value, "%lf", &val) == 1) {
                    SetCtrlVal(panel, SOCEIS_NUM_CURRENT_THRESHOLD, val);
                    foundItems++;
                }
            } else if (strcmp(key, "Log_Interval_s") == 0) {
                int val;
                if (sscanf(value, "%d", &val) == 1) {
                    SetCtrlVal(panel, SOCEIS_NUM_INTERVAL, val);
                    foundItems++;
                }
            }
        }
    }
    
    fclose(file);
    
    // Check what was missing
    if (foundItems < 7) {
        hasMissing = 1;
        // Simple approach - just list expected count
        char temp[MEDIUM_BUFFER_SIZE];
        sprintf(temp, "\nFound %d of 7 expected values.\n", foundItems);
        strcat(missingItems, temp);
    }
    
    // Show results
    if (foundItems > 0) {
        char message[MEDIUM_BUFFER_SIZE];
        snprintf(message, sizeof(message), 
                 "Successfully imported %d values from:\n%s", 
                 foundItems, filename);
        
        if (hasMissing) {
            strcat(message, "\n\n");
            strcat(message, missingItems);
        }
        
        MessagePopup("Import Results", message);
        LogMessage("Imported %d settings from capacity experiment results", foundItems);
    } else {
        MessagePopup("Import Failed", 
                     "No compatible values found in the selected file.\n"
                     "Please select a valid capacity experiment results file.");
    }
    
    return 0;
}

int SOCEISExperiment_IsRunning(void) {
    return !(g_experimentContext.state == SOCEIS_STATE_IDLE ||
             g_experimentContext.state == SOCEIS_STATE_COMPLETED ||
             g_experimentContext.state == SOCEIS_STATE_ERROR ||
             g_experimentContext.state == SOCEIS_STATE_CANCELLED);
}

/******************************************************************************
 * Experiment Thread Implementation
 ******************************************************************************/

static int SOCEISExperimentThread(void *functionData) {
    SOCEISExperimentContext *ctx = (SOCEISExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting SOCEIS Experiment ===");
    
    // Record experiment start time
    ctx->experimentStartTime = Timer();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == SOCEIS_STATE_CANCELLED) {
        LogMessage("SOCEIS experiment cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    snprintf(message, sizeof(message),
        "SOCEIS Experiment Parameters:\n\n"
        "Battery Capacity: %.2f mAh\n"
        "EIS Interval: %.1f%% SOC\n"
        "Charge Voltage: %.2f V\n"
        "Discharge Voltage: %.2f V\n"
        "Charge Current: %.2f A\n"
        "Discharge Current: %.2f A\n"
        "Current Threshold: %.3f A\n"
        "Log Interval: %d seconds\n"
        "Discharge After: %s\n\n"
        "Please confirm these parameters are correct.",
        ctx->params.batteryCapacity_mAh,
        ctx->params.eisInterval,
        ctx->params.chargeVoltage,
        ctx->params.dischargeVoltage,
        ctx->params.chargeCurrent,
        ctx->params.dischargeCurrent,
        ctx->params.currentThreshold,
        ctx->params.logInterval,
        ctx->params.dischargeAfter ? "Yes" : "No");
    
    int response = ConfirmPopup("Confirm Experiment Parameters", message);
    if (!response || ctx->state == SOCEIS_STATE_CANCELLED) {
        LogMessage("SOCEIS experiment cancelled by user");
        ctx->state = SOCEIS_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create experiment directory
    result = CreateExperimentDirectory(ctx);
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        MessagePopup("Error", "Failed to create experiment directory.\nPlease check permissions.");
        ctx->state = SOCEIS_STATE_ERROR;
        goto cleanup;
    }
    
    // Initialize relay states (both OFF)
    LogMessage("Initializing relay states...");
    result = SetRelayState(SOCEIS_RELAY_PSB_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to initialize PSB relay");
        ctx->state = SOCEIS_STATE_ERROR;
        goto cleanup;
    }
    
    result = SetRelayState(SOCEIS_RELAY_BIOLOGIC_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to initialize BioLogic relay");
        ctx->state = SOCEIS_STATE_ERROR;
        goto cleanup;
    }
    
    // Calculate target SOC points
    result = CalculateTargetSOCs(ctx);
    if (result != SUCCESS) {
        LogError("Failed to calculate target SOC points");
        ctx->state = SOCEIS_STATE_ERROR;
        goto cleanup;
    }
    
    // Configure graphs
    ConfigureGraphs(ctx);
    
    // Verify battery is discharged
    result = VerifyBatteryDischarged(ctx);
    if (result != SUCCESS || ctx->state == SOCEIS_STATE_CANCELLED) {
        if (ctx->state != SOCEIS_STATE_CANCELLED) {
            ctx->state = SOCEIS_STATE_CANCELLED;
        }
        goto cleanup;
    }
    
    // Perform initial measurement (0% SOC)
    LogMessage("Performing initial EIS measurement at 0%% SOC...");
    result = PerformEISMeasurement(ctx, 0.0);
    if (result != SUCCESS || ctx->state == SOCEIS_STATE_CANCELLED) {
        if (ctx->state != SOCEIS_STATE_CANCELLED) {
            ctx->state = SOCEIS_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Run charging loop with EIS measurements
    result = RunChargingPhase(ctx);
    if (result != SUCCESS || ctx->state == SOCEIS_STATE_CANCELLED) {
        if (ctx->state != SOCEIS_STATE_CANCELLED) {
            ctx->state = SOCEIS_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Record experiment end time
    ctx->experimentEndTime = Timer();
    
    ctx->state = SOCEIS_STATE_COMPLETED;
    LogMessage("=== SOCEIS Experiment Completed ===");
    
    // Write results file
    result = WriteResultsFile(ctx);
    if (result != SUCCESS) {
        LogError("Failed to write results file");
    }
    
    // Check if we should discharge to 50%
    if (ctx->state == SOCEIS_STATE_COMPLETED && ctx->params.dischargeAfter) {
        DischargeToFiftyPercent(ctx);
    }
    
cleanup:
    // Ensure all devices are disconnected
    SetRelayState(SOCEIS_RELAY_PSB_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    SetRelayState(SOCEIS_RELAY_BIOLOGIC_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Update status based on final state
    if (ctx->state == SOCEIS_STATE_COMPLETED) {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "SOCEIS experiment completed");
    } else if (ctx->state == SOCEIS_STATE_CANCELLED) {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "SOCEIS experiment cancelled");
    } else {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "SOCEIS experiment failed");
    }
    
    // Free allocated memory
    if (ctx->measurements) {
        // Use measurementCapacity instead of measurementCount to catch any partially completed measurements
        for (int i = 0; i < ctx->measurementCapacity; i++) {
            // Only free if the measurement was actually started
            if (i < ctx->measurementCount || 
                ctx->measurements[i].ocvData || 
                ctx->measurements[i].geisData ||
                ctx->measurements[i].frequencies) {
                
                if (ctx->measurements[i].ocvData) {
                    BIO_FreeTechniqueData(ctx->measurements[i].ocvData);
                }
                if (ctx->measurements[i].geisData) {
                    BIO_FreeTechniqueData(ctx->measurements[i].geisData);
                }
                if (ctx->measurements[i].frequencies) {
                    free(ctx->measurements[i].frequencies);
                }
                if (ctx->measurements[i].zReal) {
                    free(ctx->measurements[i].zReal);
                }
                if (ctx->measurements[i].zImag) {
                    free(ctx->measurements[i].zImag);
                }
            }
        }
        free(ctx->measurements);
    }
    
    if (ctx->targetSOCs) {
        free(ctx->targetSOCs);
    }
    
    // Restore button text
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start");
    
    // Restore UI
    RestoreUI(ctx);
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    // Clear thread ID
    g_experimentThreadId = 0;
    
    return 0;
}

/******************************************************************************
 * Helper Functions Implementation
 ******************************************************************************/

static int CreateExperimentDirectory(SOCEISExperimentContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        // Fallback to current directory
        strcpy(basePath, ".");
    }
    
    // Create data directory
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, SOCEIS_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamp subdirectory
    int result = CreateTimestampedDirectory(dataPath, "soceis_experiment", 
                                          ctx->experimentDirectory, sizeof(ctx->experimentDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        return result;
    }
    
    LogMessage("Created experiment directory: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int VerifyBatteryDischarged(SOCEISExperimentContext *ctx) {
    char message[LARGE_BUFFER_SIZE];
    
    LogMessage("Verifying battery discharge state...");
    
    // Check for cancellation
    if (ctx->state == SOCEIS_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Switch to PSB for voltage measurement
    int error = SwitchToPSB(ctx);
    if (error != SUCCESS) {
        return error;
    }
    
    // Get current battery voltage
    PSB_Status status;
    error = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    
    if (error != PSB_SUCCESS) {
        LogError("Failed to read PSB status: %s", PSB_GetErrorString(error));
        return ERR_COMM_FAILED;
    }
    
    double voltageDiff = fabs(status.voltage - ctx->params.dischargeVoltage);
    
    LogMessage("Battery voltage: %.3f V, Expected: %.3f V, Difference: %.3f V", 
               status.voltage, ctx->params.dischargeVoltage, voltageDiff);
    
    if (voltageDiff > SOCEIS_VOLTAGE_MARGIN) {
        snprintf(message, sizeof(message),
            "Battery may not be fully discharged:\n\n"
            "Measured Voltage: %.3f V\n"
            "Expected Voltage: %.3f V\n"
            "Difference: %.3f V\n"
            "Error Margin: %.3f V\n\n"
            "Do you want to continue anyway?",
            status.voltage,
            ctx->params.dischargeVoltage,
            voltageDiff,
            SOCEIS_VOLTAGE_MARGIN);
        
        // Check for cancellation before showing dialog
        if (ctx->state == SOCEIS_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        
        int response = ConfirmPopup("Battery Not Fully Discharged", message);
        if (!response || ctx->state == SOCEIS_STATE_CANCELLED) {
            LogMessage("User cancelled due to battery not being fully discharged");
            return ERR_CANCELLED;
        }
    }
    
    // Disconnect PSB for safety
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);    
    SetRelayState(SOCEIS_RELAY_PSB_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    
    LogMessage("Battery discharge state verified");
    return SUCCESS;
}

static int ConfigureGraphs(SOCEISExperimentContext *ctx) {
    // Configure Graph 1 - Current vs Time
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph1Handle, "Current vs Time", "Time (s)", "Current (A)",
                   0.0, ctx->params.chargeCurrent * 1.1);
    
    // Configure Graph 2 - OCV vs SOC
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, "OCV vs SOC", "SOC (%)", "OCV (V)",
                   ctx->params.dischargeVoltage * 0.9, ctx->params.chargeVoltage * 1.1);
    
    // Set X-axis range for SOC graph to accommodate overcharge
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_BOTTOM_XAXIS, 
                       VAL_MANUAL, 0.0, 150.0);
    
    // Configure Graph Biologic - Nyquist Plot
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, "Nyquist Plot");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_XNAME, "Z' (Ohms)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_YNAME, "-Z'' (Ohms)");
    
    // Set to autoscale initially
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_LEFT_YAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    // Clear any existing plots
    ClearGraphs(ctx);
    
    return SUCCESS;
}

static int CalculateTargetSOCs(SOCEISExperimentContext *ctx) {
    // Initial allocation - start with expected targets up to 100%
    // We'll grow this dynamically if needed
    ctx->numTargetSOCs = 2;  // Start with 0% and 100%
    
    // Add intermediate points based on interval
    if (ctx->params.eisInterval > 0 && ctx->params.eisInterval < 100) {
        double soc = ctx->params.eisInterval;
        while (soc < 100.0) {
            ctx->numTargetSOCs++;
            soc += ctx->params.eisInterval;
        }
    }
    
    // Allocate with extra space for potential growth
    int initialCapacity = ctx->numTargetSOCs + 10;  // Extra space for >100% measurements
    ctx->targetSOCs = (double*)calloc(initialCapacity, sizeof(double));
    if (!ctx->targetSOCs) {
        return ERR_OUT_OF_MEMORY;
    }
    
    // Fill initial array
    int index = 0;
    ctx->targetSOCs[index++] = 0.0;  // Always start with 0%
    
    if (ctx->params.eisInterval > 0 && ctx->params.eisInterval < 100) {
        double soc = ctx->params.eisInterval;
        while (soc < 100.0 && index < ctx->numTargetSOCs - 1) {
            ctx->targetSOCs[index++] = soc;
            soc += ctx->params.eisInterval;
        }
    }
    
    ctx->targetSOCs[ctx->numTargetSOCs - 1] = 100.0;  // Always include 100%
    
    // Allocate measurement array with same extra capacity
    ctx->measurementCapacity = initialCapacity;
    ctx->measurements = (EISMeasurement*)calloc(ctx->measurementCapacity, sizeof(EISMeasurement));
    if (!ctx->measurements) {
        return ERR_OUT_OF_MEMORY;
    }
    
    LogMessage("Initial target SOC points: ");
    for (int i = 0; i < ctx->numTargetSOCs; i++) {
        LogMessage("  %.1f%%", ctx->targetSOCs[i]);
    }
    
    return SUCCESS;
}

static int AddDynamicTargetSOC(SOCEISExperimentContext *ctx, double targetSOC) {
    // Check if we need to grow the arrays
    if (ctx->numTargetSOCs >= ctx->measurementCapacity) {
        // Grow by 10 more slots
        int newCapacity = ctx->measurementCapacity + 10;
        
        // Reallocate targetSOCs array
        double *newTargetSOCs = (double*)realloc(ctx->targetSOCs, newCapacity * sizeof(double));
        if (!newTargetSOCs) {
            LogError("Failed to grow targetSOCs array");
            return ERR_OUT_OF_MEMORY;
        }
        ctx->targetSOCs = newTargetSOCs;
        
        // Reallocate measurements array
        EISMeasurement *newMeasurements = (EISMeasurement*)realloc(ctx->measurements, 
                                                                   newCapacity * sizeof(EISMeasurement));
        if (!newMeasurements) {
            LogError("Failed to grow measurements array");
            return ERR_OUT_OF_MEMORY;
        }
        ctx->measurements = newMeasurements;
        
        // Initialize new measurement slots
        for (int i = ctx->measurementCapacity; i < newCapacity; i++) {
            memset(&ctx->measurements[i], 0, sizeof(EISMeasurement));
        }
        
        ctx->measurementCapacity = newCapacity;
        LogDebug("Grew measurement arrays to capacity %d", newCapacity);
    }
    
    // Add the new target
    ctx->targetSOCs[ctx->numTargetSOCs] = targetSOC;
    ctx->numTargetSOCs++;
    
    LogMessage("Added dynamic target SOC: %.1f%%", targetSOC);
    
    return SUCCESS;
}

static int SetRelayState(int pin, int state) {
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        return ERR_NOT_CONNECTED;
    }
    
    return TNY_SetPinQueued(pin, state, DEVICE_PRIORITY_NORMAL);
}

static int SwitchToBioLogic(SOCEISExperimentContext *ctx) {
    int result;
    
    LogMessage("Switching to BioLogic...");
    
    // Disable PSB output first for safety
    LogMessage("Disabling PSB output...");
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Wait briefly for PSB to disable output
    Delay(0.5);
    
    // Disconnect PSB first
    LogMessage("Disconnecting PSB relay...");
    result = SetRelayState(SOCEIS_RELAY_PSB_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    // Wait for relay to settle
    Delay(SOCEIS_RELAY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect BioLogic
    LogMessage("Connecting BioLogic relay...");
    result = SetRelayState(SOCEIS_RELAY_BIOLOGIC_PIN, SOCEIS_RELAY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to connect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    // Wait for relay to settle
    Delay(SOCEIS_RELAY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to BioLogic");
    return SUCCESS;
}

static int SwitchToPSB(SOCEISExperimentContext *ctx) {
    int result;
    
    LogMessage("Switching to PSB...");
    
    // Disconnect BioLogic first
    LogMessage("Disconnecting BioLogic relay...");
    result = SetRelayState(SOCEIS_RELAY_BIOLOGIC_PIN, SOCEIS_RELAY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to disconnect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    // Wait for relay to settle
    Delay(SOCEIS_RELAY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect PSB
    LogMessage("Connecting PSB relay...");
    result = SetRelayState(SOCEIS_RELAY_PSB_PIN, SOCEIS_RELAY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to connect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    // Wait for relay to settle
    Delay(SOCEIS_RELAY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to PSB");
    return SUCCESS;
}

static int PerformEISMeasurement(SOCEISExperimentContext *ctx, double targetSOC) {
    int result;
    int retryCount = 0;
    
    // Check for cancellation
    if (ctx->state == SOCEIS_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Allocate measurement
    if (ctx->measurementCount >= ctx->measurementCapacity) {
        // Shouldn't happen if we calculated correctly
        LogError("Measurement array full!");
        return ERR_OPERATION_FAILED;
    }
    
    EISMeasurement *measurement = &ctx->measurements[ctx->measurementCount];
    measurement->targetSOC = targetSOC;
    measurement->actualSOC = ctx->currentSOC;
    measurement->timestamp = Timer() - ctx->experimentStartTime;
    
    // Initialize pointers to NULL for safe cleanup
    measurement->ocvData = NULL;
    measurement->geisData = NULL;
    measurement->frequencies = NULL;
    measurement->zReal = NULL;
    measurement->zImag = NULL;
    measurement->numPoints = 0;
    
    // Update UI
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), 
             "Measuring EIS at %.1f%% SOC...", ctx->currentSOC);
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, statusMsg);
    
    while (retryCount <= SOCEIS_MAX_EIS_RETRY) {
        // Switch to BioLogic
        result = SwitchToBioLogic(ctx);
        if (result != SUCCESS) {
            LogError("Failed to switch to BioLogic for EIS measurement");
            return result;
        }
        
        // Run OCV measurement
        result = RunOCVMeasurement(ctx, measurement);
        if (result != SUCCESS && retryCount < SOCEIS_MAX_EIS_RETRY) {
            LogWarning("OCV measurement failed, retrying...");
            retryCount++;
            continue;
        } else if (result != SUCCESS) {
            LogError("OCV measurement failed after retry");
            return result;
        }
        
        // Check for cancellation
        if (ctx->state == SOCEIS_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        
        // Run GEIS measurement
        result = RunGEISMeasurement(ctx, measurement);
        if (result != SUCCESS && retryCount < SOCEIS_MAX_EIS_RETRY) {
            LogWarning("GEIS measurement failed, retrying...");
            retryCount++;
            continue;
        } else if (result != SUCCESS) {
            LogError("GEIS measurement failed after retry");
            return result;
        }
        
        // If we get here, measurements succeeded
        break;
    }
    
    // Process GEIS data to extract impedance values
    result = ProcessGEISData(measurement->geisData, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to process GEIS data");
    }
    
    // Update graphs
    UpdateOCVGraph(ctx, measurement);
    UpdateNyquistPlot(ctx, measurement);
    
    // Save measurement data
    result = SaveMeasurementData(ctx, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to save measurement data");
    }
    
    ctx->measurementCount++;
    
    LogMessage("EIS measurement completed at %.1f%% SOC (OCV: %.3f V)", 
               measurement->actualSOC, measurement->ocvVoltage);
	
	// Reset last time to avoid counting the entire EIS measurement as a long charge leap
	ctx->lastTime = 0.0;
    
    return SUCCESS;
}

static int RunOCVMeasurement(SOCEISExperimentContext *ctx, EISMeasurement *measurement) {
    int result;
    
    LogDebug("Starting OCV measurement...");
    
    // Initialize the voltage to a known value
    measurement->ocvVoltage = 0.0;
    
    // Run OCV
    result = BIO_RunOCVQueued(ctx->biologicID, 0,  // channel 0
                            SOCEIS_OCV_DURATION_S,
                            SOCEIS_OCV_SAMPLE_INTERVAL_S,
                            SOCEIS_OCV_RECORD_EVERY_DE,
                            SOCEIS_OCV_RECORD_EVERY_DT,
                            SOCEIS_OCV_E_RANGE,
                            true,  // process data
                            &measurement->ocvData,
                            SOCEIS_OCV_TIMEOUT_MS,
                            DEVICE_PRIORITY_NORMAL,
                            NULL, NULL);
    
    if (result != SUCCESS) {
        LogError("OCV measurement failed: %s (error code: %d)", BIO_GetErrorString(result), result);
        
        // Try to stop the channel if it's stuck
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
        Delay(0.5);
        
        return result;
    }
    
    // Extract final voltage
    if (measurement->ocvData && measurement->ocvData->convertedData) {
        BIO_ConvertedData *convData = measurement->ocvData->convertedData;
        
        LogDebug("OCV data: numPoints=%d, numVariables=%d", 
                 convData->numPoints, convData->numVariables);
        
        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            // Get the last voltage value
            int lastPoint = convData->numPoints - 1;
            measurement->ocvVoltage = convData->data[1][lastPoint];  // Ewe is at index 1
            
            LogDebug("OCV measurement complete: %.3f V", measurement->ocvVoltage);
        } else {
            LogWarning("OCV data incomplete");
        }
    } else {
        LogWarning("No OCV data received from BioLogic");
    }
    
    return SUCCESS;
}

static int RunGEISMeasurement(SOCEISExperimentContext *ctx, EISMeasurement *measurement) {
    int result;
    
    LogDebug("Starting GEIS measurement...");
    
    // Run GEIS
    result = BIO_RunGEISQueued(ctx->biologicID, 0,  // channel 0
                             SOCEIS_GEIS_VS_INITIAL,
                             SOCEIS_GEIS_INITIAL_CURRENT,
                             SOCEIS_GEIS_DURATION_S,
                             SOCEIS_GEIS_RECORD_EVERY_DT,
                             SOCEIS_GEIS_RECORD_EVERY_DE,
                             SOCEIS_GEIS_INITIAL_FREQ,
                             SOCEIS_GEIS_FINAL_FREQ,
                             SOCEIS_GEIS_SWEEP_LINEAR,
                             SOCEIS_GEIS_AMPLITUDE_I,
                             SOCEIS_GEIS_FREQ_NUMBER,
                             SOCEIS_GEIS_AVERAGE_N,
                             SOCEIS_GEIS_CORRECTION,
                             SOCEIS_GEIS_WAIT_FOR_STEADY,
                             SOCEIS_GEIS_I_RANGE,
                             true,  // process data
                             &measurement->geisData,
                             SOCEIS_GEIS_TIMEOUT_MS,
                             DEVICE_PRIORITY_NORMAL,
                             NULL, NULL);
    
    if (result != SUCCESS) {
        LogError("GEIS measurement failed: %d %s", result, GetErrorString(result));
        return result;
    }
    
    LogDebug("GEIS measurement complete");
    return SUCCESS;
}

static int ProcessGEISData(BIO_TechniqueData *geisData, EISMeasurement *measurement) {
    if (!geisData) {
        LogWarning("No GEIS data available");
        return ERR_INVALID_PARAMETER;
    }
    
    // Check if we have converted data
    if (!geisData->convertedData) {
        LogWarning("No converted GEIS data available");
        return ERR_OPERATION_FAILED;
    }
    
    BIO_ConvertedData *convData = geisData->convertedData;
    
    // Check process index from raw data
    int processIndex = -1;
    if (geisData->rawData) {
        processIndex = geisData->rawData->processIndex;
    }
    
    LogDebug("Processing GEIS data: %d points, %d variables (process %d)", 
             convData->numPoints, convData->numVariables, processIndex);
    
    // Debug: print all variable names if available
    if (convData->variableNames) {
        for (int i = 0; i < convData->numVariables; i++) {
            if (convData->variableNames[i]) {
                LogDebug("  Variable %d: %s", i, convData->variableNames[i]);
            }
        }
    }
    
    // GEIS process 1 should have 11 variables:
    // 0: Frequency, 1: |Ewe|, 2: |I|, 3: Phase_Zwe, 4: Re(Zwe), 5: Im(Zwe)
    // 6: Ewe, 7: I, 8: |Ece|, 9: |Ice|, 10: Time
    if (processIndex == 1 && convData->numVariables >= 11) {
        // This is the impedance data we want
        
        // Allocate arrays for impedance data
        measurement->frequencies = (double*)calloc(convData->numPoints, sizeof(double));
        measurement->zReal = (double*)calloc(convData->numPoints, sizeof(double));
        measurement->zImag = (double*)calloc(convData->numPoints, sizeof(double));
        
        if (!measurement->frequencies || !measurement->zReal || !measurement->zImag) {
            LogError("Failed to allocate impedance arrays");
            if (measurement->frequencies) free(measurement->frequencies);
            if (measurement->zReal) free(measurement->zReal);
            if (measurement->zImag) free(measurement->zImag);
            measurement->frequencies = NULL;
            measurement->zReal = NULL;
            measurement->zImag = NULL;
            return ERR_OUT_OF_MEMORY;
        }
        
        // Copy the data - it's already processed!
        for (int i = 0; i < convData->numPoints; i++) {
            measurement->frequencies[i] = convData->data[0][i];  // Frequency
            measurement->zReal[i] = convData->data[4][i];       // Re(Zwe)
            measurement->zImag[i] = convData->data[5][i];       // Im(Zwe)
            
            // Log first few points for debugging
            if (i < 3) {
                LogDebug("Point %d: f=%.1f Hz, Z=(%.3f, %.3f) Ohm",
                         i, measurement->frequencies[i], 
                         measurement->zReal[i], measurement->zImag[i]);
            }
        }
        
        measurement->numPoints = convData->numPoints;
        
        LogDebug("Successfully extracted %d impedance points from GEIS data", 
                 measurement->numPoints);
        
    } else if (processIndex == 0 && convData->numVariables >= 3) {
        // This is stabilization data, not what we want
        LogWarning("Received GEIS process 0 (stabilization) data instead of process 1 (impedance) data");
        LogWarning("This indicates the impedance sweep may not have completed properly");
        return ERR_OPERATION_FAILED;
        
    } else {
        // Unexpected data format
        LogWarning("Unexpected GEIS data format: process %d with %d variables", 
                  processIndex, convData->numVariables);
        LogWarning("Expected process 1 with 11 variables for impedance data");
        return ERR_OPERATION_FAILED;
    }
    
    return SUCCESS;
}

static int SaveMeasurementData(SOCEISExperimentContext *ctx, EISMeasurement *measurement) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    // Create filename based on SOC
    snprintf(filename, sizeof(filename), "%s%s%s%02d.txt", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             SOCEIS_DETAILS_FILE_PREFIX, 
             (int)(measurement->actualSOC + 0.5));
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create measurement file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    // Write measurement information
    WriteINISection(file, "Measurement_Information");
    
    // Get timestamp
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    WriteINIValue(file, "Timestamp", "%s", timeStr);
    
    WriteINIDouble(file, "Target_SOC_Percent", measurement->targetSOC, 1);
    WriteINIDouble(file, "Actual_SOC_Percent", measurement->actualSOC, 1);
    WriteINIDouble(file, "Elapsed_Time_s", measurement->timestamp, 1);
    WriteINIDouble(file, "Battery_Voltage_V", measurement->ocvVoltage, 3);
    fprintf(file, "\n");
    
    // Write OCV parameters
    WriteINISection(file, "OCV_Parameters");
    WriteINIDouble(file, "Duration_s", SOCEIS_OCV_DURATION_S, 1);
    WriteINIDouble(file, "Sample_Interval_s", SOCEIS_OCV_SAMPLE_INTERVAL_S, 1);
    WriteINIDouble(file, "Record_Every_dE_mV", SOCEIS_OCV_RECORD_EVERY_DE, 1);
    WriteINIDouble(file, "Record_Every_dT_s", SOCEIS_OCV_RECORD_EVERY_DT, 1);
    fprintf(file, "\n");
    
    // Write OCV results
    WriteINISection(file, "OCV_Results");
    WriteINIDouble(file, "Final_Voltage_V", measurement->ocvVoltage, 3);
    if (measurement->ocvData && measurement->ocvData->convertedData) {
        WriteINIValue(file, "Data_Points", "%d", measurement->ocvData->convertedData->numPoints);
    }
    fprintf(file, "\n");
    
    // Write GEIS parameters
    WriteINISection(file, "GEIS_Parameters");
    WriteINIDouble(file, "Initial_Current_A", SOCEIS_GEIS_INITIAL_CURRENT, 3);
    WriteINIDouble(file, "Duration_s", SOCEIS_GEIS_DURATION_S, 1);
    WriteINIDouble(file, "Initial_Freq_Hz", SOCEIS_GEIS_INITIAL_FREQ, 0);
    WriteINIDouble(file, "Final_Freq_Hz", SOCEIS_GEIS_FINAL_FREQ, 0);
    WriteINIDouble(file, "Amplitude_mA", SOCEIS_GEIS_AMPLITUDE_I * 1000, 0);
    WriteINIValue(file, "Frequency_Count", "%d", SOCEIS_GEIS_FREQ_NUMBER);
    fprintf(file, "\n");
    
    // Write GEIS results
    WriteINISection(file, "GEIS_Results");
    WriteINIValue(file, "Data_Points", "%d", measurement->numPoints);
    fprintf(file, "\n");
    
    // Write impedance data table
    if (measurement->numPoints > 0) {
        fprintf(file, "[Impedance_Data]\n");
        fprintf(file, "Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Mag_Ohm,Phase_Deg\n");
        
        for (int i = 0; i < measurement->numPoints; i++) {
            double magnitude = sqrt(measurement->zReal[i] * measurement->zReal[i] + 
                                  measurement->zImag[i] * measurement->zImag[i]);
            double phase = atan2(measurement->zImag[i], measurement->zReal[i]) * 180.0 / M_PI;
            
            fprintf(file, "%.1f,%.6f,%.6f,%.6f,%.2f\n",
                    measurement->frequencies[i],
                    measurement->zReal[i],
                    measurement->zImag[i],
                    magnitude,
                    phase);
        }
    }
    
    fclose(file);
    
    LogDebug("Saved measurement data to: %s", filename);
    return SUCCESS;
}

static int RunChargingPhase(SOCEISExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    int result;
    int nextTargetIndex = 1;  // Skip 0% as we already measured it
    int dynamicTargetsAdded = 0;  // Track if we've gone beyond 100%
    
    LogMessage("Starting charging phase...");
    
    // Create charge log file
    snprintf(filename, sizeof(filename), "%s%scharge.csv", 
             ctx->experimentDirectory, PATH_SEPARATOR);
    
    ctx->currentLogFile = fopen(filename, "w");
    if (!ctx->currentLogFile) {
        LogError("Failed to create charge log file");
        return ERR_BASE_FILE;
    }
    
    // Write CSV header
    fprintf(ctx->currentLogFile, "Time_s,Voltage_V,Current_A,Power_W,SOC_Percent\n");
    
    // Switch to PSB with proper logging
    LogMessage("Switching to PSB for charging...");
    result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to PSB for charging");
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Verify PSB connection with a status read
    PSB_Status preChargeStatus;
    result = PSB_GetStatusQueued(&preChargeStatus, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to read PSB status before charging: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    LogMessage("Pre-charge battery voltage: %.3f V", preChargeStatus.voltage);
    
    // Set charging parameters
    LogMessage("Setting charge parameters: %.2f V, %.2f A", 
               ctx->params.chargeVoltage, ctx->params.chargeCurrent);
    
    result = PSB_SetVoltageQueued(ctx->params.chargeVoltage, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set charge voltage: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    result = PSB_SetCurrentQueued(ctx->params.chargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set charge current: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    result = PSB_SetPowerQueued(SOCEIS_MAX_POWER, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set power value: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Enable PSB output
    LogMessage("Enabling PSB output...");
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Wait for PSB to establish output before checking current
    LogMessage("Waiting for PSB output to stabilize...");
    Delay(2.0);  // Give PSB time to establish output
    
    // Verify PSB is actually outputting
    PSB_Status initialStatus;
    result = PSB_GetStatusQueued(&initialStatus, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to read initial PSB status: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    LogMessage("Initial charging status - Voltage: %.3f V, Current: %.3f A, Power: %.3f W",
               initialStatus.voltage, initialStatus.current, initialStatus.power);
    
    // Check if current is actually flowing
    if (fabs(initialStatus.current) < 0.01) {  // Less than 10mA suggests no connection
        LogWarning("Warning: Very low current detected (%.3f A) - possible connection issue", 
                   initialStatus.current);
        LogWarning("Continuing anyway, but check relay connections...");
    }
    
    LogMessage("Charging started - monitoring current threshold: %.3f A", ctx->params.currentThreshold);
    
    // Initialize timing and SOC tracking
    ctx->phaseStartTime = Timer();
    ctx->lastLogTime = ctx->phaseStartTime;
    ctx->lastGraphUpdate = ctx->phaseStartTime;
    ctx->currentSOC = 0.0;
    ctx->accumulatedCapacity_mAh = 0.0;
    ctx->lastCurrent = 0.0;
    ctx->lastTime = 0.0;
    
    // Update UI state
    ctx->state = SOCEIS_STATE_CHARGING;
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Charging battery...");
    
    // Track consecutive low current readings to avoid premature termination
    int lowCurrentReadings = 0;
    const int MIN_LOW_CURRENT_READINGS = 5;  // Require 5 consecutive readings below threshold
    
    // Main charging loop - continue until current threshold
    while (1) {
        // Check for cancellation
        if (ctx->state == SOCEIS_STATE_CANCELLED) {
            LogMessage("Charging phase cancelled by user");
            break;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - ctx->phaseStartTime;
        
        // Get current status
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        
        if (result != PSB_SUCCESS) {
            LogError("Failed to read status: %s", PSB_GetErrorString(result));
            break;
        }
        
        // Update SOC tracking (no clamping to 100%)
        UpdateSOCTracking(ctx, status.voltage, status.current);
        
        // Log data if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            fprintf(ctx->currentLogFile, "%.3f,%.3f,%.3f,%.3f,%.2f\n", 
                    elapsedTime, status.voltage, status.current, status.power, ctx->currentSOC);
            fflush(ctx->currentLogFile);
            ctx->lastLogTime = currentTime;
            
            // Update SOC display
            SetCtrlVal(ctx->tabPanelHandle, ctx->socControl, ctx->currentSOC);
            
            // Periodic status logging
            if ((int)elapsedTime % 60 == 0 && elapsedTime > 0) {  // Every minute
                LogMessage("Charging progress - Time: %.1f min, SOC: %.1f%%, Current: %.3f A",
                          elapsedTime / 60.0, ctx->currentSOC, status.current);
            }
        }
        
        // Update graph if needed
        if ((currentTime - ctx->lastGraphUpdate) >= 1.0) {  // Update every second
            UpdateGraphs(ctx, status.current, elapsedTime);
            ctx->lastGraphUpdate = currentTime;
        }
        
        // Check if we need to perform EIS measurement
        if (nextTargetIndex < ctx->numTargetSOCs && 
            ctx->currentSOC >= ctx->targetSOCs[nextTargetIndex]) {
            
            LogMessage("Target SOC %.1f%% reached (actual: %.1f%%)", 
                      ctx->targetSOCs[nextTargetIndex], ctx->currentSOC);
            
            // Disable PSB output
            PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
            
            // Perform EIS measurement
            ctx->state = SOCEIS_STATE_MEASURING_EIS;
            result = PerformEISMeasurement(ctx, ctx->targetSOCs[nextTargetIndex]);
            
            if (result != SUCCESS || ctx->state == SOCEIS_STATE_CANCELLED) {
                LogError("EIS measurement failed at %.1f%% SOC", ctx->currentSOC);
                break;
            }
            
            nextTargetIndex++;
            
            // Check if we've reached the last pre-calculated target
            if (nextTargetIndex >= ctx->numTargetSOCs) {
                // Add a new dynamic target if SOC will continue rising
                if (ctx->params.eisInterval > 0) {
                    double nextTarget = ctx->targetSOCs[ctx->numTargetSOCs - 1] + ctx->params.eisInterval;
                    result = AddDynamicTargetSOC(ctx, nextTarget);
                    if (result != SUCCESS) {
                        LogError("Failed to add dynamic target SOC");
                        // Continue anyway, just won't take more measurements
                    } else {
                        dynamicTargetsAdded++;
                        if (dynamicTargetsAdded == 1) {
                            LogMessage("Battery capacity appears to be underestimated - continuing measurements beyond 100%%");
                        }
                    }
                }
            }
            
            // Resume charging
            ctx->state = SOCEIS_STATE_CHARGING;
            SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Charging battery...");
            
            LogMessage("Resuming charging after EIS measurement...");
            result = SwitchToPSB(ctx);
            if (result != SUCCESS) break;
            
            // Re-enable PSB output and wait for stabilization
            PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
            Delay(1.0);  // Give PSB time to re-establish output
            
            // Reset low current counter after resuming
            lowCurrentReadings = 0;
        }
        
        // Check current threshold for charge completion with debouncing
        if (fabs(status.current) < ctx->params.currentThreshold) {
            lowCurrentReadings++;
            LogDebug("Low current reading %d/%d: %.3f A < %.3f A", 
                    lowCurrentReadings, MIN_LOW_CURRENT_READINGS,
                    fabs(status.current), ctx->params.currentThreshold);
            
            if (lowCurrentReadings >= MIN_LOW_CURRENT_READINGS) {
                LogMessage("Charging completed - current below threshold for %d consecutive readings", 
                          MIN_LOW_CURRENT_READINGS);
                LogMessage("Final current: %.3f A < %.3f A", fabs(status.current), ctx->params.currentThreshold);
                LogMessage("Final SOC: %.1f%%", ctx->currentSOC);
                
                // Perform final measurement at actual final SOC
                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                
                // Add final measurement if not already at a target
                int needFinalMeasurement = 1;
                if (nextTargetIndex > 0 && nextTargetIndex <= ctx->numTargetSOCs) {
                    double lastMeasuredSOC = ctx->measurements[ctx->measurementCount - 1].actualSOC;
                    if (fabs(ctx->currentSOC - lastMeasuredSOC) < 1.0) {
                        needFinalMeasurement = 0;  // Already have a measurement very close
                    }
                }
                
                if (needFinalMeasurement) {
                    LogMessage("Taking final EIS measurement at %.1f%% SOC", ctx->currentSOC);
                    ctx->state = SOCEIS_STATE_MEASURING_EIS;
                    
                    // Add this as a dynamic target
                    result = AddDynamicTargetSOC(ctx, ctx->currentSOC);
                    if (result == SUCCESS) {
                        result = PerformEISMeasurement(ctx, ctx->currentSOC);
                    }
                }
                
                break;
            }
        } else {
            // Reset counter if current is above threshold
            if (lowCurrentReadings > 0) {
                LogDebug("Current above threshold, resetting low current counter");
                lowCurrentReadings = 0;
            }
        }
        
        // Safety check - if charging takes too long, abort
        if (elapsedTime > SOCEIS_TIMEOUT_SEC) {
            LogError("Charging timeout - aborting after %.1f hours", elapsedTime / 3600.0);
            ctx->state = SOCEIS_STATE_ERROR;
            break;
        }
        
        // Process events and delay
        ProcessSystemEvents();
        Delay(0.5);  // Increased from 0.1s to reduce CPU usage and improve stability
    }
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Close log file
    fclose(ctx->currentLogFile);
    ctx->currentLogFile = NULL;
    
    LogMessage("Charging phase completed");
    if (dynamicTargetsAdded > 0) {
        LogMessage("Note: Battery capacity was underestimated - took %d measurements beyond 100%% SOC", 
                  dynamicTargetsAdded);
    }
    
    return (ctx->state == SOCEIS_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static int UpdateSOCTracking(SOCEISExperimentContext *ctx, double voltage, double current) {
    double currentTime = Timer() - ctx->phaseStartTime;
    
    if (ctx->lastTime > 0) {
        double deltaTime = currentTime - ctx->lastTime;
        
        // Capacity increment using battery_utils function
        double capacityIncrement = Battery_CalculateCapacityIncrement(
            fabs(ctx->lastCurrent), fabs(current), deltaTime);
        
        ctx->accumulatedCapacity_mAh += capacityIncrement;
        
        // Update SOC
        if (ctx->params.batteryCapacity_mAh > 0) {
            ctx->currentSOC = (ctx->accumulatedCapacity_mAh / ctx->params.batteryCapacity_mAh) * 100.0;
            
            // Only clamp to minimum 0%
            if (ctx->currentSOC < 0.0) {
                ctx->currentSOC = 0.0;
            }
        }
    }
    
    ctx->lastCurrent = current;
    ctx->lastTime = currentTime;
    
    return SUCCESS;
}

static void UpdateGraphs(SOCEISExperimentContext *ctx, double current, double time) {
    // Plot current point on graph 1
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph1Handle, 
                  time, fabs(current), VAL_SOLID_CIRCLE, VAL_RED);
}

static void UpdateOCVGraph(SOCEISExperimentContext *ctx, EISMeasurement *measurement) {
    // Plot OCV vs SOC point
    ctx->ocvPlotHandle = PlotPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
                                   measurement->actualSOC, measurement->ocvVoltage, 
                                   VAL_SOLID_CIRCLE, VAL_BLUE);
    
    // Connect points if we have more than one
    if (ctx->measurementCount > 1) {
        double *socArray = (double*)calloc(ctx->measurementCount, sizeof(double));
        double *ocvArray = (double*)calloc(ctx->measurementCount, sizeof(double));
        
        if (socArray && ocvArray) {
            for (int i = 0; i < ctx->measurementCount; i++) {
                socArray[i] = ctx->measurements[i].actualSOC;
                ocvArray[i] = ctx->measurements[i].ocvVoltage;
            }
            
            PlotXY(ctx->mainPanelHandle, ctx->graph2Handle,
                   socArray, ocvArray, ctx->measurementCount,
                   VAL_DOUBLE, VAL_DOUBLE, VAL_THIN_LINE,
                   VAL_NO_POINT, VAL_SOLID, 1, VAL_BLUE);
            
            free(socArray);
            free(ocvArray);
        }
    }
}

static void UpdateNyquistPlot(SOCEISExperimentContext *ctx, EISMeasurement *measurement) {
    if (measurement->numPoints == 0) return;
    
    // Clear previous Nyquist plot
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphBiologicHandle, -1, VAL_DELAYED_DRAW);
    
    // Prepare data for plotting (negate imaginary part for convention)
    double *negZImag = (double*)calloc(measurement->numPoints, sizeof(double));
    if (!negZImag) return;
    
    for (int i = 0; i < measurement->numPoints; i++) {
        negZImag[i] = -measurement->zImag[i];
    }
    
    // Plot Nyquist data
    ctx->nyquistPlotHandle = PlotXY(ctx->mainPanelHandle, ctx->graphBiologicHandle,
                                    measurement->zReal, negZImag, measurement->numPoints,
                                    VAL_DOUBLE, VAL_DOUBLE, VAL_SCATTER,
                                    VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_GREEN);
    
    // Add title with SOC
    char title[MEDIUM_BUFFER_SIZE];
    snprintf(title, sizeof(title), "Nyquist Plot - SOC: %.1f%%", measurement->actualSOC);
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, title);
    
    free(negZImag);
}

static int WriteResultsFile(SOCEISExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, SOCEIS_RESULTS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create results file");
        return ERR_BASE_FILE;
    }
    
    // Get timestamp for experiment start
    time_t startTime = (time_t)ctx->experimentStartTime;
    char startTimeStr[64];
    FormatTimestamp(startTime, startTimeStr, sizeof(startTimeStr));
    
    // Get timestamp for experiment end
    time_t endTime = (time_t)ctx->experimentEndTime;
    char endTimeStr[64];
    FormatTimestamp(endTime, endTimeStr, sizeof(endTimeStr));
    
    // Write header
    fprintf(file, "# SOCEIS Experiment Summary\n");
    fprintf(file, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
    
    // Experiment Information
    WriteINISection(file, "Experiment_Information");
    WriteINIValue(file, "Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_h", (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0, 2);
    WriteINIDouble(file, "Battery_Capacity_mAh", ctx->params.batteryCapacity_mAh, 1);
    WriteINIDouble(file, "EIS_Interval_Percent", ctx->params.eisInterval, 1);
    fprintf(file, "\n");
    
    // Experiment Parameters
    WriteINISection(file, "Experiment_Parameters");
    WriteINIDouble(file, "Charge_Voltage_V", ctx->params.chargeVoltage, 3);
    WriteINIDouble(file, "Discharge_Voltage_V", ctx->params.dischargeVoltage, 3);
    WriteINIDouble(file, "Charge_Current_A", ctx->params.chargeCurrent, 3);
    WriteINIDouble(file, "Discharge_Current_A", ctx->params.dischargeCurrent, 3);
    WriteINIDouble(file, "Current_Threshold_A", ctx->params.currentThreshold, 3);
    fprintf(file, "\n");
    
    // Measurements summary
    WriteINISection(file, "Measurements");
    WriteINIValue(file, "Total_Measurements", "%d", ctx->measurementCount);
    
    // Write SOC points
    fprintf(file, "SOC_Points=");
    for (int i = 0; i < ctx->measurementCount; i++) {
        fprintf(file, "%.1f", ctx->measurements[i].actualSOC);
        if (i < ctx->measurementCount - 1) fprintf(file, ",");
    }
    fprintf(file, "\n\n");
    
    // Impedance Summary table
    WriteINISection(file, "Impedance_Summary");
    fprintf(file, "# SOC_%%,OCV_V,Z_100kHz_Ohm,Z_10Hz_Ohm\n");
    
    for (int i = 0; i < ctx->measurementCount; i++) {
        EISMeasurement *m = &ctx->measurements[i];
        
        // Find impedances at specific frequencies
        double z100kHz_real = 0, z100kHz_imag = 0;
        double z10Hz_real = 0, z10Hz_imag = 0;
        
        if (m->numPoints > 0) {
            // Assuming first point is highest frequency (100kHz)
            z100kHz_real = m->zReal[0];
            z100kHz_imag = m->zImag[0];
            
            // Assuming last point is lowest frequency (10Hz)
            z10Hz_real = m->zReal[m->numPoints - 1];
            z10Hz_imag = m->zImag[m->numPoints - 1];
        }
        
        double z100kHz_mag = sqrt(z100kHz_real*z100kHz_real + z100kHz_imag*z100kHz_imag);
        double z10Hz_mag = sqrt(z10Hz_real*z10Hz_real + z10Hz_imag*z10Hz_imag);
        
        fprintf(file, "%.1f,%.3f,%.6f,%.6f\n", 
                m->actualSOC, m->ocvVoltage, z100kHz_mag, z10Hz_mag);
    }
    
    fclose(file);
    
    LogMessage("Results written to: %s", filename);
    return SUCCESS;
}

static void RestoreUI(SOCEISExperimentContext *ctx) {
    // Re-enable controls
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
}

static void ClearGraphs(SOCEISExperimentContext *ctx) {
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle, ctx->graphBiologicHandle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 3);
}

static int DischargeToFiftyPercent(SOCEISExperimentContext *ctx) {
    if (ctx->params.batteryCapacity_mAh <= 0) {
        LogWarning("Cannot discharge to 50%% - battery capacity unknown");
        return ERR_INVALID_PARAMETER;
    }
    
    LogMessage("=== Discharging battery to 50%% capacity ===");
    LogMessage("Target discharge: %.2f mAh", ctx->params.batteryCapacity_mAh * 0.5);
    
    // Update UI
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Discharging to 50% capacity...");
    
    // Switch to PSB
    int result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to PSB for discharge");
        return result;
    }
    
    // Configure discharge parameters
    CapacityTransferParams discharge50 = {
        .mode = BATTERY_MODE_DISCHARGE,
        .targetCapacity_mAh = ctx->params.batteryCapacity_mAh * 0.5,
        .current_A = ctx->params.dischargeCurrent,
        .voltage_V = ctx->params.dischargeVoltage,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,
        .updateIntervalMs = 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .progressCallback = NULL,
        .statusCallback = NULL,
        .cancelFlag = NULL
    };
    
    // Perform the discharge
    int dischargeResult = Battery_TransferCapacity(&discharge50);
    
    if (dischargeResult == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
        LogMessage("Successfully discharged battery to 50%% capacity");
        LogMessage("  Discharged: %.2f mAh", discharge50.actualTransferred_mAh);
        LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
        LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
        
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, 
                  "SOCEIS completed - battery at 50% capacity");
    } else {
        LogWarning("Failed to discharge to 50%% capacity");
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, 
                  "SOCEIS completed - discharge to 50% failed");
    }
    
    return dischargeResult;
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void SOCEISExperiment_Cleanup(void) {
    if (SOCEISExperiment_IsRunning()) {
        SOCEISExperiment_Abort();
    }
}

int SOCEISExperiment_Abort(void) {
    if (SOCEISExperiment_IsRunning()) {
        g_experimentContext.state = SOCEIS_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}