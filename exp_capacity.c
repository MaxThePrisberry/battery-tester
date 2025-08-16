/******************************************************************************
 * exp_capacity.c
 * 
 * Battery Capacity Experiment Module Implementation
 ******************************************************************************/

#include "common.h"
#include "exp_capacity.h"
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
static CapacityExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

// Controls to be dimmed during experiment
static const int numControls = 3;
static const int controls[3] = {
    CAPACITY_NUM_CURRENT_THRESHOLD, 
    CAPACITY_NUM_INTERVAL,
    CAPACITY_CHECKBOX_RETURN_50
};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CapacityExperimentThread(void *functionData);
static int VerifyBatteryCharged(CapacityExperimentContext *ctx);
static int ConfigureGraphs(CapacityExperimentContext *ctx);
static int RunExperimentPhase(CapacityExperimentContext *ctx, CapacityExperimentPhase phase);
static int LogDataPoint(CapacityExperimentContext *ctx, CapacityDataPoint *point);
static void UpdateGraphs(CapacityExperimentContext *ctx, CapacityDataPoint *point);
static void RestoreUI(CapacityExperimentContext *ctx);
static int CreateExperimentDirectory(CapacityExperimentContext *ctx);
static int WriteResultsFile(CapacityExperimentContext *ctx);
static void UpdatePhaseResults(CapacityExperimentContext *ctx, PhaseResults *results, 
                               CapacityDataPoint *point);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if capacity experiment is already running
    if (CapacityExperiment_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop capacity experiment");
        g_experimentContext.state = CAPACITY_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the capacity experiment.");
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
                     "Please ensure it is connected before running the capacity experiment.");
        return 0;
    }
    
    PSB_Handle *psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!psbHandle || !psbHandle->isConnected) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the capacity experiment.");
        return 0;
    }
    
    // Check that PSB output is disabled
    PSB_Status status;
    if (PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL) == PSB_SUCCESS) {
        if (status.outputEnabled) {
            CmtGetLock(g_busyLock);
            g_systemBusy = 0;
            CmtReleaseLock(g_busyLock);
            
            MessagePopup("PSB Output Enabled", 
                         "The PSB output must be disabled before starting the experiment.\n"
                         "Please turn off the output and try again.");
            return 0;
        }
    } else {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Communication Error", 
                     "Failed to communicate with the PSB.\n"
                     "Please check the connection and try again.");
        return 0;
    }
    
    // Initialize experiment context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
    g_experimentContext.state = CAPACITY_STATE_PREPARING;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.buttonControl = control;
    g_experimentContext.statusControl = PANEL_STR_PSB_STATUS;
    g_experimentContext.graph1Handle = PANEL_GRAPH_1;
    g_experimentContext.graph2Handle = PANEL_GRAPH_2;
    
    // Read experiment parameters from UI
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_experimentContext.params.chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_experimentContext.params.dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_experimentContext.params.chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_experimentContext.params.dischargeCurrent);
    GetCtrlVal(panel, CAPACITY_NUM_CURRENT_THRESHOLD, &g_experimentContext.params.currentThreshold);
    GetCtrlVal(panel, CAPACITY_NUM_INTERVAL, &g_experimentContext.params.logInterval);
    
    // Update UI
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    DimExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);
    
    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CapacityExperimentThread, 
                                            &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        g_experimentContext.state = CAPACITY_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start capacity experiment thread.");
        return 0;
    }
    
    return 0;
}

int CapacityExperiment_IsRunning(void) {
    return !(g_experimentContext.state == CAPACITY_STATE_IDLE ||
             g_experimentContext.state == CAPACITY_STATE_COMPLETED ||
             g_experimentContext.state == CAPACITY_STATE_ERROR ||
             g_experimentContext.state == CAPACITY_STATE_CANCELLED);
}

/******************************************************************************
 * Experiment Thread Implementation
 ******************************************************************************/

