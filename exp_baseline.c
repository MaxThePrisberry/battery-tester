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

// Main experiment thread
static int BaselineExperimentThread(void *functionData);

// Setup and verification functions
static int VerifyAllDevicesAndInitialize(BaselineExperimentContext *ctx);
static int CreateExperimentFileSystem(BaselineExperimentContext *ctx);
static int InitializeExperimentLogging(BaselineExperimentContext *ctx);
static int SaveExperimentSettings(BaselineExperimentContext *ctx);

// Phase implementation functions
static int RunPhase1_DischargeAndTemp(BaselineExperimentContext *ctx);
static int RunPhase2_CapacityExperiment(BaselineExperimentContext *ctx);
static int RunPhase3_EISCharge(BaselineExperimentContext *ctx);
static int RunPhase4_Discharge50Percent(BaselineExperimentContext *ctx);

// Temperature control functions
static int SetupTemperatureControl(BaselineExperimentContext *ctx);
static int WaitForTargetTemperature(BaselineExperimentContext *ctx);
static int StabilizeTemperature(BaselineExperimentContext *ctx);

// Device control functions
static int SetRelayState(int pin, int state);
static int SwitchToPSB(BaselineExperimentContext *ctx);
static int SwitchToBioLogic(BaselineExperimentContext *ctx);
static int SafeDisconnectAllDevices(BaselineExperimentContext *ctx);

// EIS measurement functions
static int InitializeEISTargets(BaselineExperimentContext *ctx);
static int AddDynamicSOCTarget(BaselineExperimentContext *ctx, double targetSOC);
static int PerformEISMeasurement(BaselineExperimentContext *ctx, double targetSOC);
static int RunOCVMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static int RunGEISMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static int ProcessGEISData(BIO_TechniqueData *geisData, BaselineEISMeasurement *measurement);
static int SaveEISMeasurementData(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static int RetryEISMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);

// Data logging and tracking functions
static int LogMainDataPoint(BaselineExperimentContext *ctx, BaselineDataPoint *point);
static int LogPhaseDataPoint(BaselineExperimentContext *ctx, const char *format, ...);
static int ReadAllTemperatures(BaselineExperimentContext *ctx, TemperatureDataPoint *tempData, double timestamp);
static int UpdateSOCTracking(BaselineExperimentContext *ctx, double voltage, double current);
static int UpdateCapacityEstimate(BaselineExperimentContext *ctx);

// Phase results management
static int InitializePhaseResults(BaselinePhaseResults *results, BaselineExperimentPhase phase);
static int UpdatePhaseResults(BaselinePhaseResults *results, BaselineDataPoint *dataPoint);
static int FinalizePhaseResults(BaselineExperimentContext *ctx, BaselinePhaseResults *results);
static int SavePhaseResults(BaselineExperimentContext *ctx, BaselinePhaseResults *results);

// Graph and UI functions
static int ConfigureExperimentGraphs(BaselineExperimentContext *ctx);
static void UpdateCurrentGraph(BaselineExperimentContext *ctx, double current, double time);
static void UpdateVoltageGraph(BaselineExperimentContext *ctx, double voltage, double time);
static void UpdateOCVGraph(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static void UpdateNyquistPlot(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement);
static void ClearAllExperimentGraphs(BaselineExperimentContext *ctx);

// Results and cleanup functions
static int WriteComprehensiveResults(BaselineExperimentContext *ctx);
static int WriteImportableResults(BaselineExperimentContext *ctx);
static void LogExperimentError(BaselineExperimentContext *ctx, const char *format, ...);
static void CleanupExperiment(BaselineExperimentContext *ctx);
static void RestoreUI(BaselineExperimentContext *ctx);

// Utility functions
static int CheckCancellation(BaselineExperimentContext *ctx);
static const char* GetPhaseDescription(BaselineExperimentPhase phase);
static const char* GetStateDescription(BaselineExperimentState state);

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
    g_experimentContext.emergencyStop = 0;
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
    
    // Preliminary validation
    if (g_experimentContext.params.targetTemperature < 5.0 || g_experimentContext.params.targetTemperature > 80.0) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Invalid Temperature", 
                     "Target temperature must be between 5°C and 80°C for safety.");
        return 0;
    }
    
    if (g_experimentContext.params.eisInterval <= 0 || g_experimentContext.params.eisInterval > 50) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Invalid EIS Interval", 
                     "EIS interval must be between 1% and 50% SOC.");
        return 0;
    }
    
    // Verify all required devices are connected and initialize
    int result = VerifyAllDevicesAndInitialize(&g_experimentContext);
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

