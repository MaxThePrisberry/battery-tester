/******************************************************************************
 * exp_baseline.c
 * 
 * Baseline Battery Experiment Module Implementation
 * Comprehensive experiment combining discharge, capacity testing, EIS, and temperature control
 ******************************************************************************/

#include "common.h"
#include "exp_baseline.h"
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
static BaselineExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

// Controls to be dimmed during experiment
static const int numControls = 4;
static const int controls[4] = {
    BASELINE_NUM_CURRENT_THRESHOLD,
    BASELINE_NUM_INTERVAL,
    BASELINE_NUM_EIS_INTERVAL,
    BASELINE_NUM_TEMPERATURE
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int BaselineExperimentThread(void *functionData);
static int VerifyAllDevices(BaselineExperimentContext *ctx);
static int CreateExperimentDirectory(BaselineExperimentContext *ctx);
static int ConfigureGraphs(BaselineExperimentContext *ctx);

// Phase functions
static int RunPhase1_DischargeAndTemp(BaselineExperimentContext *ctx);
static int RunPhase2_CapacityExperiment(BaselineExperimentContext *ctx);
static int RunPhase3_EISCharge(BaselineExperimentContext *ctx);
static int RunPhase4_Discharge50Percent(BaselineExperimentContext *ctx);

// Device control functions
static int SetRelayState(int pin, int state);
static int SwitchToPSB(BaselineExperimentContext *ctx);
static int SwitchToBioLogic(BaselineExperimentContext *ctx);
static int WaitForTargetTemperature(BaselineExperimentContext *ctx);

// EIS functions
static int CalculateTargetSOCs(BaselineExperimentContext *ctx);
static int PerformEISMeasurement(BaselineExperimentContext *ctx, double targetSOC);
static int RunOCVMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static int RunGEISMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static int ProcessGEISData(BIO_TechniqueData *geisData, BaselineEISMeasurement *measurement);
static int SaveEISMeasurementData(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);

// Data logging and tracking
static int ReadTemperatures(TemperatureDataPoint *tempData, double timestamp);
static int LogDataPoint(BaselineExperimentContext *ctx, BaselineDataPoint *point);
static int UpdateSOCTracking(BaselineExperimentContext *ctx, double voltage, double current);

// Graph functions
static void UpdateGraphs(BaselineExperimentContext *ctx, double current, double time);
static void UpdateOCVGraph(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static void UpdateNyquistPlot(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static void ClearGraphs(BaselineExperimentContext *ctx);

// Results and cleanup
static int WriteResultsFile(BaselineExperimentContext *ctx);
static void RestoreUI(BaselineExperimentContext *ctx);
static void CleanupAllDevices(BaselineExperimentContext *ctx);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartBaselineExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if baseline experiment is already running
    if (BaselineExperiment_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop baseline experiment");
		g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = BASELINE_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the baseline experiment.");
        return 0;
    }
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);
    
    // Initialize experiment context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
	g_experimentContext.cancelRequested = 0;
    g_experimentContext.state = BASELINE_STATE_PREPARING;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.buttonControl = control;
    g_experimentContext.outputControl = BASELINE_NUM_OUTPUT;
    g_experimentContext.statusControl = BASELINE_STR_BASELINE_STATUS;
    g_experimentContext.graph1Handle = PANEL_GRAPH_1;
    g_experimentContext.graph2Handle = PANEL_GRAPH_2;
    g_experimentContext.graphBiologicHandle = PANEL_GRAPH_BIOLOGIC;
    g_experimentContext.dtbSlaveAddress = DTB1_SLAVE_ADDRESS;
    
    // Read experiment parameters from UI
    GetCtrlVal(panel, BASELINE_NUM_TEMPERATURE, &g_experimentContext.params.targetTemperature);
    GetCtrlVal(panel, BASELINE_NUM_EIS_INTERVAL, &g_experimentContext.params.eisInterval);
    GetCtrlVal(panel, BASELINE_NUM_CURRENT_THRESHOLD, &g_experimentContext.params.currentThreshold);
    GetCtrlVal(panel, BASELINE_NUM_INTERVAL, &g_experimentContext.params.logInterval);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_experimentContext.params.chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_experimentContext.params.dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_experimentContext.params.chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_experimentContext.params.dischargeCurrent);
    
    // Verify all required devices are connected
    int result = VerifyAllDevices(&g_experimentContext);
    if (result != SUCCESS) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        g_experimentContext.state = BASELINE_STATE_ERROR;
        return 0;
    }
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Dim appropriate controls
    DimExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);
    
    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, BaselineExperimentThread, 
                                            &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        // Failed to start thread
        g_experimentContext.state = BASELINE_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start baseline experiment thread.");
        return 0;
    }
    
    return 0;
}

int BaselineExperiment_IsRunning(void) {
    return !(g_experimentContext.state == BASELINE_STATE_IDLE ||
             g_experimentContext.state == BASELINE_STATE_COMPLETED ||
             g_experimentContext.state == BASELINE_STATE_ERROR ||
             g_experimentContext.state == BASELINE_STATE_CANCELLED);
}

/******************************************************************************
 * Experiment Thread Implementation
 ******************************************************************************/