static int CapacityExperimentThread(void *functionData) {
    CapacityExperimentContext *ctx = (CapacityExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    double chargeCapacity_mAh = 0.0;
    
    LogMessage("=== Starting Battery Capacity Experiment ===");
    
    // Record experiment start time
    ctx->experimentStartTime = GetTimestamp();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity experiment cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    snprintf(message, sizeof(message),
        "Battery Capacity Experiment Parameters:\n\n"
        "Charge Voltage: %.2f V\n"
        "Discharge Voltage: %.2f V\n"
        "Charge Current: %.2f A\n"
        "Discharge Current: %.2f A\n"
        "Current Threshold: %.3f A\n"
        "Log Interval: %d seconds\n\n"
        "Please confirm these parameters are correct.",
        ctx->params.chargeVoltage,
        ctx->params.dischargeVoltage,
        ctx->params.chargeCurrent,
        ctx->params.dischargeCurrent,
        ctx->params.currentThreshold,
        ctx->params.logInterval);
    
    int response = ConfirmPopup("Confirm Experiment Parameters", message);
    if (!response || ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity experiment cancelled by user");
        ctx->state = CAPACITY_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create experiment directory
    result = CreateExperimentDirectory(ctx);
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        MessagePopup("Error", "Failed to create experiment directory.\nPlease check permissions.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Initialize PSB to safe state
    LogMessage("Initializing PSB to zeroed state...");
    result = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to initialize PSB to safe state: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to initialize PSB to safe state.\nPlease check the connection and try again.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Check for cancellation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity experiment cancelled during initialization");
        goto cleanup;
    }
    
    // Verify battery is charged
    result = VerifyBatteryCharged(ctx);
    if (result != SUCCESS || ctx->state == CAPACITY_STATE_CANCELLED) {
        if (ctx->state != CAPACITY_STATE_CANCELLED) {
            ctx->state = CAPACITY_STATE_CANCELLED;
        }
        goto cleanup;
    }
    
    // Configure graphs
    ConfigureGraphs(ctx);
    
    // Run discharge phase
    LogMessage("Starting discharge phase...");
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Discharging battery...");
    
    result = RunExperimentPhase(ctx, CAPACITY_PHASE_DISCHARGE);
    if (result != SUCCESS || ctx->state == CAPACITY_STATE_CANCELLED) {
        if (ctx->state != CAPACITY_STATE_CANCELLED) {
            ctx->state = CAPACITY_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Clear graphs between phases
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 2);
    
    // Short delay between phases
    LogMessage("Switching from discharge to charge phase...");
    for (int i = 0; i < 20; i++) {
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            goto cleanup;
        }
        Delay(0.1);
    }
    
    // Run charge phase
    LogMessage("Starting charge phase...");
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Charging battery...");
    
    result = RunExperimentPhase(ctx, CAPACITY_PHASE_CHARGE);
    if (result != SUCCESS || ctx->state == CAPACITY_STATE_CANCELLED) {
        if (ctx->state != CAPACITY_STATE_CANCELLED) {
            ctx->state = CAPACITY_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Store charge capacity for potential 50% return
    chargeCapacity_mAh = ctx->chargeResults.capacity_mAh;
    
    // Record experiment end time
    ctx->experimentEndTime = GetTimestamp();
    
    ctx->state = CAPACITY_STATE_COMPLETED;
    LogMessage("=== Battery Capacity Experiment Completed Successfully ===");
    
    // Write results file
    result = WriteResultsFile(ctx);
    if (result != SUCCESS) {
        LogError("Failed to write results file");
    }
    
    // Check if we should return to 50% capacity
    if (ctx->state == CAPACITY_STATE_COMPLETED) {
        int return50 = 0;
        GetCtrlVal(ctx->tabPanelHandle, CAPACITY_CHECKBOX_RETURN_50, &return50);
        
        if (return50 && chargeCapacity_mAh > 0) {
            LogMessage("=== Returning battery to 50%% capacity ===");
            LogMessage("Charge capacity measured: %.2f mAh", chargeCapacity_mAh);
            LogMessage("Target discharge: %.2f mAh", chargeCapacity_mAh * 0.5);
            
            SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Returning battery to 50% capacity...");
            
            // Configure discharge parameters
            DischargeParams discharge50 = {
                .targetCapacity_mAh = chargeCapacity_mAh * 0.5,
                .dischargeCurrent_A = ctx->params.dischargeCurrent,
                .dischargeVoltage_V = ctx->params.dischargeVoltage,
                .currentThreshold_A = ctx->params.currentThreshold,
                .timeoutSeconds = 3600.0,
                .updateIntervalMs = 1000,
                .panelHandle = ctx->mainPanelHandle,
                .statusControl = ctx->statusControl,
                .progressControl = 0,
                .progressCallback = NULL,
                .statusCallback = NULL
            };
            
            // Perform the discharge
            int dischargeResult = Battery_DischargeCapacity(&discharge50);
            
            if (dischargeResult == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
                LogMessage("Successfully returned battery to 50%% capacity");
                LogMessage("  Discharged: %.2f mAh", discharge50.actualDischarged_mAh);
                LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
                LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
                
                SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, 
                          "Capacity experiment completed - battery at 50% capacity");
            } else {
                LogWarning("Failed to return to 50%% capacity");
                SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, 
                          "Capacity experiment completed - failed to return to 50%");
            }
        }
    }
    
cleanup:
    // Turn off PSB output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Update status based on final state
    const char *statusMsg;
    switch (ctx->state) {
        case CAPACITY_STATE_COMPLETED:
            statusMsg = "Capacity experiment completed";
            break;
        case CAPACITY_STATE_CANCELLED:
            statusMsg = "Capacity experiment cancelled";
            break;
        default:
            statusMsg = "Capacity experiment failed";
            break;
    }
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, statusMsg);
    
    // Restore UI
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start");
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

static int CreateExperimentDirectory(CapacityExperimentContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        strcpy(basePath, ".");
    }
    
    // Create data directory
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, CAPACITY_EXPERIMENT_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamped subdirectory
    int result = CreateTimestampedDirectory(dataPath, "capacity_exp", 
                                          ctx->experimentDirectory, sizeof(ctx->experimentDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        return result;
    }
    
    LogMessage("Created experiment directory: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int VerifyBatteryCharged(CapacityExperimentContext *ctx) {
    char message[LARGE_BUFFER_SIZE];
    
    LogMessage("Verifying battery charge state...");
    
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Get current battery voltage
    PSB_Status status;
    int error = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    
    if (error != PSB_SUCCESS) {
        LogError("Failed to read PSB status: %s", PSB_GetErrorString(error));
        return ERR_COMM_FAILED;
    }
    
    double voltageDiff = fabs(status.voltage - ctx->params.chargeVoltage);
    
    LogMessage("Battery voltage: %.3f V, Expected: %.3f V, Difference: %.3f V", 
               status.voltage, ctx->params.chargeVoltage, voltageDiff);
    
    if (voltageDiff > CAPACITY_EXPERIMENT_VOLTAGE_MARGIN) {
        snprintf(message, sizeof(message),
            "Battery may not be fully charged:\n\n"
            "Measured Voltage: %.3f V\n"
            "Expected Voltage: %.3f V\n"
            "Difference: %.3f V\n"
            "Error Margin: %.3f V\n\n"
            "Do you want to continue anyway?",
            status.voltage,
            ctx->params.chargeVoltage,
            voltageDiff,
            CAPACITY_EXPERIMENT_VOLTAGE_MARGIN);
        
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        
        int response = ConfirmPopup("Battery Not Fully Charged", message);
        if (!response || ctx->state == CAPACITY_STATE_CANCELLED) {
            LogMessage("User cancelled due to battery not being fully charged");
            return ERR_CANCELLED;
        }
    }
    
    LogMessage("Battery charge state verified");
    return SUCCESS;
}

static int ConfigureGraphs(CapacityExperimentContext *ctx) {
    // Configure Graph 1 - Current vs Time
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph1Handle, 
                   "Current vs Time", "Time (s)", "Current (A)", 
                   0.0, maxCurrent * 1.1);
    
    // Configure Graph 2 - Voltage vs Time
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // Clear any existing plots
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 2);
    
    return SUCCESS;
}

static int RunExperimentPhase(CapacityExperimentContext *ctx, CapacityExperimentPhase phase) {
    char filename[MAX_PATH_LENGTH];
    CapacityDataPoint dataPoint;
    PhaseResults *phaseResults;
    int result;
    
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Determine phase parameters
    const char *phaseName = (phase == CAPACITY_PHASE_DISCHARGE) ? "discharge" : "charge";
    double targetVoltage = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                          ctx->params.dischargeVoltage : ctx->params.chargeVoltage;
    double targetCurrent = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                          ctx->params.dischargeCurrent : ctx->params.chargeCurrent;
    int capacityControl = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                         CAPACITY_NUM_DISCHARGE_CAP : CAPACITY_NUM_CHARGE_CAP;
    const char *csvFileName = (phase == CAPACITY_PHASE_DISCHARGE) ?
                             CAPACITY_EXPERIMENT_DISCHARGE_FILE : CAPACITY_EXPERIMENT_CHARGE_FILE;
    
    // Get pointer to phase results
    phaseResults = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                   &ctx->dischargeResults : &ctx->chargeResults;
    
    // Initialize phase results
    memset(phaseResults, 0, sizeof(PhaseResults));
    
    // Update state
    ctx->state = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                CAPACITY_STATE_DISCHARGING : CAPACITY_STATE_CHARGING;
    ctx->capacityControl = capacityControl;
    
    // Open CSV file
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, csvFileName);
    
    ctx->csvFile = fopen(filename, "w");
    if (!ctx->csvFile) {
        LogError("Failed to create %s file", filename);
        return ERR_BASE_FILE;
    }
    
    // Write CSV header
    fprintf(ctx->csvFile, "Time_s,Voltage_V,Current_A,Power_W\n");
    
    // Set target voltage and current
    result = PSB_SetVoltageQueued(targetVoltage, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    if (phase == CAPACITY_PHASE_DISCHARGE) {
        result = PSB_SetSinkCurrentQueued(targetCurrent, DEVICE_PRIORITY_NORMAL);
    } else {
        result = PSB_SetCurrentQueued(targetCurrent, DEVICE_PRIORITY_NORMAL);
    }
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    // Enable PSB output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    LogMessage("Waiting for output to stabilize...");
    
    // Check for cancellation during stabilization
    for (int i = 0; i < 20; i++) {
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            fclose(ctx->csvFile);
            ctx->csvFile = NULL;
            return ERR_CANCELLED;
        }
        Delay(0.1);
    }
    
    // Initialize timing and capacity
    ctx->phaseStartTime = GetTimestamp();
    ctx->lastLogTime = ctx->phaseStartTime;
    ctx->lastGraphUpdate = ctx->phaseStartTime;
    ctx->accumulatedCapacity_mAh = 0.0;
    ctx->accumulatedEnergy_Wh = 0.0;
    ctx->lastCurrent = 0.0;
    ctx->lastTime = 0.0;
    ctx->dataPointCount = 0;
    
    // Reset capacity display
    SetCtrlVal(ctx->tabPanelHandle, capacityControl, 0.0);
    
    // Get initial status for start voltage
    PSB_Status status;
    result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
    if (result == PSB_SUCCESS) {
        phaseResults->startVoltage = status.voltage;
    }
    
    LogMessage("%s phase started", phaseName);
    
    // Main experiment loop
    while (1) {
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            LogMessage("%s phase cancelled by user", phaseName);
            break;
        }
        
        double currentTime = GetTimestamp();
        double elapsedTime = currentTime - ctx->phaseStartTime;
        
        // Check for timeout
        if (elapsedTime > (CAPACITY_EXPERIMENT_MAX_DURATION_H * 3600.0)) {
            LogWarning("%s phase timeout reached", phaseName);
            break;
        }
        
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            LogError("Failed to read status: %s", PSB_GetErrorString(result));
            break;
        }
        
        // Check current threshold
        if (fabs(status.current) < ctx->params.currentThreshold) {
            LogMessage("%s phase completed - current below threshold (%.3f A < %.3f A)",
                      phaseName, fabs(status.current), ctx->params.currentThreshold);
            phaseResults->endVoltage = status.voltage;
            break;
        }
        
        // Log data point if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            dataPoint.time = elapsedTime;
            dataPoint.voltage = status.voltage;
            dataPoint.current = status.current;
            dataPoint.power = status.power;
            
            LogDataPoint(ctx, &dataPoint);
            ctx->lastLogTime = currentTime;
        }
        
        // Update graphs if needed
        if ((currentTime - ctx->lastGraphUpdate) >= CAPACITY_EXPERIMENT_GRAPH_UPDATE_RATE) {
            dataPoint.time = elapsedTime;
            dataPoint.voltage = status.voltage;
            dataPoint.current = status.current;
            dataPoint.power = status.power;
            
            UpdateGraphs(ctx, &dataPoint);
            ctx->lastGraphUpdate = currentTime;
        }
        
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Store final results
    phaseResults->capacity_mAh = ctx->accumulatedCapacity_mAh;
    phaseResults->energy_Wh = ctx->accumulatedEnergy_Wh;
    phaseResults->duration_s = GetTimestamp() - ctx->phaseStartTime;
    
    // Calculate averages
    if (phaseResults->dataPoints > 0) {
        phaseResults->avgCurrent = phaseResults->sumCurrent / phaseResults->dataPoints;
        phaseResults->avgVoltage = phaseResults->sumVoltage / phaseResults->dataPoints;
    }
    
    // Close CSV file
    fclose(ctx->csvFile);
    ctx->csvFile = NULL;
    
    LogMessage("%s phase completed - Capacity: %.2f mAh, Energy: %.2f Wh", 
              phaseName, phaseResults->capacity_mAh, phaseResults->energy_Wh);
    
    return (ctx->state == CAPACITY_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static int LogDataPoint(CapacityExperimentContext *ctx, CapacityDataPoint *point) {
    // Write to CSV
    fprintf(ctx->csvFile, "%.3f,%.3f,%.3f,%.3f\n", 
            point->time, point->voltage, point->current, point->power);
    fflush(ctx->csvFile);
    
    // Calculate increments
    if (ctx->dataPointCount > 0) {
        double deltaTime = point->time - ctx->lastTime;
        
        // Capacity increment using battery_utils function
        double capacityIncrement = Battery_CalculateCapacityIncrement(
            fabs(ctx->lastCurrent), fabs(point->current), deltaTime);
        
        // Energy increment (Wh) = average power * time / 3600
        double avgPower = (ctx->lastCurrent * point->voltage + point->current * ctx->lastCurrent) / 2.0;
        double energyIncrement = fabs(avgPower) * deltaTime / 3600.0;
        
        ctx->accumulatedCapacity_mAh += capacityIncrement;
        ctx->accumulatedEnergy_Wh += energyIncrement;
        
        // Update capacity display
        SetCtrlVal(ctx->tabPanelHandle, ctx->capacityControl, ctx->accumulatedCapacity_mAh);
        
        // Update phase results
        PhaseResults *results = (ctx->state == CAPACITY_STATE_DISCHARGING) ? 
                              &ctx->dischargeResults : &ctx->chargeResults;
        UpdatePhaseResults(ctx, results, point);
    }
    
    // Store for next calculation
    ctx->lastCurrent = point->current;
    ctx->lastTime = point->time;
    ctx->dataPointCount++;
    
    return SUCCESS;
}

static void UpdatePhaseResults(CapacityExperimentContext *ctx, PhaseResults *results, 
                               CapacityDataPoint *point) {
    results->dataPoints++;
    results->sumCurrent += fabs(point->current);
    results->sumVoltage += point->voltage;
}

static void UpdateGraphs(CapacityExperimentContext *ctx, CapacityDataPoint *point) {
    // Plot current and voltage points
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph1Handle, 
                  point->time, fabs(point->current), VAL_SOLID_CIRCLE, VAL_RED);
    
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
                  point->time, point->voltage, VAL_SOLID_CIRCLE, VAL_BLUE);
    
    // Auto-scale if needed
    double yMin, yMax;
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graph1Handle, VAL_LEFT_YAXIS, 
                       NULL, &yMin, &yMax);
    
    if (fabs(point->current) > yMax) {
        SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph1Handle, VAL_LEFT_YAXIS, 
                          VAL_AUTOSCALE, 0.0, 0.0);
    }
    
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_LEFT_YAXIS, 
                       NULL, &yMin, &yMax);
    
    if (point->voltage < yMin || point->voltage > yMax) {
        SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_LEFT_YAXIS, 
                          VAL_AUTOSCALE, 0.0, 0.0);
    }
}