int BaselineExperiment_Abort(void) {
    if (BaselineExperiment_IsRunning()) {
        LogMessage("Aborting baseline experiment...");
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = BASELINE_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

int BaselineExperiment_EmergencyStop(void) {
    if (BaselineExperiment_IsRunning()) {
        LogMessage("EMERGENCY STOP - Baseline experiment");
        g_experimentContext.emergencyStop = 1;
        g_experimentContext.cancelRequested = 1;
        g_experimentContext.state = BASELINE_STATE_ERROR;
        
        // Immediately disconnect all devices
        SafeDisconnectAllDevices(&g_experimentContext);
        
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}

void BaselineExperiment_Cleanup(void) {
    if (BaselineExperiment_IsRunning()) {
        BaselineExperiment_Abort();
    }
}

/******************************************************************************
 * Main Experiment Thread Implementation
 ******************************************************************************/

static int BaselineExperimentThread(void *functionData) {
    BaselineExperimentContext *ctx = (BaselineExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting Baseline Battery Experiment ===");
    
    // Record experiment start time
    ctx->experimentStartTime = Timer();
    
    // Check for early cancellation
    if (CheckCancellation(ctx)) {
        LogMessage("Baseline experiment cancelled before confirmation");
        goto cleanup;
    }
    
    // Show comprehensive confirmation popup
    snprintf(message, sizeof(message),
        "BASELINE BATTERY EXPERIMENT\n"
        "=============================\n\n"
        "PARAMETERS:\n"
        "• Target Temperature: %.1f °C\n"
        "• EIS Interval: %.1f%% SOC\n"
        "• Charge Voltage: %.2f V\n"
        "• Discharge Voltage: %.2f V\n"
        "• Charge Current: %.2f A\n"
        "• Discharge Current: %.2f A\n"
        "• Current Threshold: %.3f A\n"
        "• Log Interval: %d seconds\n\n"
        "EXPERIMENT PHASES:\n"
        "1. Discharge battery and establish temperature\n"
        "2. Capacity test (charge ? discharge)\n"
        "3. EIS measurements during charge\n"
        "4. Discharge to 50%% capacity\n\n"
        "ESTIMATED DURATION: 6-12 hours\n\n"
        "Continue with experiment?",
        ctx->params.targetTemperature,
        ctx->params.eisInterval,
        ctx->params.chargeVoltage,
        ctx->params.dischargeVoltage,
        ctx->params.chargeCurrent,
        ctx->params.dischargeCurrent,
        ctx->params.currentThreshold,
        ctx->params.logInterval);
    
    int response = ConfirmPopup("Confirm Baseline Experiment", message);
    if (!response || CheckCancellation(ctx)) {
        LogMessage("Baseline experiment cancelled by user");
        ctx->state = BASELINE_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create file system and initialize logging
    result = CreateExperimentFileSystem(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to create experiment file system");
        MessagePopup("Error", "Failed to create experiment directory.\nPlease check disk space and permissions.");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    result = InitializeExperimentLogging(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to initialize experiment logging");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    // Save experiment settings for future reference
    SaveExperimentSettings(ctx);
    
    // Configure graphs and UI
    ConfigureExperimentGraphs(ctx);
    
    // Initialize relay states (safety: both OFF)
    LogMessage("Initializing relay states...");
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to initialize PSB relay");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to initialize BioLogic relay");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    // Initialize EIS target SOCs
    result = InitializeEISTargets(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to initialize EIS targets");
        ctx->state = BASELINE_STATE_ERROR;
        goto cleanup;
    }
    
    // PHASE 1: Initial Discharge and Temperature Setup
    LogMessage("=== PHASE 1: Initial Discharge and Temperature Setup ===");
    ctx->currentPhase = BASELINE_PHASE_1;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 1: Discharging and establishing temperature...");
    
    result = RunPhase1_DischargeAndTemp(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 2: Capacity Experiment (Charge ? Discharge)
    LogMessage("=== PHASE 2: Capacity Experiment (Charge ? Discharge) ===");
    ctx->currentPhase = BASELINE_PHASE_2;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Running capacity experiment...");
    ClearAllExperimentGraphs(ctx);
    
    result = RunPhase2_CapacityExperiment(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 3: EIS Measurements During Charge
    LogMessage("=== PHASE 3: EIS Measurements During Charge ===");
    ctx->currentPhase = BASELINE_PHASE_3;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 3: EIS measurements during charge...");
    ClearAllExperimentGraphs(ctx);
    
    result = RunPhase3_EISCharge(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // PHASE 4: Discharge to 50% Capacity
    LogMessage("=== PHASE 4: Discharge to 50%% Capacity ===");
    ctx->currentPhase = BASELINE_PHASE_4;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 4: Discharging to 50% capacity...");
    ClearAllExperimentGraphs(ctx);
    
    result = RunPhase4_Discharge50Percent(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = BASELINE_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Record experiment completion
    ctx->experimentEndTime = Timer();
    ctx->state = BASELINE_STATE_COMPLETED;
    LogMessage("=== BASELINE EXPERIMENT COMPLETED SUCCESSFULLY ===");
    LogMessage("Total experiment time: %.1f hours", 
               (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0);
    
    // Write comprehensive results
    result = WriteComprehensiveResults(ctx);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to write comprehensive results");
    }
    
    result = WriteImportableResults(ctx);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to write importable results");
    }
    
cleanup:
    // Always perform cleanup
    CleanupExperiment(ctx);
    
    // Update final status
    const char *finalStatus;
    switch (ctx->state) {
        case BASELINE_STATE_COMPLETED:
            finalStatus = "Baseline experiment completed successfully";
            break;
        case BASELINE_STATE_CANCELLED:
            finalStatus = "Baseline experiment cancelled by user";
            break;
        case BASELINE_STATE_ERROR:
            finalStatus = ctx->emergencyStop ? "Baseline experiment emergency stopped" : 
                                               "Baseline experiment failed";
            break;
        default:
            finalStatus = "Baseline experiment ended unexpectedly";
            break;
    }
    
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, finalStatus);
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, finalStatus);
    
    // Restore button text and UI
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
 * Setup and Verification Functions
 ******************************************************************************/

static int VerifyAllDevicesAndInitialize(BaselineExperimentContext *ctx) {
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
    
    // Check DTB connection (REQUIRED for baseline)
    DTBQueueManager *dtbQueueMgr = DTB_GetGlobalQueueManager();
    if (!dtbQueueMgr) {
        MessagePopup("DTB Not Connected", 
                     "The DTB temperature controller is REQUIRED for baseline experiments.\n"
                     "Please ensure it is connected before running.");
        return ERR_NOT_CONNECTED;
    }
    
    DTB_Handle *dtbHandle = DTB_QueueGetHandle(dtbQueueMgr, ctx->dtbSlaveAddress);
    if (!dtbHandle || !dtbHandle->isConnected) {
        MessagePopup("DTB Not Connected", 
                     "The DTB temperature controller is REQUIRED for baseline experiments.\n"
                     "Please ensure it is connected before running.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check Teensy connection (for relay control)
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        MessagePopup("Teensy Not Connected", 
                     "The Teensy relay controller is not connected.\n"
                     "Please ensure it is connected before running the baseline experiment.");
        return ERR_NOT_CONNECTED;
    }
    
    // Verify PSB is in safe state
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
    
    LogMessage("All devices verified and initialized successfully");
    return SUCCESS;
}

static int CreateExperimentFileSystem(BaselineExperimentContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        strcpy(basePath, ".");
    }
    
    // Create main data directory
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, BASELINE_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamped experiment directory
    int result = CreateTimestampedDirectory(dataPath, "baseline", 
                                          ctx->experimentDirectory, sizeof(ctx->experimentDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create experiment directory");
        return result;
    }
    
    // Create phase subdirectories
    char phaseDir[MAX_PATH_LENGTH];
    const char *phaseDirs[] = {
        BASELINE_PHASE1_DIR,
        BASELINE_PHASE2_DIR,
        BASELINE_PHASE3_DIR,
        BASELINE_PHASE4_DIR,
        BASELINE_DIAGNOSTICS_DIR
    };
    
    for (int i = 0; i < sizeof(phaseDirs)/sizeof(phaseDirs[0]); i++) {
        snprintf(phaseDir, sizeof(phaseDir), "%s%s%s", 
                 ctx->experimentDirectory, PATH_SEPARATOR, phaseDirs[i]);
        
        if (CreateDirectoryPath(phaseDir) != SUCCESS) {
            LogError("Failed to create phase directory: %s", phaseDir);
            return ERR_BASE_FILE;
        }
    }
    
    // Create EIS measurements subdirectory
    snprintf(phaseDir, sizeof(phaseDir), "%s%s%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             BASELINE_PHASE3_DIR, PATH_SEPARATOR, BASELINE_PHASE3_EIS_DIR);
    
    if (CreateDirectoryPath(phaseDir) != SUCCESS) {
        LogError("Failed to create EIS measurements directory: %s", phaseDir);
        return ERR_BASE_FILE;
    }
    
    LogMessage("Created experiment file system: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int InitializeExperimentLogging(BaselineExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    
    // Open main experiment log
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_MAIN_LOG_FILE);
    
    ctx->mainLogFile = fopen(filename, "w");
    if (!ctx->mainLogFile) {
        LogError("Failed to create main experiment log: %s", filename);
        return ERR_BASE_FILE;
    }
    
    // Write main log header
    fprintf(ctx->mainLogFile, "# Baseline Battery Experiment Log\n");
    fprintf(ctx->mainLogFile, "# Generated by Battery Tester v%s\n", PROJECT_VERSION);
    fprintf(ctx->mainLogFile, "Timestamp_s,Phase,State,Voltage_V,Current_A,Power_W,SOC_Percent,"
                             "DTB_Temp_C,TC0_Temp_C,TC1_Temp_C,Description\n");
    fflush(ctx->mainLogFile);
    
    // Open error log
    snprintf(filename, sizeof(filename), "%s%s%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             BASELINE_DIAGNOSTICS_DIR, PATH_SEPARATOR, BASELINE_ERROR_LOG_FILE);
    
    ctx->errorLogFile = fopen(filename, "w");
    if (!ctx->errorLogFile) {
        LogError("Failed to create error log: %s", filename);
        // Non-critical, continue without error log
    } else {
        fprintf(ctx->errorLogFile, "# Baseline Experiment Error Log\n");
        fprintf(ctx->errorLogFile, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
        fflush(ctx->errorLogFile);
    }
    
    LogMessage("Experiment logging initialized");
    return SUCCESS;
}

static int SaveExperimentSettings(BaselineExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_SETTINGS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create settings file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    fprintf(file, "# Baseline Experiment Settings\n");
    fprintf(file, "# Created: %s\n", timeStr);
    fprintf(file, "# Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Experiment_Parameters");
    WriteINIDouble(file, "Target_Temperature_C", ctx->params.targetTemperature, 1);
    WriteINIDouble(file, "EIS_Interval_Percent", ctx->params.eisInterval, 1);
    WriteINIDouble(file, "Current_Threshold_A", ctx->params.currentThreshold, 3);
    WriteINIValue(file, "Log_Interval_s", "%u", ctx->params.logInterval);
    WriteINIDouble(file, "Charge_Voltage_V", ctx->params.chargeVoltage, 3);
    WriteINIDouble(file, "Discharge_Voltage_V", ctx->params.dischargeVoltage, 3);
    WriteINIDouble(file, "Charge_Current_A", ctx->params.chargeCurrent, 3);
    WriteINIDouble(file, "Discharge_Current_A", ctx->params.dischargeCurrent, 3);
    fprintf(file, "\n");
    
    WriteINISection(file, "Device_Configuration");
    WriteINIValue(file, "DTB_Slave_Address", "%d", ctx->dtbSlaveAddress);
    WriteINIValue(file, "BioLogic_Device_ID", "%d", ctx->biologicID);
    fprintf(file, "\n");
    
    WriteINISection(file, "EIS_Configuration");
    WriteINIDouble(file, "OCV_Duration_s", OCV_DURATION_S, 1);
    WriteINIDouble(file, "GEIS_Initial_Freq_Hz", GEIS_INITIAL_FREQ, 0);
    WriteINIDouble(file, "GEIS_Final_Freq_Hz", GEIS_FINAL_FREQ, 1);
    WriteINIValue(file, "GEIS_Freq_Points", "%d", GEIS_FREQ_NUMBER);
    WriteINIDouble(file, "GEIS_Amplitude_A", GEIS_AMPLITUDE_I, 3);
    
    fclose(file);
    
    LogMessage("Experiment settings saved to: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Phase Implementation Functions
 ******************************************************************************/

static int RunPhase1_DischargeAndTemp(BaselineExperimentContext *ctx) {
    int result;
    
    ctx->state = BASELINE_STATE_PHASE1_DISCHARGE;
    InitializePhaseResults(&ctx->phase1Results, BASELINE_PHASE_1);
    
    // Create phase directory path
    snprintf(ctx->currentPhaseDirectory, sizeof(ctx->currentPhaseDirectory), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE1_DIR);
    strcpy(ctx->phase1Results.phaseDirectory, ctx->currentPhaseDirectory);
    
    // Setup temperature control first
    result = SetupTemperatureControl(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to setup temperature control in Phase 1");
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Open phase log file
    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->currentPhaseDirectory, PATH_SEPARATOR, BASELINE_PHASE1_DISCHARGE_FILE);
    
    ctx->currentPhaseLogFile = fopen(filename, "w");
    if (!ctx->currentPhaseLogFile) {
        LogExperimentError(ctx, "Failed to create Phase 1 log file");
        return ERR_BASE_FILE;
    }
    
    fprintf(ctx->currentPhaseLogFile, "Time_s,Voltage_V,Current_A,Power_W,DTB_Temp_C,TC0_Temp_C,TC1_Temp_C\n");
    
    // Switch to PSB for discharge
    result = SwitchToPSB(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        fclose(ctx->currentPhaseLogFile);
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Use battery_utils for discharge with graph updates
    VoltageTargetParams dischargeParams = {
        .targetVoltage_V = ctx->params.dischargeVoltage,
        .maxCurrent_A = ctx->params.dischargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 7200.0,  // 2 hours max
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
    
    LogMessage("Discharging battery to %.2f V", ctx->params.dischargeVoltage);
    ctx->phaseStartTime = Timer() - ctx->experimentStartTime;
    
    result = Battery_GoToVoltage(&dischargeParams);
    
    if (CheckCancellation(ctx)) {
        fclose(ctx->currentPhaseLogFile);
        return ERR_CANCELLED;
    }
    
    if (result != SUCCESS || dischargeParams.result != BATTERY_OP_SUCCESS) {
        if (dischargeParams.result == BATTERY_OP_ABORTED) {
            fclose(ctx->currentPhaseLogFile);
            return ERR_CANCELLED;
        }
        LogExperimentError(ctx, "Phase 1 discharge failed");
        fclose(ctx->currentPhaseLogFile);
        return ERR_OPERATION_FAILED;
    }
    
    // Store discharge results
    ctx->phase1Results.capacity_mAh = fabs(dischargeParams.actualCapacity_mAh);
    ctx->phase1Results.energy_Wh = fabs(dischargeParams.actualEnergy_Wh);
    ctx->phase1Results.startVoltage = dischargeParams.startVoltage_V;
    ctx->phase1Results.endVoltage = dischargeParams.finalVoltage_V;
    
    LogMessage("Phase 1 discharge completed: %.2f mAh in %.1f minutes", 
               ctx->phase1Results.capacity_mAh, dischargeParams.elapsedTime_s / 60.0);
    
    // Update output display
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->phase1Results.capacity_mAh);
    
    fclose(ctx->currentPhaseLogFile);
    ctx->currentPhaseLogFile = NULL;
    
    // Wait for target temperature
    ctx->state = BASELINE_STATE_PHASE1_TEMP_WAIT;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 1: Waiting for target temperature...");
    
    result = WaitForTargetTemperature(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Stabilize temperature
    ctx->state = BASELINE_STATE_PHASE1_TEMP_STABILIZE;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 1: Stabilizing temperature...");
    
    result = StabilizeTemperature(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Finalize phase results
    FinalizePhaseResults(ctx, &ctx->phase1Results);
    SavePhaseResults(ctx, &ctx->phase1Results);
    
    LogMessage("Phase 1 completed successfully");
    return SUCCESS;
}

static int RunPhase2_CapacityExperiment(BaselineExperimentContext *ctx) {
    int result;
    
    ctx->state = BASELINE_STATE_PHASE2_CHARGE;
    InitializePhaseResults(&ctx->phase2ChargeResults, BASELINE_PHASE_2);
    InitializePhaseResults(&ctx->phase2DischargeResults, BASELINE_PHASE_2);
    
    // Create phase directory path
    snprintf(ctx->currentPhaseDirectory, sizeof(ctx->currentPhaseDirectory), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE2_DIR);
    strcpy(ctx->phase2ChargeResults.phaseDirectory, ctx->currentPhaseDirectory);
    strcpy(ctx->phase2DischargeResults.phaseDirectory, ctx->currentPhaseDirectory);
    
    // Reconfigure graphs for capacity experiment
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // --- CHARGE PHASE ---
    LogMessage("Running Phase 2 charge phase...");
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Charging battery...");
    
    // Use battery_utils for charge (no EIS interruptions in Phase 2)
    VoltageTargetParams chargeParams = {
        .targetVoltage_V = ctx->params.chargeVoltage,
        .maxCurrent_A = ctx->params.chargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 7200.0,  // 2 hours max
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
    
    ctx->phaseStartTime = Timer() - ctx->experimentStartTime;
    result = Battery_GoToVoltage(&chargeParams);
    
    if (CheckCancellation(ctx)) {
        return ERR_CANCELLED;
    }
    
    if (result != SUCCESS || chargeParams.result != BATTERY_OP_SUCCESS) {
        if (chargeParams.result == BATTERY_OP_ABORTED) {
            return ERR_CANCELLED;
        }
        LogExperimentError(ctx, "Phase 2 charge failed");
        return ERR_OPERATION_FAILED;
    }
    
    // Store charge results
    ctx->phase2ChargeResults.capacity_mAh = chargeParams.actualCapacity_mAh;
    ctx->phase2ChargeResults.energy_Wh = chargeParams.actualEnergy_Wh;
    ctx->phase2ChargeResults.duration = chargeParams.elapsedTime_s;
    ctx->phase2ChargeResults.startVoltage = chargeParams.startVoltage_V;
    ctx->phase2ChargeResults.endVoltage = chargeParams.finalVoltage_V;
    ctx->measuredChargeCapacity_mAh = chargeParams.actualCapacity_mAh;
    
    LogMessage("Phase 2 charge completed: %.2f mAh, %.2f Wh in %.1f minutes", 
               ctx->phase2ChargeResults.capacity_mAh, ctx->phase2ChargeResults.energy_Wh, 
               ctx->phase2ChargeResults.duration / 60.0);
    
    // Update output display
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->phase2ChargeResults.capacity_mAh);
    
    // Brief pause between charge and discharge
    LogMessage("Pausing between charge and discharge phases...");
    for (int i = 0; i < 30; i++) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        Delay(0.1);
    }
    
    // --- DISCHARGE PHASE ---
    LogMessage("Running Phase 2 discharge phase...");
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 2: Discharging battery...");
    ctx->state = BASELINE_STATE_PHASE2_DISCHARGE;
    
    VoltageTargetParams dischargeParams = {
        .targetVoltage_V = ctx->params.dischargeVoltage,
        .maxCurrent_A = ctx->params.dischargeCurrent,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 7200.0,  // 2 hours max
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
    
    if (CheckCancellation(ctx)) {
        return ERR_CANCELLED;
    }
    
    if (result != SUCCESS || dischargeParams.result != BATTERY_OP_SUCCESS) {
        LogExperimentError(ctx, "Phase 2 discharge failed");
        return ERR_OPERATION_FAILED;
    }
    
    // Store discharge results
    ctx->phase2DischargeResults.capacity_mAh = fabs(dischargeParams.actualCapacity_mAh);
    ctx->phase2DischargeResults.energy_Wh = fabs(dischargeParams.actualEnergy_Wh);
    ctx->phase2DischargeResults.duration = dischargeParams.elapsedTime_s;
    ctx->phase2DischargeResults.startVoltage = dischargeParams.startVoltage_V;
    ctx->phase2DischargeResults.endVoltage = dischargeParams.finalVoltage_V;
    ctx->measuredDischargeCapacity_mAh = fabs(dischargeParams.actualCapacity_mAh);
    
    LogMessage("Phase 2 discharge completed: %.2f mAh, %.2f Wh in %.1f minutes", 
               ctx->phase2DischargeResults.capacity_mAh, ctx->phase2DischargeResults.energy_Wh, 
               ctx->phase2DischargeResults.duration / 60.0);
    
    // Update output display
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->phase2DischargeResults.capacity_mAh);
    
    // Update battery capacity estimate for Phase 3
    ctx->estimatedBatteryCapacity_mAh = ctx->measuredChargeCapacity_mAh;
    
    // Finalize phase results
    FinalizePhaseResults(ctx, &ctx->phase2ChargeResults);
    FinalizePhaseResults(ctx, &ctx->phase2DischargeResults);
    SavePhaseResults(ctx, &ctx->phase2ChargeResults);
    SavePhaseResults(ctx, &ctx->phase2DischargeResults);
    
    // Write Phase 2 capacity results file
    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->currentPhaseDirectory, PATH_SEPARATOR, BASELINE_PHASE2_RESULTS_FILE);
    
    FILE *file = fopen(filename, "w");
    if (file) {
        WriteINISection(file, "Phase2_Capacity_Results");
        WriteINIDouble(file, "Charge_Capacity_mAh", ctx->phase2ChargeResults.capacity_mAh, 2);
        WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->phase2DischargeResults.capacity_mAh, 2);
        WriteINIDouble(file, "Charge_Energy_Wh", ctx->phase2ChargeResults.energy_Wh, 3);
        WriteINIDouble(file, "Discharge_Energy_Wh", ctx->phase2DischargeResults.energy_Wh, 3);
        WriteINIDouble(file, "Coulombic_Efficiency_Percent", 
                      Battery_CalculateCoulombicEfficiency(ctx->phase2ChargeResults.capacity_mAh, 
                                                          ctx->phase2DischargeResults.capacity_mAh), 1);
        WriteINIDouble(file, "Energy_Efficiency_Percent", 
                      Battery_CalculateEnergyEfficiency(ctx->phase2ChargeResults.energy_Wh,
                                                       ctx->phase2DischargeResults.energy_Wh), 1);
        fclose(file);
    }
    
    LogMessage("Phase 2 completed successfully");
    return SUCCESS;
}

static int RunPhase3_EISCharge(BaselineExperimentContext *ctx) {
    int result;
    char filename[MAX_PATH_LENGTH];
    
    ctx->state = BASELINE_STATE_PHASE3_SETUP;
    InitializePhaseResults(&ctx->phase3Results, BASELINE_PHASE_3);
    
    // Create phase directory path
    snprintf(ctx->currentPhaseDirectory, sizeof(ctx->currentPhaseDirectory), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE3_DIR);
    strcpy(ctx->phase3Results.phaseDirectory, ctx->currentPhaseDirectory);
    
    // Reconfigure graphs for EIS phase
    ClearAllExperimentGraphs(ctx);
    
    // Configure Graph 2 for OCV vs SOC
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, "OCV vs SOC", "SOC (%)", "OCV (V)",
                   ctx->params.dischargeVoltage * 0.9, ctx->params.chargeVoltage * 1.1);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_BOTTOM_XAXIS, 
                       VAL_MANUAL, 0.0, 150.0);  // Allow for >100% SOC
    
    // Configure Nyquist Plot
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, "Nyquist Plot");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_XNAME, "Z' (Ohms)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_YNAME, "-Z'' (Ohms)");
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphBiologicHandle, VAL_LEFT_YAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    // Perform initial EIS measurement at 0% SOC
    LogMessage("Performing initial EIS measurement at 0%% SOC...");
    result = PerformEISMeasurement(ctx, 0.0);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Initial EIS measurement failed");
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Start charging phase with EIS interruptions
    LogMessage("Starting Phase 3 charging with EIS measurements...");
    ctx->state = BASELINE_STATE_PHASE3_CHARGING;
    
    // Open phase charge log file
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->currentPhaseDirectory, PATH_SEPARATOR, BASELINE_PHASE3_CHARGE_FILE);
    
    ctx->currentPhaseLogFile = fopen(filename, "w");
    if (!ctx->currentPhaseLogFile) {
        LogExperimentError(ctx, "Failed to create Phase 3 log file");
        return ERR_BASE_FILE;
    }
    
    fprintf(ctx->currentPhaseLogFile, "Time_s,Voltage_V,Current_A,Power_W,SOC_Percent,DTB_Temp_C,TC0_Temp_C,TC1_Temp_C\n");
    
    // Switch to PSB for charging
    result = SwitchToPSB(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        fclose(ctx->currentPhaseLogFile);
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Configure PSB for charging
    result = PSB_SetVoltageQueued(ctx->params.chargeVoltage, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogExperimentError(ctx, "Failed to set charge voltage: %s", PSB_GetErrorString(result));
        fclose(ctx->currentPhaseLogFile);
        return result;
    }
    
    result = PSB_SetCurrentQueued(ctx->params.chargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogExperimentError(ctx, "Failed to set charge current: %s", PSB_GetErrorString(result));
        fclose(ctx->currentPhaseLogFile);
        return result;
    }
    
    result = PSB_SetPowerQueued(BASELINE_POWER_LIMIT, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
    
    // Enable PSB output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogExperimentError(ctx, "Failed to enable output: %s", PSB_GetErrorString(result));
        fclose(ctx->currentPhaseLogFile);
        return result;
    }
    
    // Wait for output stabilization
    LogMessage("Waiting for PSB output to stabilize...");
    Delay(2.0);
    
    // Initialize SOC tracking and timing
    ctx->phaseStartTime = Timer() - ctx->experimentStartTime;
    ctx->lastLogTime = Timer();
    ctx->lastGraphUpdate = Timer();
    ctx->currentSOC = 0.0;
    ctx->accumulatedCapacity_mAh = 0.0;
    ctx->lastCurrent = 0.0;
    ctx->lastTime = 0.0;
    
    int nextTargetIndex = 1;  // Skip 0% as we already measured it
    int lowCurrentReadings = 0;
    const int MIN_LOW_CURRENT_READINGS = 5;
    
    LogMessage("Phase 3 charging started with EIS interruptions at %.1f%% SOC intervals", 
               ctx->params.eisInterval);
    
    // Main charging loop with EIS interruptions
    while (1) {
        if (CheckCancellation(ctx)) {
            LogMessage("Phase 3 cancelled by user");
            break;
        }
        
        // Check for emergency stop
        if (ctx->emergencyStop) {
            LogMessage("Phase 3 emergency stopped");
            break;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - ctx->experimentStartTime - ctx->phaseStartTime;
        
        // Safety timeout
        if (elapsedTime > BASELINE_MAX_EXPERIMENT_TIME / 2) {  // Half total experiment time
            LogExperimentError(ctx, "Phase 3 timeout - charging too long");
            break;
        }
        
        // Get current PSB status
        PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
        if (result != PSB_SUCCESS) {
            LogExperimentError(ctx, "Failed to read PSB status: %s", PSB_GetErrorString(result));
            break;
        }
        
        // Update SOC tracking
        UpdateSOCTracking(ctx, status.voltage, status.current);
        UpdateCapacityEstimate(ctx);
        
        // Check for SOC safety limit
        if (ctx->currentSOC > BASELINE_SOC_OVERCHARGE_LIMIT) {
            LogExperimentError(ctx, "Safety limit reached - SOC exceeds %.1f%%", BASELINE_SOC_OVERCHARGE_LIMIT);
            break;
        }
        
        // Log data if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            TemperatureDataPoint tempData;
            ReadAllTemperatures(ctx, &tempData, elapsedTime);
            
            LogPhaseDataPoint(ctx, "%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f", 
                            elapsedTime, status.voltage, status.current, status.power, ctx->currentSOC,
                            tempData.dtbTemperature, tempData.tc0Temperature, tempData.tc1Temperature);
            
            ctx->lastLogTime = currentTime;
            
            // Update SOC display
            SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->currentSOC);
        }
        
        // Update graphs if needed
        if ((currentTime - ctx->lastGraphUpdate) >= 1.0) {
            UpdateCurrentGraph(ctx, status.current, elapsedTime);
            ctx->lastGraphUpdate = currentTime;
        }
        
        // Check if we need to perform EIS measurement
        if (nextTargetIndex < ctx->numTargetSOCs && 
            ctx->currentSOC >= (ctx->targetSOCs[nextTargetIndex] - BASELINE_SOC_TOLERANCE)) {
            
            LogMessage("EIS target SOC %.1f%% reached (actual: %.1f%%)", 
                      ctx->targetSOCs[nextTargetIndex], ctx->currentSOC);
            
            // Disable PSB output
            PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
            
            // Perform EIS measurement
            ctx->state = BASELINE_STATE_PHASE3_EIS_MEASUREMENT;
            result = PerformEISMeasurement(ctx, ctx->targetSOCs[nextTargetIndex]);
            
            if (CheckCancellation(ctx)) {
                break;
            }
            
            if (result != SUCCESS) {
                LogExperimentError(ctx, "EIS measurement failed at %.1f%% SOC", ctx->currentSOC);
                // Continue with charging despite EIS failure
            }
            
            nextTargetIndex++;
            
            // Check if we need to add dynamic targets
            if (nextTargetIndex >= ctx->numTargetSOCs) {
                if (ctx->dynamicTargetsAdded < BASELINE_MAX_DYNAMIC_TARGETS) {
                    double nextTarget = ctx->targetSOCs[ctx->numTargetSOCs - 1] + ctx->params.eisInterval;
                    if (nextTarget <= BASELINE_SOC_OVERCHARGE_LIMIT) {
                        result = AddDynamicSOCTarget(ctx, nextTarget);
                        if (result == SUCCESS) {
                            ctx->dynamicTargetsAdded++;
                            if (ctx->dynamicTargetsAdded == 1) {
                                LogMessage("Battery capacity underestimated - adding dynamic EIS targets beyond 100%% SOC");
                            }
                        }
                    }
                }
            }
            
            // Resume charging
            ctx->state = BASELINE_STATE_PHASE3_CHARGING;
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Phase 3: Resuming charge after EIS...");
            
            LogMessage("Resuming charging after EIS measurement...");
            result = SwitchToPSB(ctx);
            if (result != SUCCESS) break;
            
            PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
            Delay(1.0);  // Stabilization time
            
            // Reset low current counter
            lowCurrentReadings = 0;
            
            // Reset time tracking to avoid large jumps
            ctx->lastTime = 0.0;
        }
        
        // Check for charge completion with debouncing
        if (fabs(status.current) < ctx->params.currentThreshold) {
            lowCurrentReadings++;
            if (lowCurrentReadings >= MIN_LOW_CURRENT_READINGS) {
                LogMessage("Phase 3 charging completed - current below threshold for %d consecutive readings", 
                          MIN_LOW_CURRENT_READINGS);
                LogMessage("Final SOC: %.1f%%, Final current: %.3f A", ctx->currentSOC, fabs(status.current));
                
                // Perform final EIS measurement if needed
                PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
                
                // Check if we need a final measurement
                int needFinalMeasurement = 1;
                if (ctx->eisMeasurementCount > 0) {
                    double lastMeasuredSOC = ctx->eisMeasurements[ctx->eisMeasurementCount - 1].actualSOC;
                    if (fabs(ctx->currentSOC - lastMeasuredSOC) < BASELINE_SOC_TOLERANCE * 2) {
                        needFinalMeasurement = 0;
                    }
                }
                
                if (needFinalMeasurement) {
                    LogMessage("Taking final EIS measurement at %.1f%% SOC", ctx->currentSOC);
                    result = AddDynamicSOCTarget(ctx, ctx->currentSOC);
                    if (result == SUCCESS) {
                        ctx->state = BASELINE_STATE_PHASE3_EIS_MEASUREMENT;
                        result = PerformEISMeasurement(ctx, ctx->currentSOC);
                    }
                }
                
                break;
            }
        } else {
            if (lowCurrentReadings > 0) {
                lowCurrentReadings = 0;  // Reset counter
            }
        }
        
        // Brief delay to prevent excessive CPU usage
        ProcessSystemEvents();
        Delay(0.5);
    }
    
    // Ensure output is disabled
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Close phase log file
    fclose(ctx->currentPhaseLogFile);
    ctx->currentPhaseLogFile = NULL;
    
    // Write OCV vs SOC data file
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->currentPhaseDirectory, PATH_SEPARATOR, BASELINE_PHASE3_OCV_FILE);
    
    FILE *ocvFile = fopen(filename, "w");
    if (ocvFile) {
        fprintf(ocvFile, "SOC_Percent,OCV_V,Timestamp_s,Temperature_C\n");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            BaselineEISMeasurement *m = &ctx->eisMeasurements[i];
            fprintf(ocvFile, "%.2f,%.4f,%.1f,%.2f\n", 
                    m->actualSOC, m->ocvVoltage, m->timestamp, m->tempData.dtbTemperature);
        }
        fclose(ocvFile);
        LogMessage("OCV vs SOC data written to: %s", filename);
    }
    
    // Finalize phase results
    ctx->phase3Results.capacity_mAh = ctx->accumulatedCapacity_mAh;
    ctx->phase3Results.endVoltage = ctx->eisMeasurementCount > 0 ? 
                                   ctx->eisMeasurements[ctx->eisMeasurementCount - 1].ocvVoltage : 0.0;
    
    FinalizePhaseResults(ctx, &ctx->phase3Results);
    SavePhaseResults(ctx, &ctx->phase3Results);
    
    if (ctx->dynamicTargetsAdded > 0) {
        LogMessage("Phase 3 completed - battery capacity was underestimated, took %d measurements beyond 100%% SOC", 
                  ctx->dynamicTargetsAdded);
    } else {
        LogMessage("Phase 3 completed successfully");
    }
    
    return CheckCancellation(ctx) ? ERR_CANCELLED : SUCCESS;
}

static int RunPhase4_Discharge50Percent(BaselineExperimentContext *ctx) {
    if (ctx->measuredChargeCapacity_mAh <= 0) {
        LogExperimentError(ctx, "Cannot discharge to 50%% - charge capacity unknown");
        return ERR_INVALID_PARAMETER;
    }
    
    ctx->state = BASELINE_STATE_PHASE4_DISCHARGE;
    InitializePhaseResults(&ctx->phase4Results, BASELINE_PHASE_4);
    
    // Create phase directory path
    snprintf(ctx->currentPhaseDirectory, sizeof(ctx->currentPhaseDirectory), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_PHASE4_DIR);
    strcpy(ctx->phase4Results.phaseDirectory, ctx->currentPhaseDirectory);
    
    LogMessage("=== Phase 4: Discharging battery to 50%% capacity ===");
    LogMessage("Target discharge: %.2f mAh (50%% of %.2f mAh)", 
               ctx->measuredChargeCapacity_mAh * 0.5, ctx->measuredChargeCapacity_mAh);
    
    // Reconfigure graphs for discharge
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    // Switch to PSB for discharge
    int result = SwitchToPSB(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogExperimentError(ctx, "Failed to switch to PSB for Phase 4 discharge");
        return result != SUCCESS ? result : ERR_CANCELLED;
    }
    
    // Use battery_utils for precise capacity discharge
    CapacityTransferParams discharge50 = {
        .mode = BATTERY_MODE_DISCHARGE,
        .targetCapacity_mAh = ctx->measuredChargeCapacity_mAh * 0.5,
        .current_A = ctx->params.dischargeCurrent,
        .voltage_V = ctx->params.dischargeVoltage,
        .currentThreshold_A = ctx->params.currentThreshold,
        .timeoutSeconds = 3600.0,  // 1 hour max
        .updateIntervalMs = ctx->params.logInterval * 1000,
        .panelHandle = ctx->mainPanelHandle,
        .statusControl = PANEL_STR_PSB_STATUS,
        .progressControl = 0,
        .progressCallback = NULL,
        .statusCallback = NULL,
        .cancelFlag = &ctx->cancelRequested
    };
    
    ctx->phaseStartTime = Timer() - ctx->experimentStartTime;
    result = Battery_TransferCapacity(&discharge50);
    
    if (CheckCancellation(ctx)) {
        return ERR_CANCELLED;
    }
    
    // Store results regardless of exact success
    ctx->phase4Results.capacity_mAh = discharge50.actualTransferred_mAh;
    ctx->phase4Results.duration = discharge50.elapsedTime_s;
    ctx->phase4Results.endVoltage = discharge50.finalVoltage_V;
    
    if (result == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
        LogMessage("Phase 4 completed successfully");
        LogMessage("  Discharged: %.2f mAh", discharge50.actualTransferred_mAh);
        LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
        LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
        
        // Calculate final SOC estimate
        double finalSOC = 50.0 - ((discharge50.actualTransferred_mAh - discharge50.targetCapacity_mAh) / 
                                 ctx->measuredChargeCapacity_mAh) * 100.0;
        SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, MAX(0.0, finalSOC));
        
        strcpy(ctx->phase4Results.completionReason, "Target capacity reached");
    } else {
        LogWarning("Phase 4 incomplete - failed to discharge to exactly 50%%");
        LogMessage("  Discharged: %.2f mAh (target: %.2f mAh)", 
                   discharge50.actualTransferred_mAh, discharge50.targetCapacity_mAh);
        
        if (discharge50.result == BATTERY_OP_CURRENT_THRESHOLD) {
            strcpy(ctx->phase4Results.completionReason, "Current below threshold");
        } else if (discharge50.result == BATTERY_OP_TIMEOUT) {
            strcpy(ctx->phase4Results.completionReason, "Timeout reached");
        } else {
            strcpy(ctx->phase4Results.completionReason, "Discharge incomplete");
        }
    }
    
    // Finalize phase results
    FinalizePhaseResults(ctx, &ctx->phase4Results);
    SavePhaseResults(ctx, &ctx->phase4Results);
    
    LogMessage("Phase 4 completed");
    return SUCCESS;
}

/******************************************************************************
 * Temperature Control Functions
 ******************************************************************************/

static int SetupTemperatureControl(BaselineExperimentContext *ctx) {
    int result;
    
    LogMessage("Setting up temperature control - target: %.1f °C", ctx->params.targetTemperature);
    
    // Set DTB target temperature
    result = DTB_SetSetPointQueued(ctx->dtbSlaveAddress, ctx->params.targetTemperature, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogExperimentError(ctx, "Failed to set DTB temperature: %s", DTB_GetErrorString(result));
        return result;
    }
    
    // Start DTB controller
    result = DTB_SetRunStopQueued(ctx->dtbSlaveAddress, 1, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogExperimentError(ctx, "Failed to start DTB: %s", DTB_GetErrorString(result));
        return result;
    }
    
    LogMessage("DTB temperature controller started");
    return SUCCESS;
}

static int WaitForTargetTemperature(BaselineExperimentContext *ctx) {
    double startTime = Timer();
    double lastCheckTime = startTime;
    
    LogMessage("Waiting for DTB to reach target temperature: %.1f °C", ctx->params.targetTemperature);
    
    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        
        // Check temperature every interval
        if ((currentTime - lastCheckTime) >= BASELINE_TEMP_CHECK_INTERVAL) {
            DTB_Status status;
            int result = DTB_GetStatusQueued(ctx->dtbSlaveAddress, &status, DEVICE_PRIORITY_NORMAL);
            
            if (result == DTB_SUCCESS) {
                double tempDiff = fabs(status.processValue - ctx->params.targetTemperature);
                
                LogMessage("DTB temperature: %.1f °C (target: %.1f °C, diff: %.1f °C)", 
                          status.processValue, ctx->params.targetTemperature, tempDiff);
                
                if (tempDiff <= BASELINE_TEMP_TOLERANCE) {
                    LogMessage("DTB reached target temperature: %.1f °C", status.processValue);
                    ctx->dtbReady = 1;
                    ctx->temperatureStabilizationStart = currentTime;
                    return SUCCESS;
                }
                
                // Update status display
                char statusMsg[MEDIUM_BUFFER_SIZE];
                snprintf(statusMsg, sizeof(statusMsg), 
                         "Waiting for temperature: %.1f/%.1f °C", 
                         status.processValue, ctx->params.targetTemperature);
                SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            } else {
                LogWarning("Failed to read DTB status: %s", DTB_GetErrorString(result));
            }
            
            lastCheckTime = currentTime;
        }
        
        // Check timeout
        if ((currentTime - startTime) > BASELINE_TEMP_TIMEOUT_SEC) {
            LogWarning("Temperature wait timeout reached - continuing anyway");
            MessagePopup("Temperature Timeout", 
                         "DTB did not reach target temperature within timeout.\n"
                         "Continuing with experiment anyway.");
            ctx->dtbReady = 1;
            return SUCCESS;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int StabilizeTemperature(BaselineExperimentContext *ctx) {
    double startTime = ctx->temperatureStabilizationStart;
    double lastCheckTime = startTime;
    
    LogMessage("Stabilizing temperature for %.0f seconds...", BASELINE_TEMP_STABILIZE_TIME);
    
    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;
        
        // Check if stabilization time reached
        if (elapsedTime >= BASELINE_TEMP_STABILIZE_TIME) {
            LogMessage("Temperature stabilization completed");
            ctx->temperatureStable = 1;
            return SUCCESS;
        }
        
        // Periodic temperature monitoring during stabilization
        if ((currentTime - lastCheckTime) >= BASELINE_TEMP_CHECK_INTERVAL) {
            DTB_Status status;
            int result = DTB_GetStatusQueued(ctx->dtbSlaveAddress, &status, DEVICE_PRIORITY_NORMAL);
            
            if (result == DTB_SUCCESS) {
                double tempDiff = fabs(status.processValue - ctx->params.targetTemperature);
                
                if (tempDiff > BASELINE_TEMP_TOLERANCE) {
                    LogWarning("Temperature drifted during stabilization: %.1f °C (diff: %.1f °C)", 
                              status.processValue, tempDiff);
                    // Reset stabilization timer
                    startTime = currentTime;
                    ctx->temperatureStabilizationStart = currentTime;
                    LogMessage("Restarting temperature stabilization due to drift");
                }
                
                // Update status display
                char statusMsg[MEDIUM_BUFFER_SIZE];
                double remainingTime = BASELINE_TEMP_STABILIZE_TIME - elapsedTime;
                snprintf(statusMsg, sizeof(statusMsg), 
                         "Stabilizing temperature: %.1f °C (%.0f sec remaining)", 
                         status.processValue, remainingTime);
                SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
            }
            
            lastCheckTime = currentTime;
        }
        
        ProcessSystemEvents();
        Delay(1.0);
    }
}

/******************************************************************************
 * Device Control Functions
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
    
    // Safety: Disable BioLogic and PSB outputs first
    BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_LOW);  // Non-blocking
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);
    
    // Disconnect BioLogic relay first
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to disconnect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect PSB relay
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to connect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to PSB");
    return SUCCESS;
}

static int SwitchToBioLogic(BaselineExperimentContext *ctx) {
    int result;
    
    LogMessage("Switching to BioLogic...");
    
    // Safety: Disable PSB output first
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);
    
    // Disconnect PSB relay first
    result = SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to disconnect PSB relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    // Connect BioLogic relay
    result = SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "Failed to connect BioLogic relay: %s", GetErrorString(result));
        return result;
    }
    
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    
    LogMessage("Successfully switched to BioLogic");
    return SUCCESS;
}

static int SafeDisconnectAllDevices(BaselineExperimentContext *ctx) {
    LogMessage("Performing emergency disconnect of all devices...");
    
    // Disable all outputs (non-blocking, best effort)
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_LOW);
    BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_LOW);
    DTB_SetRunStopQueued(ctx->dtbSlaveAddress, 0, DEVICE_PRIORITY_LOW);
    
    // Disconnect all relays
    SetRelayState(TNY_PSB_PIN, TNY_STATE_DISCONNECTED);
    SetRelayState(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED);
    
    LogMessage("Emergency disconnect completed");
    return SUCCESS;
}

/******************************************************************************
 * EIS Measurement Functions  
 ******************************************************************************/

static int InitializeEISTargets(BaselineExperimentContext *ctx) {
    // Calculate initial target SOCs
    ctx->numTargetSOCs = 2;  // Start with 0% and 100%
    
    // Add intermediate points based on interval
    if (ctx->params.eisInterval > 0 && ctx->params.eisInterval < 100) {
        double soc = ctx->params.eisInterval;
        while (soc < 100.0) {
            ctx->numTargetSOCs++;
            soc += ctx->params.eisInterval;
        }
    }
    
    // Allocate arrays with extra capacity for dynamic growth
    ctx->targetSOCCapacity = ctx->numTargetSOCs + BASELINE_MAX_DYNAMIC_TARGETS;
    ctx->targetSOCs = (double*)calloc(ctx->targetSOCCapacity, sizeof(double));
    if (!ctx->targetSOCs) {
        return ERR_OUT_OF_MEMORY;
    }
    
    // Fill initial target array
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
    
    // Allocate measurements array
    ctx->eisMeasurementCapacity = ctx->targetSOCCapacity;
    ctx->eisMeasurements = (BaselineEISMeasurement*)calloc(ctx->eisMeasurementCapacity, sizeof(BaselineEISMeasurement));
    if (!ctx->eisMeasurements) {
        return ERR_OUT_OF_MEMORY;
    }
    
    LogMessage("EIS target SOC points initialized:");
    for (int i = 0; i < ctx->numTargetSOCs; i++) {
        LogMessage("  %.1f%%", ctx->targetSOCs[i]);
    }
    
    return SUCCESS;
}

static int AddDynamicSOCTarget(BaselineExperimentContext *ctx, double targetSOC) {
    // Check if we have capacity
    if (ctx->numTargetSOCs >= ctx->targetSOCCapacity) {
        LogWarning("Cannot add dynamic SOC target - array full");
        return ERR_OPERATION_FAILED;
    }
    
    // Add the new target
    ctx->targetSOCs[ctx->numTargetSOCs] = targetSOC;
    ctx->numTargetSOCs++;
    
    LogMessage("Added dynamic EIS target: %.1f%% SOC", targetSOC);
    return SUCCESS;
}

static int PerformEISMeasurement(BaselineExperimentContext *ctx, double targetSOC) {
    int result;
    
    // Check for cancellation
    if (CheckCancellation(ctx)) {
        return ERR_CANCELLED;
    }
    
    // Check measurement capacity
    if (ctx->eisMeasurementCount >= ctx->eisMeasurementCapacity) {
        LogError("EIS measurement array full!");
        return ERR_OPERATION_FAILED;
    }
    
    BaselineEISMeasurement *measurement = &ctx->eisMeasurements[ctx->eisMeasurementCount];
    
    // Initialize measurement
    memset(measurement, 0, sizeof(BaselineEISMeasurement));
    measurement->measurementIndex = ctx->eisMeasurementCount;
    measurement->targetSOC = targetSOC;
    measurement->actualSOC = ctx->currentSOC;
    measurement->timestamp = Timer() - ctx->experimentStartTime;
    measurement->retryCount = 0;
    
    // Read temperatures during measurement
    ReadAllTemperatures(ctx, &measurement->tempData, measurement->timestamp);
    
    // Update UI
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), 
             "Measuring EIS at %.1f%% SOC...", ctx->currentSOC);
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
    
    // Perform measurement with retry capability
    result = RetryEISMeasurement(ctx, measurement);
    if (result != SUCCESS) {
        LogExperimentError(ctx, "EIS measurement failed at %.1f%% SOC after retries", ctx->currentSOC);
        return result;
    }
    
    // Update graphs
    UpdateOCVGraph(ctx, measurement);
    UpdateNyquistPlot(ctx, measurement);
    
    // Save measurement data to file
    result = SaveEISMeasurementData(ctx, measurement);
    if (result != SUCCESS) {
        LogWarning("Failed to save EIS measurement data");
    }
    
    ctx->eisMeasurementCount++;
    
    LogMessage("EIS measurement %d completed at %.1f%% SOC (OCV: %.3f V)", 
               measurement->measurementIndex + 1, measurement->actualSOC, measurement->ocvVoltage);
    
    return SUCCESS;
}

static int RetryEISMeasurement(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    int result;
    
    while (measurement->retryCount <= BASELINE_MAX_EIS_RETRY) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        // Switch to BioLogic
        result = SwitchToBioLogic(ctx);
        if (result != SUCCESS) {
            LogExperimentError(ctx, "Failed to switch to BioLogic for EIS measurement");
            return result;
        }
        
        // Wait for settling after relay switch
        if (measurement->retryCount > 0) {
            LogMessage("EIS measurement retry %d after %.1f second delay", 
                      measurement->retryCount, BASELINE_EIS_RETRY_DELAY);
            Delay(BASELINE_EIS_RETRY_DELAY);
        }
        
        // Run OCV measurement
        result = RunOCVMeasurement(ctx, measurement);
        if (result != SUCCESS) {
            if (measurement->retryCount < BASELINE_MAX_EIS_RETRY) {
                LogWarning("OCV measurement failed (attempt %d), retrying...", measurement->retryCount + 1);
                measurement->retryCount++;
                continue;
            } else {
                LogError("OCV measurement failed after %d retries", BASELINE_MAX_EIS_RETRY + 1);
                return result;
            }
        }
        
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        // Run GEIS measurement
        result = RunGEISMeasurement(ctx, measurement);
        if (result != SUCCESS) {
            if (measurement->retryCount < BASELINE_MAX_EIS_RETRY) {
                LogWarning("GEIS measurement failed (attempt %d), retrying...", measurement->retryCount + 1);
                measurement->retryCount++;
                continue;
            } else {
                LogError("GEIS measurement failed after %d retries", BASELINE_MAX_EIS_RETRY + 1);
                return result;
            }
        }
        
        // Process GEIS data
        result = ProcessGEISData(measurement->geisData, measurement);
        if (result != SUCCESS) {
            LogWarning("Failed to process GEIS data");
            // Continue anyway - we have the raw data
        }
        
        // Success - exit retry loop
        if (measurement->retryCount > 0) {
            LogMessage("EIS measurement succeeded after %d retries", measurement->retryCount);
        }
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;
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
                                NULL, NULL, &(ctx->cancelRequested));
    
    if (result != SUCCESS) {
        LogError("OCV measurement failed: %s", BIO_GetErrorString(result));
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
        Delay(0.5);
        return result;
    }
    
    // Extract final voltage from OCV data
    if (measurement->ocvData && measurement->ocvData->convertedData) {
        BIO_ConvertedData *convData = measurement->ocvData->convertedData;
        
        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            int lastPoint = convData->numPoints - 1;
            measurement->ocvVoltage = convData->data[1][lastPoint];
            LogDebug("OCV measurement complete: %.3f V", measurement->ocvVoltage);
        } else {
            LogWarning("OCV data incomplete - using 0.0 V");
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
                                 NULL, NULL, &(ctx->cancelRequested));
    
    if (result != SUCCESS) {
        LogError("GEIS measurement failed: %s", BIO_GetErrorString(result));
        return result;
    }
    
    LogDebug("GEIS measurement complete");
    return SUCCESS;
}

static int ProcessGEISData(BIO_TechniqueData *geisData, BaselineEISMeasurement *measurement) {
    if (!geisData || !geisData->convertedData) {
        LogWarning("No GEIS data available for processing");
        return ERR_INVALID_PARAMETER;
    }
    
    BIO_ConvertedData *convData = geisData->convertedData;
    int processIndex = -1;
    
    if (geisData->rawData) {
        processIndex = geisData->rawData->processIndex;
    }
    
    LogDebug("Processing GEIS data: %d points, %d variables (process %d)", 
             convData->numPoints, convData->numVariables, processIndex);
    
    // Process impedance data (process 1 with 11+ variables)
    if (processIndex == 1 && convData->numVariables >= 11) {
        // Allocate impedance arrays
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
        
        // Extract impedance data
        for (int i = 0; i < convData->numPoints; i++) {
            measurement->frequencies[i] = convData->data[0][i];  // Frequency
            measurement->zReal[i] = convData->data[4][i];       // Re(Zwe)
            measurement->zImag[i] = convData->data[5][i];       // Im(Zwe)
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
    
    // Create filename with zero-padded SOC for proper sorting
    snprintf(filename, sizeof(filename), "%s%s%s%seis_%03d_soc.txt", 
             ctx->experimentDirectory, PATH_SEPARATOR, 
             BASELINE_PHASE3_DIR, PATH_SEPARATOR, 
             BASELINE_PHASE3_EIS_DIR, PATH_SEPARATOR,
             (int)(measurement->actualSOC + 0.5));
    
    // Store filename in measurement for reference
    strcpy(measurement->filename, filename);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create EIS measurement file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    // Write measurement header
    WriteINISection(file, "EIS_Measurement_Information");
    WriteINIValue(file, "Measurement_Index", "%d", measurement->measurementIndex);
    WriteINIValue(file, "Timestamp", "%s", timeStr);
    WriteINIDouble(file, "Elapsed_Time_s", measurement->timestamp, 1);
    WriteINIDouble(file, "Target_SOC_Percent", measurement->targetSOC, 1);
    WriteINIDouble(file, "Actual_SOC_Percent", measurement->actualSOC, 1);
    WriteINIDouble(file, "OCV_Voltage_V", measurement->ocvVoltage, 4);
    WriteINIDouble(file, "DTB_Temperature_C", measurement->tempData.dtbTemperature, 1);
    WriteINIDouble(file, "TC0_Temperature_C", measurement->tempData.tc0Temperature, 1);
    WriteINIDouble(file, "TC1_Temperature_C", measurement->tempData.tc1Temperature, 1);
    WriteINIValue(file, "Retry_Count", "%d", measurement->retryCount);
    fprintf(file, "\n");
    
    // Write EIS configuration
    WriteINISection(file, "EIS_Configuration");
    WriteINIDouble(file, "OCV_Duration_s", OCV_DURATION_S, 1);
    WriteINIDouble(file, "GEIS_Initial_Freq_Hz", GEIS_INITIAL_FREQ, 0);
    WriteINIDouble(file, "GEIS_Final_Freq_Hz", GEIS_FINAL_FREQ, 1);
    WriteINIValue(file, "GEIS_Freq_Points", "%d", GEIS_FREQ_NUMBER);
    WriteINIDouble(file, "GEIS_Amplitude_A", GEIS_AMPLITUDE_I, 3);
    WriteINIValue(file, "GEIS_Average_N", "%d", GEIS_AVERAGE_N);
    fprintf(file, "\n");
    
    // Write impedance data table
    WriteINISection(file, "Impedance_Data");
    if (measurement->numPoints > 0) {
        fprintf(file, "# Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Mag_Ohm,Phase_Deg\n");
        
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
    } else {
        fprintf(file, "# No impedance data available\n");
    }
    
    fclose(file);
    
    LogDebug("Saved EIS measurement data to: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Data Logging and Tracking Functions
 ******************************************************************************/

static int ReadAllTemperatures(BaselineExperimentContext *ctx, TemperatureDataPoint *tempData, double timestamp) {
    tempData->timestamp = timestamp;
    
    // Read DTB temperature
    DTB_Status dtbStatus;
    if (DTB_GetStatusQueued(ctx->dtbSlaveAddress, &dtbStatus, DEVICE_PRIORITY_NORMAL) == DTB_SUCCESS) {
        tempData->dtbTemperature = dtbStatus.processValue;
        snprintf(tempData->status, sizeof(tempData->status), "DTB: %.1f°C", dtbStatus.processValue);
    } else {
        tempData->dtbTemperature = 0.0;
        strcpy(tempData->status, "DTB: Error");
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
    double currentTime = Timer() - ctx->experimentStartTime - ctx->phaseStartTime;
    
    if (ctx->lastTime > 0) {
        double deltaTime = currentTime - ctx->lastTime;
        
        if (deltaTime > 0 && deltaTime < 3600) {  // Sanity check: reasonable time delta
            double capacityIncrement = Battery_CalculateCapacityIncrement(
                fabs(ctx->lastCurrent), fabs(current), deltaTime);
            
            ctx->accumulatedCapacity_mAh += capacityIncrement;
            
            // Update SOC based on current capacity estimate
            if (ctx->estimatedBatteryCapacity_mAh > 0) {
                ctx->currentSOC = (ctx->accumulatedCapacity_mAh / ctx->estimatedBatteryCapacity_mAh) * 100.0;
                
                // Only clamp to minimum 0%
                if (ctx->currentSOC < 0.0) {
                    ctx->currentSOC = 0.0;
                }
            }
        }
    }
    
    ctx->lastCurrent = current;
    ctx->lastTime = currentTime;
    
    return SUCCESS;
}

static int UpdateCapacityEstimate(BaselineExperimentContext *ctx) {
    // Update capacity estimate based on SOC progress
    // This helps handle cases where initial estimate was wrong
    
    if (ctx->currentSOC > 110.0 && ctx->estimatedBatteryCapacity_mAh > 0) {
        // Battery capacity was underestimated
        double newEstimate = (ctx->accumulatedCapacity_mAh / ctx->currentSOC) * 100.0;
        if (newEstimate > ctx->estimatedBatteryCapacity_mAh * 1.1) {
            LogMessage("Updating battery capacity estimate from %.1f to %.1f mAh", 
                      ctx->estimatedBatteryCapacity_mAh, newEstimate);
            ctx->estimatedBatteryCapacity_mAh = newEstimate;
        }
    }
    
    return SUCCESS;
}

static int LogPhaseDataPoint(BaselineExperimentContext *ctx, const char *format, ...) {
    if (!ctx->currentPhaseLogFile) {
        return ERR_INVALID_STATE;
    }
    
    va_list args;
    va_start(args, format);
    vfprintf(ctx->currentPhaseLogFile, format, args);
    va_end(args);
    
    fprintf(ctx->currentPhaseLogFile, "\n");
    fflush(ctx->currentPhaseLogFile);
    
    return SUCCESS;
}

/******************************************************************************
 * Utility and Helper Functions
 ******************************************************************************/

static int CheckCancellation(BaselineExperimentContext *ctx) {
    return (ctx->cancelRequested || ctx->emergencyStop || 
            ctx->state == BASELINE_STATE_CANCELLED ||
            ctx->state == BASELINE_STATE_ERROR);
}

static const char* GetPhaseDescription(BaselineExperimentPhase phase) {
    switch (phase) {
        case BASELINE_PHASE_1: return "Phase 1: Discharge & Temperature";
        case BASELINE_PHASE_2: return "Phase 2: Capacity Test";
        case BASELINE_PHASE_3: return "Phase 3: EIS Charge";
        case BASELINE_PHASE_4: return "Phase 4: Discharge to 50%";
        default: return "Unknown Phase";
    }
}

static const char* GetStateDescription(BaselineExperimentState state) {
    switch (state) {
        case BASELINE_STATE_IDLE: return "Idle";
        case BASELINE_STATE_PREPARING: return "Preparing";
        case BASELINE_STATE_PHASE1_DISCHARGE: return "Phase 1 Discharge";
        case BASELINE_STATE_PHASE1_TEMP_WAIT: return "Phase 1 Temperature Wait";
        case BASELINE_STATE_PHASE1_TEMP_STABILIZE: return "Phase 1 Temperature Stabilize";
        case BASELINE_STATE_PHASE2_CHARGE: return "Phase 2 Charge";
        case BASELINE_STATE_PHASE2_DISCHARGE: return "Phase 2 Discharge";
        case BASELINE_STATE_PHASE3_SETUP: return "Phase 3 Setup";
        case BASELINE_STATE_PHASE3_CHARGING: return "Phase 3 Charging";
        case BASELINE_STATE_PHASE3_EIS_MEASUREMENT: return "Phase 3 EIS Measurement";
        case BASELINE_STATE_PHASE4_DISCHARGE: return "Phase 4 Discharge";
        case BASELINE_STATE_COMPLETED: return "Completed";
        case BASELINE_STATE_ERROR: return "Error";
        case BASELINE_STATE_CANCELLED: return "Cancelled";
        default: return "Unknown State";
    }
}

static void LogExperimentError(BaselineExperimentContext *ctx, const char *format, ...) {
    char message[LARGE_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Log to system log
    LogError("%s", message);
    
    // Log to experiment error file if available
    if (ctx->errorLogFile) {
        time_t now = time(NULL);
        char timeStr[64];
        FormatTimestamp(now, timeStr, sizeof(timeStr));
        
        fprintf(ctx->errorLogFile, "[%s] %s: %s\n", 
                timeStr, GetStateDescription(ctx->state), message);
        fflush(ctx->errorLogFile);
    }
}

/******************************************************************************
 * Phase Results Management (abbreviated due to length)
 ******************************************************************************/

static int InitializePhaseResults(BaselinePhaseResults *results, BaselineExperimentPhase phase) {
    memset(results, 0, sizeof(BaselinePhaseResults));
    results->phase = phase;
    results->startTime = -1;
    results->endTime = -1;
    return SUCCESS;
}

static int FinalizePhaseResults(BaselineExperimentContext *ctx, BaselinePhaseResults *results) {
    results->endTime = Timer() - ctx->experimentStartTime;
    if (results->startTime >= 0) {
        results->duration = results->endTime - results->startTime;
    }
    return SUCCESS;
}

static int SavePhaseResults(BaselineExperimentContext *ctx, BaselinePhaseResults *results) {
    char filename[MAX_PATH_LENGTH];
    snprintf(filename, sizeof(filename), "%s%s%s", 
             results->phaseDirectory, PATH_SEPARATOR, BASELINE_PHASE_SUMMARY_FILE);
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        LogWarning("Failed to save phase results to: %s", filename);
        return ERR_BASE_FILE;
    }
    
    WriteINISection(file, "Phase_Summary");
    WriteINIValue(file, "Phase_Number", "%d", results->phase);
    WriteINIDouble(file, "Duration_s", results->duration, 1);
    WriteINIDouble(file, "Capacity_mAh", results->capacity_mAh, 2);
    WriteINIDouble(file, "Energy_Wh", results->energy_Wh, 3);
    WriteINIDouble(file, "Start_Voltage_V", results->startVoltage, 3);
    WriteINIDouble(file, "End_Voltage_V", results->endVoltage, 3);
    WriteINIValue(file, "Completion_Reason", "%s", results->completionReason);
    
    fclose(file);
    return SUCCESS;
}

/******************************************************************************
 * Graph Functions (abbreviated)
 ******************************************************************************/

static int ConfigureExperimentGraphs(BaselineExperimentContext *ctx) {
    // Configure main graphs for experiment
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph1Handle, 
                   "Current vs Time", "Time (s)", "Current (A)", 
                   0.0, maxCurrent * 1.1);
    
    ConfigureGraph(ctx->mainPanelHandle, ctx->graph2Handle, 
                   "Voltage vs Time", "Time (s)", "Voltage (V)", 
                   ctx->params.dischargeVoltage * 0.9, 
                   ctx->params.chargeVoltage * 1.1);
    
    ClearAllExperimentGraphs(ctx);
    return SUCCESS;
}

static void UpdateCurrentGraph(BaselineExperimentContext *ctx, double current, double time) {
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph1Handle, 
                  time, fabs(current), VAL_SOLID_CIRCLE, VAL_RED);
}

static void UpdateVoltageGraph(BaselineExperimentContext *ctx, double voltage, double time) {
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
                  time, voltage, VAL_SOLID_CIRCLE, VAL_BLUE);
}

static void UpdateOCVGraph(BaselineExperimentContext *ctx, BaselineEISMeasurement *measurement) {
    PlotDataPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
                  measurement->actualSOC, measurement->ocvVoltage, 
                  VAL_SOLID_CIRCLE, VAL_BLUE);
    
    // Connect points with line if we have multiple measurements
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
    
    // Clear previous plot
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphBiologicHandle, -1, VAL_DELAYED_DRAW);
    
    // Create negative imaginary array for conventional Nyquist plot
    double *negZImag = (double*)calloc(measurement->numPoints, sizeof(double));
    if (!negZImag) return;
    
    for (int i = 0; i < measurement->numPoints; i++) {
        negZImag[i] = -measurement->zImag[i];
    }
    
    // Plot Nyquist data
    PlotXY(ctx->mainPanelHandle, ctx->graphBiologicHandle,
           measurement->zReal, negZImag, measurement->numPoints,
           VAL_DOUBLE, VAL_DOUBLE, VAL_SCATTER,
           VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_GREEN);
    
    // Update title
    char title[MEDIUM_BUFFER_SIZE];
    snprintf(title, sizeof(title), "Nyquist Plot - SOC: %.1f%%", measurement->actualSOC);
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, title);
    
    free(negZImag);
}

static void ClearAllExperimentGraphs(BaselineExperimentContext *ctx) {
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle, ctx->graphBiologicHandle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 3);
}

/******************************************************************************
 * Results and Cleanup Functions
 ******************************************************************************/

static int WriteComprehensiveResults(BaselineExperimentContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->experimentDirectory, PATH_SEPARATOR, BASELINE_SUMMARY_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create comprehensive results file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t startTime = (time_t)ctx->experimentStartTime;
    time_t endTime = (time_t)ctx->experimentEndTime;
    char startTimeStr[64], endTimeStr[64];
    
    FormatTimestamp(startTime, startTimeStr, sizeof(startTimeStr));
    FormatTimestamp(endTime, endTimeStr, sizeof(endTimeStr));
    
    // Write comprehensive header
    fprintf(file, "# BASELINE BATTERY EXPERIMENT SUMMARY\n");
    fprintf(file, "# ===================================\n");
    fprintf(file, "# Generated by Battery Tester v%s\n", PROJECT_VERSION);
    fprintf(file, "# Comprehensive battery characterization experiment\n\n");
    
    // Experiment Overview
    WriteINISection(file, "Experiment_Overview");
    WriteINIValue(file, "Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_h", (ctx->experimentEndTime - ctx->experimentStartTime) / 3600.0, 2);
    WriteINIValue(file, "Final_State", "%s", GetStateDescription(ctx->state));
    WriteINIDouble(file, "Target_Temperature_C", ctx->params.targetTemperature, 1);
    fprintf(file, "\n");
    
    // Phase 1 Results
    WriteINISection(file, "Phase1_Initial_Discharge");
    WriteINIDouble(file, "Initial_Discharge_Capacity_mAh", ctx->phase1Results.capacity_mAh, 2);
    WriteINIDouble(file, "Initial_Discharge_Energy_Wh", ctx->phase1Results.energy_Wh, 3);
    WriteINIDouble(file, "Start_Voltage_V", ctx->phase1Results.startVoltage, 3);
    WriteINIDouble(file, "End_Voltage_V", ctx->phase1Results.endVoltage, 3);
    fprintf(file, "\n");
    
    // Phase 2 Capacity Results
    WriteINISection(file, "Phase2_Capacity_Test");
    WriteINIDouble(file, "Charge_Capacity_mAh", ctx->phase2ChargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->phase2DischargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Charge_Energy_Wh", ctx->phase2ChargeResults.energy_Wh, 3);
    WriteINIDouble(file, "Discharge_Energy_Wh", ctx->phase2DischargeResults.energy_Wh, 3);
    WriteINIDouble(file, "Coulombic_Efficiency_Percent", 
                  Battery_CalculateCoulombicEfficiency(ctx->phase2ChargeResults.capacity_mAh, 
                                                      ctx->phase2DischargeResults.capacity_mAh), 1);
    WriteINIDouble(file, "Energy_Efficiency_Percent", 
                  Battery_CalculateEnergyEfficiency(ctx->phase2ChargeResults.energy_Wh,
                                                   ctx->phase2DischargeResults.energy_Wh), 1);
    fprintf(file, "\n");
    
    // Phase 3 EIS Results Summary
    WriteINISection(file, "Phase3_EIS_Summary");
    WriteINIValue(file, "Total_EIS_Measurements", "%d", ctx->eisMeasurementCount);
    WriteINIValue(file, "Dynamic_Targets_Added", "%d", ctx->dynamicTargetsAdded);
    
    if (ctx->eisMeasurementCount > 0) {
        fprintf(file, "SOC_Points=");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            fprintf(file, "%.1f", ctx->eisMeasurements[i].actualSOC);
            if (i < ctx->eisMeasurementCount - 1) fprintf(file, ",");
        }
        fprintf(file, "\n");
        
        fprintf(file, "OCV_Values=");
        for (int i = 0; i < ctx->eisMeasurementCount; i++) {
            fprintf(file, "%.3f", ctx->eisMeasurements[i].ocvVoltage);
            if (i < ctx->eisMeasurementCount - 1) fprintf(file, ",");
        }
        fprintf(file, "\n");
    }
    fprintf(file, "\n");
    
    // Phase 4 Results
    WriteINISection(file, "Phase4_Final_Discharge");
    WriteINIDouble(file, "Target_50Percent_Capacity_mAh", ctx->measuredChargeCapacity_mAh * 0.5, 2);
    WriteINIDouble(file, "Actual_Discharged_mAh", ctx->phase4Results.capacity_mAh, 2);
    WriteINIDouble(file, "Final_Voltage_V", ctx->phase4Results.endVoltage, 3);
    WriteINIValue(file, "Completion_Status", "%s", ctx->phase4Results.completionReason);
    fprintf(file, "\n");
    
    // Battery Characterization Summary
    WriteINISection(file, "Battery_Characterization");
    WriteINIDouble(file, "Rated_Capacity_mAh", ctx->phase2ChargeResults.capacity_mAh, 1);
    WriteINIDouble(file, "Usable_Capacity_mAh", ctx->phase2DischargeResults.capacity_mAh, 1);
    WriteINIDouble(file, "Initial_Residual_Capacity_mAh", ctx->phase1Results.capacity_mAh, 1);
    
    if (ctx->eisMeasurementCount >= 2) {
        WriteINIDouble(file, "OCV_Range_V", 
                      ctx->eisMeasurements[ctx->eisMeasurementCount-1].ocvVoltage - 
                      ctx->eisMeasurements[0].ocvVoltage, 3);
    }
    
    // Files and Data References
    fprintf(file, "\n# DATA FILES:\n");
    fprintf(file, "# Phase 1: %s/%s\n", BASELINE_PHASE1_DIR, BASELINE_PHASE1_DISCHARGE_FILE);
    fprintf(file, "# Phase 2: %s/{%s, %s}\n", BASELINE_PHASE2_DIR, BASELINE_PHASE2_CHARGE_FILE, BASELINE_PHASE2_DISCHARGE_FILE);
    fprintf(file, "# Phase 3: %s/%s and %s/%s/\n", BASELINE_PHASE3_DIR, BASELINE_PHASE3_CHARGE_FILE, BASELINE_PHASE3_DIR, BASELINE_PHASE3_EIS_DIR);
    fprintf(file, "# Phase 4: %s/%s\n", BASELINE_PHASE4_DIR, BASELINE_PHASE4_DISCHARGE_FILE);
    fprintf(file, "# Settings: %s\n", BASELINE_SETTINGS_FILE);
    fprintf(file, "# Main Log: %s\n", BASELINE_MAIN_LOG_FILE);
    
    fclose(file);
    
    LogMessage("Comprehensive results written to: %s", filename);
    return SUCCESS;
}

static int WriteImportableResults(BaselineExperimentContext *ctx) {
    // This creates results files that can be imported by other experiments
    // Similar to the capacity experiment results format
    
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    // Create importable capacity results for SOCEIS
    snprintf(filename, sizeof(filename), "%s%scapacity_results.ini", 
             ctx->experimentDirectory, PATH_SEPARATOR);
    
    file = fopen(filename, "w");
    if (!file) {
        LogWarning("Failed to create importable capacity results");
        return ERR_BASE_FILE;
    }
    
    WriteINISection(file, "Experiment_Parameters");
    WriteINIDouble(file, "Charge_Voltage_V", ctx->params.chargeVoltage, 3);
    WriteINIDouble(file, "Discharge_Voltage_V", ctx->params.dischargeVoltage, 3);
    WriteINIDouble(file, "Charge_Current_A", ctx->params.chargeCurrent, 3);
    WriteINIDouble(file, "Discharge_Current_A", ctx->params.dischargeCurrent, 3);
    WriteINIDouble(file, "Current_Threshold_A", ctx->params.currentThreshold, 3);
    WriteINIValue(file, "Log_Interval_s", "%u", ctx->params.logInterval);
    fprintf(file, "\n");
    
    WriteINISection(file, "Charge_Results");
    WriteINIDouble(file, "Charge_Capacity_mAh", ctx->phase2ChargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Charge_Energy_Wh", ctx->phase2ChargeResults.energy_Wh, 3);
    WriteINIDouble(file, "Charge_Duration_s", ctx->phase2ChargeResults.duration, 1);
    fprintf(file, "\n");
    
    WriteINISection(file, "Discharge_Results");
    WriteINIDouble(file, "Discharge_Capacity_mAh", ctx->phase2DischargeResults.capacity_mAh, 2);
    WriteINIDouble(file, "Discharge_Energy_Wh", ctx->phase2DischargeResults.energy_Wh, 3);
    WriteINIDouble(file, "Discharge_Duration_s", ctx->phase2DischargeResults.duration, 1);
    
    fclose(file);
    
    LogMessage("Importable results written for other experiments");
    return SUCCESS;
}

static void CleanupExperiment(BaselineExperimentContext *ctx) {
    LogMessage("Cleaning up baseline experiment...");
    
    // Safely disconnect all devices
    SafeDisconnectAllDevices(ctx);
    
    // Close any open log files
    if (ctx->mainLogFile) {
        fclose(ctx->mainLogFile);
        ctx->mainLogFile = NULL;
    }
    
    if (ctx->currentPhaseLogFile) {
        fclose(ctx->currentPhaseLogFile);
        ctx->currentPhaseLogFile = NULL;
    }
    
    if (ctx->errorLogFile) {
        fclose(ctx->errorLogFile);
        ctx->errorLogFile = NULL;
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
        ctx->eisMeasurements = NULL;
    }
    
    if (ctx->targetSOCs) {
        free(ctx->targetSOCs);
        ctx->targetSOCs = NULL;
    }
    
    LogMessage("Baseline experiment cleanup completed");
}

static void RestoreUI(BaselineExperimentContext *ctx) {
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
}

/******************************************************************************
 * Additional Missing Function Implementations
 ******************************************************************************/

static int LogMainDataPoint(BaselineExperimentContext *ctx, BaselineDataPoint *point) {
    if (!ctx->mainLogFile) {
        return ERR_INVALID_STATE;
    }
    
    // Log to main experiment log file
    fprintf(ctx->mainLogFile, "%.3f,%d,%s,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%s\n",
            point->timestamp,
            point->phase,
            GetStateDescription(ctx->state),
            point->voltage,
            point->current,
            point->power,
            point->soc,
            point->tempData.dtbTemperature,
            point->tempData.tc0Temperature,
            point->tempData.tc1Temperature,
            point->phaseDescription);
    
    fflush(ctx->mainLogFile);
    
    return SUCCESS;
}

static int UpdatePhaseResults(BaselinePhaseResults *results, BaselineDataPoint *dataPoint) {
    if (!results || !dataPoint) {
        return ERR_NULL_POINTER;
    }
    
    // Update running statistics
    results->dataPointCount++;
    
    // Update sums for averaging
    results->avgCurrent += fabs(dataPoint->current);
    results->avgVoltage += dataPoint->voltage;
    results->avgTemperature_dtb += dataPoint->tempData.dtbTemperature;
    results->avgTemperature_tc0 += dataPoint->tempData.tc0Temperature;
    results->avgTemperature_tc1 += dataPoint->tempData.tc1Temperature;
    
    // Track peak current
    if (fabs(dataPoint->current) > results->peakCurrent) {
        results->peakCurrent = fabs(dataPoint->current);
    }
    
    // Calculate averages
    if (results->dataPointCount > 0) {
        results->avgCurrent = results->avgCurrent / results->dataPointCount;
        results->avgVoltage = results->avgVoltage / results->dataPointCount;
        results->avgTemperature_dtb = results->avgTemperature_dtb / results->dataPointCount;
        results->avgTemperature_tc0 = results->avgTemperature_tc0 / results->dataPointCount;
        results->avgTemperature_tc1 = results->avgTemperature_tc1 / results->dataPointCount;
    }
    
    return SUCCESS;
}