static int BaselineExperimentThread(void *functionData) {
    BaselineExperimentContext *ctx = (BaselineExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting Baseline Experiment ===");
    
    // Record experiment start time
    ctx->experimentStartTime = Timer();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == BASELINE_STATE_CANCELLED) {
        LogMessage("Baseline experiment cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    snprintf(message, sizeof(message),
        "Baseline Experiment Parameters:\n\n"
        "Target Temperature: %.1f °C\n"
        "EIS Interval: %.1f%% SOC\n"
        "Charge Voltage: %.2f V\n"
        "Discharge Voltage: %.2f V\n"
        "Charge Current: %.2f A\n"
        "Discharge Current: %.2f A\n"
        "Current Threshold: %.3f A\n"
        "Log Interval: %d seconds\n\n"
        "This experiment will run 4 phases:\n"
        "1. Discharge battery and set temperature\n"
        "2. Capacity experiment (charge ? discharge)\n"
        "3. EIS measurements during charge\n"
        "4. Discharge to 50%% capacity\n\n"
        "Please confirm these parameters are correct.",
        ctx->params.targetTemperature,
        ctx->params.eisInterval,
        ctx->params.chargeVoltage,
        ctx->params.dischargeVoltage,
        ctx->params.chargeCurrent,
        ctx->params.dischargeCurrent,
        ctx->params.currentThreshold,
        ctx->params.logInterval);
    
    int response = ConfirmPopup("Confirm Baseline Experiment Parameters", message);
    if (!response || ctx->state == BASELINE_STATE_CANCELLED) {
        LogMessage("Baseline experiment cancelled by user");
        ctx->state = BASELINE_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create experiment directory
    result = CreateExperimentDirectory(ctx);
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        MessagePopup("Error", "Failed to create experiment directory.\nPlease check permissions.");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    // Initialize relay states (both OFF)
    LogMessage("Initializing relay states...");
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to initialize PSB relay");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to initialize BioLogic relay");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    // Configure graphs
    ConfigureGraphs(ctx);
    
    // Phase 1: Discharge and Temperature Setup
    LogMessage("=== Phase 1: Initial Discharge and Temperature Setup ===");
    ctx->currentPhase = BASELINE_PHASE_1;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 1: Discharging and setting temperature...");
    
    result = RunPhase1_DischargeAndTemp(ctx);
    if (result != SUCCESS || ctx->state == BASELINE_STATE_CANCELLED) {
        if (ctx->state != BASELINE_STATE_CANCELLED) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Phase 2: Capacity Experiment
    LogMessage("=== Phase 2: Capacity Experiment (Charge ? Discharge) ===");
    ctx->currentPhase = BASELINE_PHASE_2;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Running capacity experiment...");
    
    result = RunPhase2_CapacityExperiment(ctx);
    if (result != SUCCESS || ctx->state == BASELINE_STATE_CANCELLED) {
        if (ctx->state != BASELINE_STATE_CANCELLED) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Phase 3: EIS During Charge
    LogMessage("=== Phase 3: EIS Measurements During Charge ===");
    ctx->currentPhase = BASELINE_PHASE_3;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 3: EIS measurements during charge...");
    
    result = RunPhase3_EISCharge(ctx);
    if (result != SUCCESS || ctx->state == BASELINE_STATE_CANCELLED) {
        if (ctx->state != BASELINE_STATE_CANCELLED) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Phase 4: Discharge to 50%
    LogMessage("=== Phase 4: Discharge to 50%% Capacity ===");
    ctx->currentPhase = BASELINE_PHASE_4;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 4: Discharging to 50% capacity...");
    
    result = RunPhase4_Discharge50Percent(ctx);
    if (result != SUCCESS || ctx->state == BASELINE_STATE_CANCELLED) {
        if (ctx->state != BASELINE_STATE_CANCELLED) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Record experiment end time
    ctx->experimentEndTime = Timer();
    
    ctx->state = BASELINE_STATE_COMPLETED;
    LogMessage("=== Baseline Experiment Completed Successfully ===");
    
    // Write results file
    result = WriteResultsFile(ctx);
    if (result != SUCCESS) {
        LogError("Failed to write results file");
    }
    
cleanup:
    // Clean up all devices
    CleanupAllDevices(ctx);
    
    // Update status based on final state
    if (ctx->state == BASELINE_STATE_COMPLETED) {
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Baseline experiment completed");
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Baseline experiment completed");
    } else if (ctx->state == BASELINE_STATE_CANCELLED) {
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Baseline experiment cancelled");
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Baseline experiment cancelled");
    } else {
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Baseline experiment failed");
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Baseline experiment failed");
    }
    
    // Free allocated memory
    if (ctx->eisMeasurements) {
        for (int i = 0; i < ctx->eisMeasurementCapacity; i++) {
            if (i < ctx->eisMeasurementCount || 
                ctx->eisMeasurements[i].ocvData || 
                ctx->eisMeasurements[i].geisData ||
                ctx->eisMeasurements[i].frequencies) {
                
                if (ctx->eisMeasurements[i].ocvData) {
                    BIO_FreeTechniqueData(ctx->eisMeasurements[i].ocvData);
                }
                if (ctx->eisMeasurements[i].geisData) {
                    BIO_FreeTechniqueData(ctx->eisMeasurements[i].geisData);
                }
                if (ctx->eisMeasurements[i].frequencies) {
                    free(ctx->eisMeasurements[i].frequencies);
                }
                if (ctx->eisMeasurements[i].zReal) {
                    free(ctx->eisMeasurements[i].zReal);
                }
                if (ctx->eisMeasurements[i].zImag) {
                    free(ctx->eisMeasurements[i].zImag);
                }
            }
        }
        free(ctx->eisMeasurements);
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
 * Device Verification and Setup
 ******************************************************************************/

static int VerifyAllDevices(BaselineExperimentContext *ctx) {
    // Check PSB connection
    PSBQueueManager *psbQueueMgr = PSB_GetGlobalQueueManager();
    if (!psbQueueMgr) {
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    ctx->psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!ctx->psbHandle || !ctx->psbHandle->isConnected) {
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check BioLogic connection
    BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
    if (!bioQueueMgr) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    ctx->biologicID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (ctx->biologicID < 0) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check DTB connection
    DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
    if (!dtbQueueMgr) {
        MessagePopup("DTB Not Connected", 
                     "The DTB temperature controller is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    DTB_Handle *dtbHandle = DTB_QueueGetHandle(dtbQueueMgr, ctx->dtbSlaveAddress);
    if (!dtbHandle || !dtbHandle->isConnected) {
        MessagePopup("DTB Not Connected", 
                     "The DTB temperature controller is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check Teensy connection
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        MessagePopup("Teensy Not Connected", 
                     "The Teensy relay controller is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check that PSB output is disabled
    PSB_Status status;
    if (PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL) == PSB_SUCCESS) {
        if (status.outputEnabled) {
            MessagePopup("PSB Output Enabled", 
                         "The PSB output must be disabled before starting the experiment.\n"
                         "Please turn off the output and try again.");
            return ERR_INVALID_STATE;
        }
    } else {
        MessagePopup("Communication Error", 
                     "Failed to communicate with the PSB.\n"
                     "Please check the connection and try again.");
        return ERR_COMM_FAILED;
    }
    
    LogMessage("All devices verified successfully");
    return SUCCESS;
}

static int CreateExperimentDirectory(BaselineExperimentContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        strcpy(basePath, ".");
    }
    
    // Create data directory
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, BASELINE_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamped subdirectory
    int result = CreateTimestampedDirectory(dataPath, "baseline_experiment", 
                                          ctx->experimentDirectory, sizeof(ctx->experimentDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        return result;
    }
    
    LogMessage("Created experiment directory: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int ConfigureGraphs(BaselineExperimentContext *ctx) {
    // Configure Graph 1 - Current vs Time
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph1Handle, 
                   "Current vs Time", "Time (s)", "Current (A)", 
                   0.0, maxCurrent * 1.1);
    
    // Configure Graph 2 - Voltage vs Time initially
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // Configure Graph Biologic - Will be used for Nyquist plot in Phase 3
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, "Ready for EIS");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_XNAME, "Time (s)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_YNAME, "Temperature (°C)");
    
    // Clear any existing plots
    ClearGraphs(ctx);
    
    return SUCCESS;
}

/******************************************************************************
 * Phase Implementation Functions
 ******************************************************************************/

static int RunPhase1_DischargeAndTemp(BaselineExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    int result;
    
    ctx->state = BASELINE_STATE_PHASE1_DISCHARGE;
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, 0.0);
    
    // Create phase 1 log file
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE1_FILE);
    
    ctx->currentLogFile = fopen(filename, "w");
    if (!ctx->currentLogFile) {
        LogError("Failed to create phase 1 log file");
        return ERR_BASE_FILE;
    }
    
    // Write CSV header
    fprintf(ctx->currentLogFile, "Time_s,Voltage_V,Current_A,Power_W,DTB_Temp_C,TC0_Temp_C,TC1_Temp_C\n");
    
    // Switch to PSB for discharge
    result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Configure discharge parameters with graph support
    VoltageTargetParams dischargeParams = {
        .targetVoltage_V = ctx->params.dischargeVoltage,
        .maxCurrent_A = ctx->params.dischargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,
        .updateIntervalMs = ctx->params.logInterval * 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .graph1Handle = ctx->graph1Handle,
        .graph2Handle = ctx->graph2Handle,
        .cancelFlag = &ctx->cancelRequested,
        .progressCallback = NULL,
        .statusCallback = NULL
    };
    
    // Start DTB and set temperature
    LogMessage("Starting DTB and setting target temperature: %.1f °C", ctx->params.targetTemperature);
    result = DTB_SetSetPointQueued(ctx->dtbSlaveAddress, ctx->params.targetTemperature, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set DTB temperature: %s", DTB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    result = DTB_SetRunStopQueued(ctx->dtbSlaveAddress, 1, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to start DTB: %s", DTB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Perform discharge with graph updates
    LogMessage("Discharging battery to %.2f V", ctx->params.dischargeVoltage);
    result = Battery_GoToVoltage(&dischargeParams);
    
    if (result != SUCCESS || dischargeParams.result != BATTERY_OP_SUCCESS) {
        if (dischargeParams.result == BATTERY_OP_ABORTED) {
            LogMessage("Phase 1 discharge cancelled by user");
            fclose(ctx->currentLogFile);
            return ERR_CANCELLED;
        }
        LogError("Phase 1 discharge failed");
        fclose(ctx->currentLogFile);
        return ERR_OPERATION_FAILED;
    }
    
    LogMessage("Phase 1 discharge completed: %.2f mAh in %.1f minutes", 
               fabs(dischargeParams.actualCapacity_mAh), dischargeParams.elapsedTime_s / 60.0);
    
    // Update output control with discharge current
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, fabs(dischargeParams.actualCapacity_mAh));
    
    fclose(ctx->currentLogFile);
    
    // Check for cancellation before proceeding to temperature wait
    if (ctx->state == BASELINE_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Wait for target temperature
    ctx->state = BASELINE_STATE_PHASE1_TEMP_WAIT;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Waiting for target temperature...");
    
    result = WaitForTargetTemperature(ctx);
    if (result != SUCCESS) {
        return result;
    }
    
    LogMessage("Phase 1 completed successfully");
    return SUCCESS;
}

static int RunPhase2_CapacityExperiment(BaselineExperimentContext *ctx) {
    int result;
    
    ctx->state = BASELINE_STATE_PHASE2_CAPACITY;
    
    // Clear graphs for new phase
    ClearGraphs(ctx);
    
    // Reconfigure Graph 2 for voltage vs time
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // Initialize phase results
    memset(&ctx->chargeResults, 0, sizeof(ctx->chargeResults));
    memset(&ctx->dischargeResults, 0, sizeof(ctx->dischargeResults));
    
    // Run charge phase first (battery is discharged from Phase 1)
    LogMessage("Running charge phase...");
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Charging battery...");
    
    VoltageTargetParams chargeParams = {
        .targetVoltage_V = ctx->params.chargeVoltage,
        .maxCurrent_A = ctx->params.chargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,
        .updateIntervalMs = ctx->params.logInterval * 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .graph1Handle = ctx->graph1Handle,
        .graph2Handle = ctx->graph2Handle,
        .cancelFlag = &ctx->cancelRequested,
        .progressCallback = NULL,
        .statusCallback = NULL
    };
    
    result = Battery_GoToVoltage(&chargeParams);
    if (result != SUCCESS || chargeParams.result != BATTERY_OP_SUCCESS) {
        if (chargeParams.result == BATTERY_OP_ABORTED) {
            LogMessage("Phase 2 charge cancelled by user");
            return ERR_CANCELLED;
        }
        LogError("Phase 2 charge failed");
        return ERR_OPERATION_FAILED;
    }
    
    // Store charge results
    ctx->chargeResults.capacity_mAh = chargeParams.actualCapacity_mAh;
    ctx->chargeResults.energy_Wh = chargeParams.actualEnergy_Wh;
    ctx->chargeResults.duration_s = chargeParams.elapsedTime_s;
    ctx->chargeResults.startVoltage = chargeParams.startVoltage_V;
    ctx->chargeResults.endVoltage = chargeParams.finalVoltage_V;
    ctx->measuredChargeCapacity_mAh = chargeParams.actualCapacity_mAh;
    
    LogMessage("Charge phase completed: %.2f mAh, %.2f Wh in %.1f minutes", 
               ctx->chargeResults.capacity_mAh, ctx->chargeResults.energy_Wh, 
               ctx->chargeResults.duration_s / 60.0);
    
    // Update output control with charge capacity
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->chargeResults.capacity_mAh);
    
    // Short delay between phases
    LogMessage("Switching from charge to discharge phase...");
    for (int i = 0; i < 20; i++) {
        if (ctx->state == BASELINE_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        Delay(0.1);
    }
    
    // Run discharge phase
    LogMessage("Running discharge phase...");
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Discharging battery...");
    
    VoltageTargetParams dischargeParams = {
        .targetVoltage_V = ctx->params.dischargeVoltage,
        .maxCurrent_A = ctx->params.dischargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,
        .updateIntervalMs = ctx->params.logInterval * 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .graph1Handle = ctx->graph1Handle,
        .graph2Handle = ctx->graph2Handle,
        .cancelFlag = &ctx->cancelRequested,
        .progressCallback = NULL,
        .statusCallback = NULL
    };
    
    result = Battery_GoToVoltage(&dischargeParams);
    if (result != SUCCESS || dischargeParams.result != BATTERY_OP_SUCCESS) {
        LogError("Phase 2 discharge failed");
        return ERR_OPERATION_FAILED;
    }
    
    // Store discharge results
    ctx->dischargeResults.capacity_mAh = fabs(dischargeParams.actualCapacity_mAh);
    ctx->dischargeResults.energy_Wh = fabs(dischargeParams.actualEnergy_Wh);
    ctx->dischargeResults.duration_s = dischargeParams.elapsedTime_s;
    ctx->dischargeResults.startVoltage = dischargeParams.startVoltage_V;
    ctx->dischargeResults.endVoltage = dischargeParams.finalVoltage_V;
    ctx->measuredDischargeCapacity_mAh = fabs(dischargeParams.actualCapacity_mAh);
    
    LogMessage("Discharge phase completed: %.2f mAh, %.2f Wh in %.1f minutes", 
               ctx->dischargeResults.capacity_mAh, ctx->dischargeResults.energy_Wh, 
               ctx->dischargeResults.duration_s / 60.0);
    
    // Update output control with discharge capacity
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->dischargeResults.capacity_mAh);
    
    // Write phase 2 results file
    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE2_RESULTS_FILE);
    
    FILE *file = fopen(filename, "w");
    if (file) {
        WriteINISection(file, "Phase2_Capacity_Results");
        WriteINIDouble(file, "Charge_Capacity_mAh", ctx->chargeResults.capacity_mAh, 2);
        WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->dischargeResults.capacity_mAh, 2);
        WriteINIDouble(file, "Charge_Energy_Wh", ctx->chargeResults.energy_Wh, 3);
        WriteINIDouble(file, "Discharge_Energy_Wh", ctx->dischargeResults.energy_Wh, 3);
        WriteINIDouble(file, "Coulombic_Efficiency_Percent", 
                      Battery_CalculateCoulombicEfficiency(ctx->chargeResults.capacity_mAh, 
                                                          ctx->dischargeResults.capacity_mAh), 1);
        WriteINIDouble(file, "Energy_Efficiency_Percent", 
                      Battery_CalculateEnergyEfficiency(ctx->chargeResults.energy_Wh,
                                                       ctx->dischargeResults.energy_Wh), 1);
        fclose(file);
    }
    
    LogMessage("Phase 2 completed successfully");
    return SUCCESS;
}

static int RunPhase3_EISCharge(BaselineExperimentContext *ctx) {
    int result;
    
    ctx->state = BASELINE_STATE_PHASE3_EIS_CHARGE;
    
    // Clear graphs and reconfigure for EIS
    ClearGraphs(ctx);
    
    // Configure Graph 2 for OCV vs SOC
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, "OCV vs SOC", "SOC (%)", "OCV (V)",
                   ctx->params.dischargeVoltage * 0.9, ctx->params.chargeVoltage * 1.1);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_BOTTOM_XAXIS, 
                       VAL_MANUAL, 0.0, 150.0);
    
    // Configure Graph Biologic for Nyquist Plot
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, "Nyquist Plot");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_XNAME, "Z' (Ohms)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_YNAME, "-Z'' (Ohms)");
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_LEFT_YAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    // Calculate target SOC points
    result = CalculateTargetSOCs(ctx);
    if (result != SUCCESS) {
        LogError("Failed to calculate target SOC points");
        return result;
    }
    
    // Perform initial measurement at 0% SOC
    LogMessage("Performing initial EIS measurement at 0%% SOC...");
    result = PerformEISMeasurement(ctx, 0.0);
    if (result != SUCCESS) {
        LogError("Initial EIS measurement failed");
        return result;
    }
    
    // Initialize charging with EIS measurements
    // Similar to SOCEIS RunChargingPhase but with modifications for baseline
    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE3_FILE);
    
    ctx->currentLogFile = fopen(filename, "w");
    if (!ctx->currentLogFile) {
        LogError("Failed to create phase 3 log file");
        return ERR_BASE_FILE;
    }
    
    fprintf(ctx->currentLogFile, "Time_s,Voltage_V,Current_A,Power_W,SOC_Percent,DTB_Temp_C,TC0_Temp_C,TC1_Temp_C\n");
    
    // Switch to PSB for charging
    result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Set charging parameters
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
    
    result = PSB_SetPowerQueued(BASELINE_MAX_POWER, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
    
    // Enable PSB output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        fclose(ctx->currentLogFile);
        return result;
    }
    
    // Initialize timing and SOC tracking
    ctx->phaseStartTime = Timer();
    ctx->lastLogTime = ctx->phaseStartTime;
    ctx->lastGraphUpdate = ctx->phaseStartTime;
    ctx->currentSOC = 0.0;
    ctx->accumulatedCapacity_mAh = 0.0;
    ctx->lastCurrent = 0.0;
    ctx->lastTime = 0.0;
    
    int nextTargetIndex = 1;  // Skip 0% as we already measured it
    
    // Main charging loop with EIS measurements
    while (1) {
        if (ctx->state == BASELINE_STATE_CANCELLED) {
            LogMessage("Phase 3 cancelled by user");
            break;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - ctx->phaseStartTime;
        
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            LogError("Failed to read status: %s", PSB_GetErrorString(result));
            break;
        }
        
        // Update SOC tracking
        UpdateSOCTracking(ctx, status.voltage, status.current);
        
        // Log data if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            TemperatureDataPoint tempData;
            ReadTemperatures(&tempData, elapsedTime);
            
            fprintf(ctx->currentLogFile, "%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f\n", 
                    elapsedTime, status.voltage, status.current, status.power, ctx->currentSOC,
                    tempData.dtbTemperature, tempData.tc0Temperature, tempData.tc1Temperature);
            fflush(ctx->currentLogFile);
            ctx->lastLogTime = currentTime;
            
            // Update SOC display
            SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->currentSOC);
        }
        
        // Update graph if needed
        if ((currentTime - ctx->lastGraphUpdate) >= 1.0) {
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
            result = PerformEISMeasurement(ctx, ctx->targetSOCs[nextTargetIndex]);
            if (result != SUCCESS) {
                LogError("EIS measurement failed at %.1f%% SOC", ctx->currentSOC);
                break;
            }
            
            nextTargetIndex++;
            
            // Resume charging
            LogMessage("Resuming charging after EIS measurement...");
            result = SwitchToPSB(ctx);
            if (result != SUCCESS) break;
            
            PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
            Delay(1.0);
        }
        
        // Check current threshold for charge completion
        if (fabs(status.current) < ctx->params.currentThreshold) {
            LogMessage("Phase 3 charging completed - current below threshold");
            break;
        }
        
        ProcessSystemEvents();
        Delay(0.5);
    }
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    fclose(ctx->currentLogFile);
    
    LogMessage("Phase 3 completed successfully");
    return SUCCESS;
}

static int RunPhase4_Discharge50Percent(BaselineExperimentContext *ctx) {
    if (ctx->measuredChargeCapacity_mAh <= 0) {
        LogWarning("Cannot discharge to 50%% - charge capacity unknown");
        return ERR_INVALID_PARAMETER;
    }
    
    ctx->state = BASELINE_STATE_PHASE4_DISCHARGE_50;
    
    LogMessage("Discharging battery to 50%% capacity");
    LogMessage("Target discharge: %.2f mAh", ctx->measuredChargeCapacity_mAh * 0.5);
    
    // Clear graphs and reconfigure for discharge
    ClearGraphs(ctx);
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // Switch to PSB
    int result = SwitchToPSB(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to PSB for discharge");
        return result;
    }
    
    // Configure discharge parameters
    DischargeParams discharge50 = {
        .targetCapacity_mAh = ctx->measuredChargeCapacity_mAh * 0.5,
        .dischargeCurrent_A = ctx->params.dischargeCurrent,
        .dischargeVoltage_V = ctx->params.dischargeVoltage,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,
        .updateIntervalMs = ctx->params.logInterval * 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .progressCallback = NULL,
        .statusCallback = NULL
    };
    
    // Perform the discharge
    int dischargeResult = Battery_DischargeCapacity(&discharge50);
    
    if (dischargeResult == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
        LogMessage("Successfully discharged battery to 50%% capacity");
        LogMessage("  Discharged: %.2f mAh", discharge50.actualDischarged_mAh);
        LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
        LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
        
        // Calculate final SOC
        double finalSOC = 50.0 - ((discharge50.actualDischarged_mAh - discharge50.targetCapacity_mAh) / 
                                 ctx->measuredChargeCapacity_mAh) * 100.0;
        SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, finalSOC);
        
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 4 completed - battery at ~50% capacity");
    } else {
        LogWarning("Failed to discharge to 50%% capacity");
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 4 failed - discharge incomplete");
        return ERR_OPERATION_FAILED;
    }
    
    LogMessage("Phase 4 completed successfully");
    return SUCCESS;
}