static int WriteResultsFile(CapacityExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, CAPACITY_EXPERIMENT_RESULTS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create results file");
        return ERR_BASE_FILE;
    }
    
    // Get formatted timestamps
    time_t startTime = (time_t)ctx->experimentStartTime;
    time_t endTime = (time_t)ctx->experimentEndTime;
    char startTimeStr[64], endTimeStr[64];
    
    FormatTimestamp(startTime, startTimeStr, sizeof(startTimeStr));
    FormatTimestamp(endTime, endTimeStr, sizeof(endTimeStr));
    
    // Write header
    fprintf(file, "# Battery Capacity Experiment Results\n");
    fprintf(file, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
    
    // Experiment Information
    WriteINISection(file, "Experiment_Information");
    WriteINIValue(file, "Experiment_Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "Experiment_End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_s", ctx->experimentEndTime - ctx->experimentStartTime, 1);
    fprintf(file, "\n");
    
    // Experiment Parameters
    WriteINISection(file, "Experiment_Parameters");
    WriteINIDouble(file, "Charge_Voltage_V", ctx->params.chargeVoltage, 3);
    WriteINIDouble(file, "Discharge_Voltage_V", ctx->params.dischargeVoltage, 3);
    WriteINIDouble(file, "Charge_Current_A", ctx->params.chargeCurrent, 3);
    WriteINIDouble(file, "Discharge_Current_A", ctx->params.dischargeCurrent, 3);
    WriteINIDouble(file, "Current_Threshold_A", ctx->params.currentThreshold, 3);
    WriteINIValue(file, "Log_Interval_s", "%u", ctx->params.logInterval);
    fprintf(file, "\n");
    
    // Discharge Results
    WriteINISection(file, "Discharge_Results");
    WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->dischargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Discharge_Duration_s", ctx->dischargeResults.duration_s, 1);
    WriteINIDouble(file, "Discharge_Start_Voltage_V", ctx->dischargeResults.startVoltage, 3);
    WriteINIDouble(file, "Discharge_End_Voltage_V", ctx->dischargeResults.endVoltage, 3);
    WriteINIDouble(file, "Discharge_Average_Current_A", ctx->dischargeResults.avgCurrent, 3);
    WriteINIDouble(file, "Discharge_Average_Voltage_V", ctx->dischargeResults.avgVoltage, 3);
    WriteINIDouble(file, "Discharge_Energy_Wh", ctx->dischargeResults.energy_Wh, 3);
    WriteINIValue(file, "Discharge_Data_Points", "%d", ctx->dischargeResults.dataPoints);
    fprintf(file, "\n");
    
    // Charge Results
    WriteINISection(file, "Charge_Results");
    WriteINIDouble(file, "Charge_Capacity_mAh", ctx->chargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Charge_Duration_s", ctx->chargeResults.duration_s, 1);
    WriteINIDouble(file, "Charge_Start_Voltage_V", ctx->chargeResults.startVoltage, 3);
    WriteINIDouble(file, "Charge_End_Voltage_V", ctx->chargeResults.endVoltage, 3);
    WriteINIDouble(file, "Charge_Average_Current_A", ctx->chargeResults.avgCurrent, 3);
    WriteINIDouble(file, "Charge_Average_Voltage_V", ctx->chargeResults.avgVoltage, 3);
    WriteINIDouble(file, "Charge_Energy_Wh", ctx->chargeResults.energy_Wh, 3);
    WriteINIValue(file, "Charge_Data_Points", "%d", ctx->chargeResults.dataPoints);
    fprintf(file, "\n");
    
    // Calculated Metrics
    double coulombicEff = Battery_CalculateCoulombicEfficiency(ctx->chargeResults.capacity_mAh, 
                                                       ctx->dischargeResults.capacity_mAh);
    double energyEff = Battery_CalculateEnergyEfficiency(ctx->chargeResults.energy_Wh,
                                               ctx->dischargeResults.energy_Wh);
    
    WriteINISection(file, "Calculated_Metrics");
    WriteINIDouble(file, "Coulombic_Efficiency_Percent", coulombicEff, 1);
    WriteINIDouble(file, "Round_Trip_Energy_Efficiency_Percent", energyEff, 1);
    WriteINIDouble(file, "Capacity_Retention_Percent", coulombicEff, 1);
    
    fclose(file);
    
    LogMessage("Results written to: %s", filename);
    return SUCCESS;
}

static void RestoreUI(CapacityExperimentContext *ctx) {
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void CapacityExperiment_Cleanup(void) {
    if (CapacityExperiment_IsRunning()) {
        CapacityExperiment_Abort();
    }
}

int CapacityExperiment_Abort(void) {
    if (CapacityExperiment_IsRunning()) {
        g_experimentContext.state = CAPACITY_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}