/******************************************************************************
 * Device Control Helper Functions
 ******************************************************************************/

static int SetRelayState(int pin, int state) {
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        return ERR_NOT_CONNECTED;
    }
    
    return TNY_SetPinQueued(pin, state, DEVICE_PRIORITY_NORMAL);
}

static int SwitchToPSB(BaselineExperimentContext *ctx) {
    int result;
    
    LogMessage("Switching to PSB...");
    
    // Disconnect BioLogic first
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to disconnect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect PSB
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to connect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to PSB");
    return SUCCESS;
}

static int SwitchToBioLogic(BaselineExperimentContext *ctx) {
    int result;
    
    LogMessage("Switching to BioLogic...");
    
    // Disable PSB output first for safety
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);
    
    // Disconnect PSB first
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect BioLogic
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogError("Failed to connect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to BioLogic");
    return SUCCESS;
}

static int WaitForTargetTemperature(BaselineExperimentContext *ctx) {
    double startTime = Timer();
    double lastCheckTime = startTime;
    
    LogMessage("Waiting for DTB to reach target temperature: %.1f °C", ctx->params.targetTemperature);
    
    while (1) {
        if (ctx->state == BASELINE_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        
        // Check temperature every few seconds
        if ((currentTime - lastCheckTime) >= BASELINE_TEMP_CHECK_INTERVAL) {
            DTB_Status status;
            int result = DTB_GetStatusQueued(ctx->dtbSlaveAddress, &status, DEVICE_PRIORITY_NORMAL);
            
            if (result == DTB_SUCCESS) {
                double tempDiff = fabs(status.processValue - ctx->params.targetTemperature);
                
                LogDebug("DTB temperature: %.1f °C (target: %.1f °C, diff: %.1f °C)", 
                        status.processValue, ctx->params.targetTemperature, tempDiff);
                
                if (tempDiff <= BASELINE_TEMP_TOLERANCE) {
                    LogMessage("DTB reached target temperature: %.1f °C", status.processValue);
                    ctx->dtbReady = 1;
                    return SUCCESS;
                }
                
                // Update status
                char statusMsg[MEDIUM_BUFFER_SIZE];
                snprintf(statusMsg, sizeof(statusMsg), 
                         "Waiting for temperature: %.1f/%.1f °C", 
                         status.processValue, ctx->params.targetTemperature);
                SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            }
            
            lastCheckTime = currentTime;
        }
        
        // Check timeout
        if ((currentTime - startTime) > BASELINE_TEMP_TIMEOUT_SEC) {
            LogWarning("Temperature timeout reached");
            MessagePopup("Temperature Timeout", 
                         "DTB did not reach target temperature within timeout period.\n"
                         "Continue anyway?");
            // For now, continue anyway
            ctx->dtbReady = 1;
            return SUCCESS;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

/******************************************************************************
 * EIS Functions (adapted from SOCEIS)
 ******************************************************************************/

static int CalculateTargetSOCs(BaselineExperimentContext *ctx) {
    // Similar to SOCEIS implementation
    ctx->numTargetSOCs = 2;  // Start with 0% and 100%
    
    if (ctx->params.eisInterval > 0 && ctx->params.eisInterval < 100) {
        double soc = ctx->params.eisInterval;
        while (soc < 100.0) {
            ctx->numTargetSOCs++;
            soc += ctx->params.eisInterval;
        }
    }
    
    int initialCapacity = ctx->numTargetSOCs + 10;
    ctx->targetSOCs = (double*)calloc(initialCapacity, sizeof(double));
    if (!ctx->targetSOCs) {
        return ERR_OUT_OF_MEMORY;
    }
    
    int index = 0;
    ctx->targetSOCs[index++] = 0.0;
    
    if (ctx->params.eisInterval > 0 && ctx->params.eisInterval < 100) {
        double soc = ctx->params.eisInterval;
        while (soc < 100.0 && index < ctx->numTargetSOCs - 1) {
            ctx->targetSOCs[index++] = soc;
            soc += ctx->params.eisInterval;
        }
    }
    
    ctx->targetSOCs[ctx->numTargetSOCs - 1] = 100.0;
    
    ctx->eisMeasurementCapacity = initialCapacity;
    ctx->eisMeasurements = (BaselineEISMeasurement*)calloc(ctx->eisMeasurementCapacity, sizeof(BaselineEISMeasurement));
    if (!ctx->eisMeasurements) {
        return ERR_OUT_OF_MEMORY;
    }
    
    LogMessage("Target SOC points for EIS measurements:");
    for (int i = 0; i < ctx->numTargetSOCs; i++) {
        LogMessage("  %.1f%%", ctx->targetSOCs[i]);
    }
    
    return SUCCESS;
}

static int PerformEISMeasurement(BaselineExperimentContext *ctx, double targetSOC) {
    int result;
    
    if (ctx->state == BASELINE_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        LogError("EIS measurement array full!");
        return ERR_OPERATION_FAILED;
    }
    
    BaselineEISMeasurement *measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    measurement->targetSOC = targetSOC;
    measurement->actualSOC = ctx->currentSOC;
    measurement->timestamp = Timer() - ctx->experimentStartTime;
    
    // Read temperatures during measurement
    ReadTemperatures(&measurement->tempData, measurement->timestamp);
    
    // Initialize pointers for safe cleanup
    measurement->ocvData = NULL;
    measurement->geisData = NULL;
    measurement->frequencies = NULL;
    measurement->zReal = NULL;
    measurement->zImag = NULL;
    measurement->numPoints = 0;
    
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), 
             "Measuring EIS at %.1f%% SOC...", ctx->currentSOC);
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
    
    // Switch to BioLogic
    result = SwitchToBioLogic(ctx);
    if (result != SUCCESS) {
        LogError("Failed to switch to BioLogic for EIS measurement");
        return result;
    }
    
    // Run OCV measurement
    result = RunOCVMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogError("OCV measurement failed");
        return result;
    }
    
    if (ctx->state == BASELINE_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Run GEIS measurement
    result = RunGEISMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogError("GEIS measurement failed");
        return result;
    }
    
    // Process GEIS data
    result = ProcessGEISData(measurement->geisData, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to process GEIS data");
    }
    
    // Update graphs
    UpdateOCVGraph(ctx, measurement);
    UpdateNyquistPlot(ctx, measurement);
    
    // Save measurement data
    result = SaveEISMeasurementData(ctx, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to save EIS measurement data");
    }
    
    ctx->eisMeasurementCount++;
    
    LogMessage("EIS measurement completed at %.1f%% SOC (OCV: %.3f V)", 
               measurement->actualSOC, measurement->ocvVoltage);
    
    return SUCCESS;
}

static int RunOCVMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    LogDebug("Starting OCV measurement...");
    
    measurement->ocvVoltage = 0.0;
    
    int result = BIO_RunOCVQueued(ctx->biologicID, 0,
                                OCV_DURATION_S,
                                OCV_SAMPLE_INTERVAL_S,
                                OCV_RECORD_EVERY_DE,
                                OCV_RECORD_EVERY_DT,
                                OCV_E_RANGE,
                                true,
                                &measurement->ocvData,
                                OCV_TIMEOUT_MS,
                                DEVICE_PRIORITY_NORMAL,
                                NULL, NULL);
    
    if (result != SUCCESS) {
        LogError("OCV measurement failed: %s", BIO_GetErrorString(result));
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
        Delay(0.5);
        return result;
    }
    
    // Extract final voltage
    if (measurement->ocvData && measurement->ocvData->convertedData) {
        BIO_ConvertedData *convData = measurement->ocvData->convertedData;
        
        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            int lastPoint = convData->numPoints - 1;
            measurement->ocvVoltage = convData->data[1][lastPoint];
            LogDebug("OCV measurement complete: %.3f V", measurement->ocvVoltage);
        } else {
            LogWarning("OCV data incomplete");
        }
    } else {
        LogWarning("No OCV data received from BioLogic");
    }
    
    return SUCCESS;
}

static int RunGEISMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    LogDebug("Starting GEIS measurement...");
    
    int result = BIO_RunGEISQueued(ctx->biologicID, 0,
                                 GEIS_VS_INITIAL,
                                 GEIS_INITIAL_CURRENT,
                                 GEIS_DURATION_S,
                                 GEIS_RECORD_EVERY_DT,
                                 GEIS_RECORD_EVERY_DE,
                                 GEIS_INITIAL_FREQ,
                                 GEIS_FINAL_FREQ,
                                 GEIS_SWEEP_LINEAR,
                                 GEIS_AMPLITUDE_I,
                                 GEIS_FREQ_NUMBER,
                                 GEIS_AVERAGE_N,
                                 GEIS_CORRECTION,
                                 GEIS_WAIT_FOR_STEADY,
                                 GEIS_I_RANGE,
                                 true,
                                 &measurement->geisData,
                                 GEIS_TIMEOUT_MS,
                                 DEVICE_PRIORITY_NORMAL,
                                 NULL, NULL);
    
    if (result != SUCCESS) {
        LogError("GEIS measurement failed: %d %s", result, GetErrorString(result));
        return result;
    }
    
    LogDebug("GEIS measurement complete");
    return SUCCESS;
}

static int ProcessGEISData(BIO_TechniqueData *geisData, BaselineEISMeasurement *measurement) {
    // Same implementation as in SOCEIS
    if (!geisData || !geisData->convertedData) {
        LogWarning("No GEIS data available");
        return ERR_INVALID_PARAMETER;
    }
    
    BIO_ConvertedData *convData = geisData->convertedData;
    int processIndex = -1;
    
    if (geisData->rawData) {
        processIndex = geisData->rawData->processIndex;
    }
    
    LogDebug("Processing GEIS data: %d points, %d variables (process %d)", 
             convData->numPoints, convData->numVariables, processIndex);
    
    if (processIndex == 1 && convData->numVariables >= 11) {
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
        
        for (int i = 0; i < convData->numPoints; i++) {
            measurement->frequencies[i] = convData->data[0][i];
            measurement->zReal[i] = convData->data[4][i];
            measurement->zImag[i] = convData->data[5][i];
        }
        
        measurement->numPoints = convData->numPoints;
        LogDebug("Successfully extracted %d impedance points from GEIS data", 
                 measurement->numPoints);
    } else {
        LogWarning("Unexpected GEIS data format: process %d with %d variables", 
                  processIndex, convData->numVariables);
        return ERR_OPERATION_FAILED;
    }
    
    return SUCCESS;
}

static int SaveEISMeasurementData(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s%02d.txt", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             BASELINE_DETAILS_FILE_PREFIX, 
             (int)(measurement->actualSOC + 0.5));
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create EIS measurement file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    WriteINISection(file, "EIS_Measurement_Information");
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    WriteINIValue(file, "Timestamp", "%s", timeStr);
    
    WriteINIDouble(file, "Target_SOC_Percent", measurement->targetSOC, 1);
    WriteINIDouble(file, "Actual_SOC_Percent", measurement->actualSOC, 1);
    WriteINIDouble(file, "Elapsed_Time_s", measurement->timestamp, 1);
    WriteINIDouble(file, "Battery_Voltage_V", measurement->ocvVoltage, 3);
    WriteINIDouble(file, "DTB_Temperature_C", measurement->tempData.dtbTemperature, 1);
    WriteINIDouble(file, "TC0_Temperature_C", measurement->tempData.tc0Temperature, 1);
    WriteINIDouble(file, "TC1_Temperature_C", measurement->tempData.tc1Temperature, 1);
    fprintf(file, "\n");
    
    // Write impedance data if available
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
    
    LogDebug("Saved EIS measurement data to: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Data Logging and Tracking
 ******************************************************************************/

static int ReadTemperatures(TemperatureDataPoint *tempData, double timestamp) {
    tempData->timestamp = timestamp;
    
    // Read DTB temperature
    DTB_Status dtbStatus;
    if (DTB_GetStatusQueued(DTB1_SLAVE_ADDRESS, &dtbStatus, DEVICE_PRIORITY_NORMAL) == DTB_SUCCESS) {
        tempData->dtbTemperature = dtbStatus.processValue;
    } else {
        tempData->dtbTemperature = 0.0;
    }
    
    // Read thermocouple temperatures
    if (CDAQ_ReadTC(2, 0, &tempData->tc0Temperature) != SUCCESS) {
        tempData->tc0Temperature = 0.0;
    }
    
    if (CDAQ_ReadTC(2, 1, &tempData->tc1Temperature) != SUCCESS) {
        tempData->tc1Temperature = 0.0;
    }
    
    return SUCCESS;
}

static int UpdateSOCTracking(BaselineExperimentContext *ctx, double voltage, double current) {
    double currentTime = Timer() - ctx->phaseStartTime;
    
    if (ctx->lastTime > 0) {
        double deltaTime = currentTime - ctx->lastTime;
        
        double capacityIncrement = Battery_CalculateCapacityIncrement(
            fabs(ctx->lastCurrent), fabs(current), deltaTime);
        
        ctx->accumulatedCapacity_mAh += capacityIncrement;
        
        if (ctx->measuredChargeCapacity_mAh > 0) {
            ctx->currentSOC = (ctx->accumulatedCapacity_mAh / ctx->measuredChargeCapacity_mAh) * 100.0;
            
            if (ctx->currentSOC < 0.0) {
                ctx->currentSOC = 0.0;
            }
        }
    }
    
    ctx->lastCurrent = current;
    ctx->lastTime = currentTime;
    
    return SUCCESS;
}

/******************************************************************************
 * Graph Functions
 ******************************************************************************/

static void UpdateGraphs(BaselineExperimentContext *ctx, double current, double time) {
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph1Handle, 
                  time, fabs(current), VAL_SOLID_CIRCLE, VAL_RED);
}

static void UpdateOCVGraph(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    ctx->ocvPlotHandle = PlotPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
                                   measurement->actualSOC, measurement->ocvVoltage, 
                                   VAL_SOLID_CIRCLE, VAL_BLUE);
    
    if (ctx->eisMeasurementCount > 1) {
        double *socArray = (double*)calloc(ctx->eisMeasurementCount, sizeof(double));
        double *ocvArray = (double*)calloc(ctx->eisMeasurementCount, sizeof(double));
        
        if (socArray && ocvArray) {
            for (int i = 0; i < ctx->eisMeasurementCount; i++) {
                socArray[i] = ctx->eisMeasurements[i].actualSOC;
                ocvArray[i] = ctx->eisMeasurements[i].ocvVoltage;
            }
            
            PlotXY(ctx->mainPanelHandle, ctx->graph2Handle,
                   socArray, ocvArray, ctx->eisMeasurementCount,
                   VAL_DOUBLE, VAL_DOUBLE, VAL_THIN_LINE,
                   VAL_NO_POINT, VAL_SOLID, 1, VAL_BLUE);
            
            free(socArray);
            free(ocvArray);
        }
    }
}

static void UpdateNyquistPlot(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    if (measurement->numPoints == 0) return;
    
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphBiologicHandle, -1, VAL_DELAYED_DRAW);
    
    double *negZImag = (double*)calloc(measurement->numPoints, sizeof(double));
    if (!negZImag) return;
    
    for (int i = 0; i < measurement->numPoints; i++) {
        negZImag[i] = -measurement->zImag[i];
    }
    
    ctx->nyquistPlotHandle = PlotXY(ctx->mainPanelHandle, ctx->graphBiologicHandle,
                                    measurement->zReal, negZImag, measurement->numPoints,
                                    VAL_DOUBLE, VAL_DOUBLE, VAL_SCATTER,
                                    VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_GREEN);
    
    char title[MEDIUM_BUFFER_SIZE];
    snprintf(title, sizeof(title), "Nyquist Plot - SOC: %.1f%%", measurement->actualSOC);
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, title);
    
    free(negZImag);
}

static void ClearGraphs(BaselineExperimentContext *ctx) {
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle, ctx->graphBiologicHandle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 3);
}

/******************************************************************************
 * Results and Cleanup
 ******************************************************************************/

static int WriteResultsFile(BaselineExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_RESULTS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create results file");
        return ERR_BASE_FILE;
    }
    
    time_t startTime = (time_t)ctx->experimentStartTime;
    time_t endTime = (time_t)ctx->experimentEndTime;
    char startTimeStr[64], endTimeStr[64];
    
    FormatTimestamp(startTime, startTimeStr, sizeof(startTimeStr));
    FormatTimestamp(endTime, endTimeStr, sizeof(endTimeStr));
    
    fprintf(file, "# Baseline Experiment Results\n");
    fprintf(file, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Experiment_Information");
    WriteINIValue(file, "Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_h", (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0, 2);
    WriteINIDouble(file, "Target_Temperature_C", ctx->params.targetTemperature, 1);
    WriteINIDouble(file, "EIS_Interval_Percent", ctx->params.eisInterval, 1);
    fprintf(file, "\n");
    
    WriteINISection(file, "Phase2_Capacity_Results");
    WriteINIDouble(file, "Charge_Capacity_mAh", ctx->chargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->dischargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Coulombic_Efficiency_Percent", 
                  Battery_CalculateCoulombicEfficiency(ctx->chargeResults.capacity_mAh, 
                                                      ctx->dischargeResults.capacity_mAh), 1);
    fprintf(file, "\n");
    
    WriteINISection(file, "Phase3_EIS_Summary");
    WriteINIValue(file, "Total_EIS_Measurements", "%d", ctx->eisMeasurementCount);
    
    if (ctx->eisMeasurementCount > 0) {
        fprintf(file, "SOC_Points=");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            fprintf(file, "%.1f", ctx->eisMeasurements[i].actualSOC);
            if (i < ctx->eisMeasurementCount - 1) fprintf(file, ",");
        }
        fprintf(file, "\n");
    }
    
    fclose(file);
    
    LogMessage("Results written to: %s", filename);
    return SUCCESS;
}

static void CleanupAllDevices(BaselineExperimentContext *ctx) {
    LogMessage("Cleaning up all devices...");
    
    // Ensure all relays are disconnected
    SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    
    // Turn off PSB output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Stop DTB
    DTB_SetRunStopQueued(ctx->dtbSlaveAddress, 0, DEVICE_PRIORITY_NORMAL);
    
    LogMessage("All devices cleaned up");
}

static void RestoreUI(BaselineExperimentContext *ctx) {
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void BaselineExperiment_Cleanup(void) {
    if (BaselineExperiment_IsRunning()) {
        BaselineExperiment_Abort();
    }
}

int BaselineExperiment_Abort(void) {
    if (BaselineExperiment_IsRunning()) {
		g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = BASELINE_STATE_CANCELLED;
        
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}