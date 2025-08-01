/******************************************************************************
 * exp_capacity.c
 * 
 * Battery Capacity Testing Experiment Module Implementation
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

// Test context and thread management
static CapacityTestContext g_testContext = {0};
static CmtThreadFunctionID g_testThreadId = 0;

static const int numControls = 3;
static const int controls[numControls] = {CAPACITY_NUM_CURRENT_THRESHOLD, 
					                      CAPACITY_NUM_INTERVAL,
                                          CAPACITY_CHECKBOX_RETURN_50};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CapacityExperimentThread(void *functionData);
static int VerifyBatteryCharged(CapacityTestContext *ctx);
static int ConfigureGraphs(CapacityTestContext *ctx);
static int RunTestPhase(CapacityTestContext *ctx, CapacityTestPhase phase);
static int LogDataPoint(CapacityTestContext *ctx, CapacityDataPoint *point);
static void UpdateGraphs(CapacityTestContext *ctx, CapacityDataPoint *point);
static void ClearGraphs(CapacityTestContext *ctx);
static void RestoreUI(CapacityTestContext *ctx);
static int CreateTestDirectory(CapacityTestContext *ctx);
static int WriteResultsFile(CapacityTestContext *ctx);
static void UpdatePhaseResults(CapacityTestContext *ctx, PhaseResults *results, 
                               CapacityDataPoint *point, double capacityIncrement, double energyIncrement);
static double CalculateCoulombicEfficiency(double chargeCapacity, double dischargeCapacity);
static double CalculateEnergyEfficiency(double chargeEnergy, double dischargeEnergy);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if capacity test is already running
    if (CapacityTest_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop capacity test");
        g_testContext.state = CAPACITY_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the capacity test.");
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
                     "Please ensure it is connected before running the capacity test.");
        return 0;
    }
    
    PSB_Handle *psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!psbHandle || !psbHandle->isConnected) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running the capacity test.");
        return 0;
    }
    
    // Check that PSB output is disabled
    PSB_Status status;
    if (PSB_GetStatusQueued(psbHandle, &status) == PSB_SUCCESS) {
        if (status.outputEnabled) {
            CmtGetLock(g_busyLock);
            g_systemBusy = 0;
            CmtReleaseLock(g_busyLock);
            
            MessagePopup("PSB Output Enabled", 
                         "The PSB output must be disabled before starting the test.\n"
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
    
    // Initialize test context
    memset(&g_testContext, 0, sizeof(g_testContext));
    g_testContext.state = CAPACITY_STATE_PREPARING;
    g_testContext.mainPanelHandle = g_mainPanelHandle;
    g_testContext.tabPanelHandle = panel;
    g_testContext.buttonControl = control;
    g_testContext.statusControl = PANEL_STR_PSB_STATUS;
    g_testContext.psbHandle = psbHandle;
    g_testContext.graph1Handle = PANEL_GRAPH_1;
    g_testContext.graph2Handle = PANEL_GRAPH_2;
    
    // Read test parameters from UI
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_testContext.params.chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_testContext.params.dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_testContext.params.chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_testContext.params.dischargeCurrent);
    GetCtrlVal(panel, CAPACITY_NUM_CURRENT_THRESHOLD, &g_testContext.params.currentThreshold);
    GetCtrlVal(panel, CAPACITY_NUM_INTERVAL, &g_testContext.params.logInterval);
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Dim appropriate controls
    DimCapacityExperimentControls(g_mainPanelHandle, panel, 1, controls, numControls);
    
    // Start test thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CapacityExperimentThread, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        // Failed to start thread
        g_testContext.state = CAPACITY_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimCapacityExperimentControls(g_mainPanelHandle, panel, 0, controls, numControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start capacity test thread.");
        return 0;
    }
    
    return 0;
}

int CapacityTest_IsRunning(void) {
    return !(g_testContext.state == CAPACITY_STATE_IDLE ||
             g_testContext.state == CAPACITY_STATE_COMPLETED ||
             g_testContext.state == CAPACITY_STATE_ERROR ||
             g_testContext.state == CAPACITY_STATE_CANCELLED);
}

/******************************************************************************
 * Test Thread Implementation
 ******************************************************************************/

static int CapacityExperimentThread(void *functionData) {
    CapacityTestContext *ctx = (CapacityTestContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    double chargeCapacity_mAh = 0.0;
    
    LogMessage("=== Starting Battery Capacity Test ===");
    
    // Record test start time
    ctx->testStartTime = Timer();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity test cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    SAFE_SPRINTF(message, sizeof(message),
        "Battery Capacity Test Parameters:\n\n"
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
    
    int response = ConfirmPopup("Confirm Test Parameters", message);
    if (!response || ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity test cancelled by user");
        ctx->state = CAPACITY_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create test directory
    result = CreateTestDirectory(ctx);
    if (result != SUCCESS) {
        LogError("Failed to create test directory");
        MessagePopup("Error", "Failed to create test directory.\nPlease check permissions.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Initialize PSB to safe state
    LogMessage("Initializing PSB to zeroed state...");
    result = PSB_ZeroAllValues(NULL);
    
    if (result != PSB_SUCCESS) {
        LogError("Failed to initialize PSB to safe state: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to initialize PSB to safe state.\nPlease check the connection and try again.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Check for cancellation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity test cancelled during initialization");
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
    
    result = RunTestPhase(ctx, CAPACITY_PHASE_DISCHARGE);
    if (result != SUCCESS || ctx->state == CAPACITY_STATE_CANCELLED) {
        if (ctx->state != CAPACITY_STATE_CANCELLED) {
            ctx->state = CAPACITY_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Clear graphs between phases
    ClearGraphs(ctx);
    
    // Short delay between phases
    LogMessage("Switching from discharge to charge phase...");
    
    // Check for cancellation during delay
    for (int i = 0; i < 20; i++) {
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            goto cleanup;
        }
        Delay(0.1);
    }
    
    // Run charge phase
    LogMessage("Starting charge phase...");
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Charging battery...");
    
    result = RunTestPhase(ctx, CAPACITY_PHASE_CHARGE);
    if (result != SUCCESS || ctx->state == CAPACITY_STATE_CANCELLED) {
        if (ctx->state != CAPACITY_STATE_CANCELLED) {
            ctx->state = CAPACITY_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Store charge capacity for potential 50% return
    chargeCapacity_mAh = ctx->chargeResults.capacity_mAh;
    
    // Record test end time
    ctx->testEndTime = Timer();
    
    ctx->state = CAPACITY_STATE_COMPLETED;
    LogMessage("=== Battery Capacity Test Completed Successfully ===");
    
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
            
            // Update UI
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
            int dischargeResult = Battery_DischargeCapacity(ctx->psbHandle, &discharge50);
            
            if (dischargeResult == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
                LogMessage("Successfully returned battery to 50%% capacity");
                LogMessage("  Discharged: %.2f mAh", discharge50.actualDischarged_mAh);
                LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
                LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
                
                SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, 
                          "Capacity test completed - battery at 50% capacity");
            } else {
                LogWarning("Failed to return to 50%% capacity");
                SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, 
                          "Capacity test completed - failed to return to 50%");
            }
        }
    }
    
cleanup:
    // Turn off PSB output
    PSBCommandParams offParams = {0};
    PSBCommandResult offResult = {0};
    offParams.outputEnable.enable = 0;
    PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), PSB_CMD_SET_OUTPUT_ENABLE,
                           &offParams, PSB_PRIORITY_HIGH, &offResult,
                           PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Update status based on final state
    if (ctx->state == CAPACITY_STATE_COMPLETED) {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test completed");
    } else if (ctx->state == CAPACITY_STATE_CANCELLED) {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test cancelled");
    } else {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test failed");
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
    g_testThreadId = 0;
    
    return 0;
}

/******************************************************************************
 * Helper Functions Implementation
 ******************************************************************************/

static int CreateTestDirectory(CapacityTestContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        // Fallback to current directory
        strcpy(basePath, ".");
    }
    
    // Create data directory
    SAFE_SPRINTF(dataPath, sizeof(dataPath), "%s%s%s", 
                basePath, PATH_SEPARATOR, CAPACITY_TEST_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamp subdirectory using new utility function
    int result = CreateTimestampedDirectory(dataPath, NULL, 
                                          ctx->testDirectory, sizeof(ctx->testDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create test directory");
        return result;
    }
    
    LogMessage("Created test directory: %s", ctx->testDirectory);
    return SUCCESS;
}

static int VerifyBatteryCharged(CapacityTestContext *ctx) {
    PSBCommandParams params = {0};
    PSBCommandResult result = {0};
    char message[LARGE_BUFFER_SIZE];
    
    LogMessage("Verifying battery charge state...");
    
    // Check for cancellation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Get current battery voltage
    int error = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), PSB_CMD_GET_STATUS,
                                       &params, PSB_PRIORITY_HIGH, &result,
                                       PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (error != PSB_SUCCESS) {
        LogError("Failed to read PSB status: %s", PSB_GetErrorString(error));
        return ERR_COMM_FAILED;
    }
    
    double voltageDiff = fabs(result.data.status.voltage - ctx->params.chargeVoltage);
    
    LogMessage("Battery voltage: %.3f V, Expected: %.3f V, Difference: %.3f V", 
               result.data.status.voltage, ctx->params.chargeVoltage, voltageDiff);
    
    if (voltageDiff > CAPACITY_TEST_VOLTAGE_MARGIN) {
        SAFE_SPRINTF(message, sizeof(message),
            "Battery may not be fully charged:\n\n"
            "Measured Voltage: %.3f V\n"
            "Expected Voltage: %.3f V\n"
            "Difference: %.3f V\n"
            "Error Margin: %.3f V\n\n"
            "Do you want to continue anyway?",
            result.data.status.voltage,
            ctx->params.chargeVoltage,
            voltageDiff,
            CAPACITY_TEST_VOLTAGE_MARGIN);
        
        // Check for cancellation before showing dialog
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

static int ConfigureGraphs(CapacityTestContext *ctx) {
    // Configure Graph 1 - Current vs Time
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph1Handle, ATTR_LABEL_TEXT, "Current vs Time");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph1Handle, ATTR_XNAME, "Time (s)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph1Handle, ATTR_YNAME, "Current (A)");
    
    // Set Y-axis range for current
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph1Handle, VAL_LEFT_YAXIS, 
                       VAL_MANUAL, 0.0, maxCurrent * 1.1);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph1Handle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    // Configure Graph 2 - Voltage vs Time
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph2Handle, ATTR_LABEL_TEXT, "Voltage vs Time");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph2Handle, ATTR_XNAME, "Time (s)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graph2Handle, ATTR_YNAME, "Voltage (V)");
    
    // Set Y-axis range for voltage
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_LEFT_YAXIS, 
                       VAL_MANUAL, ctx->params.dischargeVoltage * 0.9, 
                       ctx->params.chargeVoltage * 1.1);
    SetAxisScalingMode(ctx->mainPanelHandle, ctx->graph2Handle, VAL_BOTTOM_XAXIS, 
                       VAL_AUTOSCALE, 0.0, 0.0);
    
    // Clear any existing plots
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph1Handle, -1, VAL_DELAYED_DRAW);
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph2Handle, -1, VAL_DELAYED_DRAW);
    
    return SUCCESS;
}

static int RunTestPhase(CapacityTestContext *ctx, CapacityTestPhase phase) {
    char filename[MAX_PATH_LENGTH];
    PSBCommandParams params = {0};
    PSBCommandResult cmdResult = {0};
    CapacityDataPoint dataPoint;
    PhaseResults *phaseResults;
    int result;
    
    // Check for cancellation
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
    SAFE_SPRINTF(filename, sizeof(filename), "%s%s%s", 
                ctx->testDirectory, PATH_SEPARATOR, 
                (phase == CAPACITY_PHASE_DISCHARGE) ? 
                CAPACITY_TEST_DISCHARGE_FILE : CAPACITY_TEST_CHARGE_FILE);
    
    ctx->csvFile = fopen(filename, "w");
    if (!ctx->csvFile) {
        LogError("Failed to create %s file", filename);
        return ERR_BASE_FILE;
    }
    
    // Write CSV header
    fprintf(ctx->csvFile, "Time_s,Voltage_V,Current_A,Power_W\n");
    
    // Set target voltage
    result = PSB_SetVoltageQueued(NULL, targetVoltage);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    // Set target current
    if (phase == CAPACITY_PHASE_DISCHARGE) {
        result = PSB_SetSinkCurrentQueued(NULL, targetCurrent);
    } else {
        result = PSB_SetCurrentQueued(NULL, targetCurrent);
    }
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    // Enable PSB output
    params.outputEnable.enable = 1;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_OUTPUT_ENABLE,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
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
    ctx->phaseStartTime = Timer();
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
    PSBCommandParams statusParams = {0};
    PSBCommandResult statusResult = {0};
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_GET_STATUS,
                                    &statusParams, PSB_PRIORITY_HIGH, &statusResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result == PSB_SUCCESS) {
        phaseResults->startVoltage = statusResult.data.status.voltage;
    }
    
    LogMessage("%s phase started", phaseName);
    
    // Main test loop
    while (1) {
        // Check for cancellation
        if (ctx->state == CAPACITY_STATE_CANCELLED) {
            LogMessage("%s phase cancelled by user", phaseName);
            break;
        }
        
        double currentTime = Timer();
        double elapsedTime = currentTime - ctx->phaseStartTime;
        
        // Check for timeout
        if (elapsedTime > (CAPACITY_TEST_MAX_DURATION_H * 3600.0)) {
            LogWarning("%s phase timeout reached", phaseName);
            break;
        }
        
        // Get current status
        result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                        PSB_CMD_GET_STATUS,
                                        &statusParams, PSB_PRIORITY_HIGH, &statusResult,
                                        PSB_QUEUE_COMMAND_TIMEOUT_MS);
        if (result != PSB_SUCCESS) {
            LogError("Failed to read status: %s", PSB_GetErrorString(result));
            break;
        }
        
        PSB_Status *status = &statusResult.data.status;
        
        // Check current threshold
        if (fabs(status->current) < ctx->params.currentThreshold) {
            LogMessage("%s phase completed - current below threshold (%.3f A < %.3f A)",
                      phaseName, fabs(status->current), ctx->params.currentThreshold);
            phaseResults->endVoltage = status->voltage;
            break;
        }
        
        // Log data point if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            dataPoint.time = elapsedTime;
            dataPoint.voltage = status->voltage;
            dataPoint.current = status->current;
            dataPoint.power = status->power;
            
            LogDataPoint(ctx, &dataPoint);
            ctx->lastLogTime = currentTime;
        }
        
        // Update graphs if needed
        if ((currentTime - ctx->lastGraphUpdate) >= CAPACITY_TEST_GRAPH_UPDATE_RATE) {
            dataPoint.time = elapsedTime;
            dataPoint.voltage = status->voltage;
            dataPoint.current = status->current;
            dataPoint.power = status->power;
            
            UpdateGraphs(ctx, &dataPoint);
            ctx->lastGraphUpdate = currentTime;
        }
        
        // Process events and delay
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    // Disable output
    params.outputEnable.enable = 0;
    PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                           PSB_CMD_SET_OUTPUT_ENABLE,
                           &params, PSB_PRIORITY_HIGH, &cmdResult,
                           PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Store final results
    phaseResults->capacity_mAh = ctx->accumulatedCapacity_mAh;
    phaseResults->energy_Wh = ctx->accumulatedEnergy_Wh;
    phaseResults->duration_s = Timer() - ctx->phaseStartTime;
    
    // Calculate averages
    if (phaseResults->dataPoints > 0) {
        phaseResults->avgCurrent = phaseResults->sumCurrent / phaseResults->dataPoints;
        phaseResults->avgVoltage = phaseResults->sumVoltage / phaseResults->dataPoints;
    }
    
    // Close CSV file
    fclose(ctx->csvFile);
    ctx->csvFile = NULL;
    
    // Log final capacity
    LogMessage("%s phase completed - Capacity: %.2f mAh, Energy: %.2f Wh", 
              phaseName, phaseResults->capacity_mAh, phaseResults->energy_Wh);
    
    return (ctx->state == CAPACITY_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static int LogDataPoint(CapacityTestContext *ctx, CapacityDataPoint *point) {
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
        UpdatePhaseResults(ctx, results, point, capacityIncrement, energyIncrement);
    }
    
    // Store for next calculation
    ctx->lastCurrent = point->current;
    ctx->lastTime = point->time;
    ctx->dataPointCount++;
    
    return SUCCESS;
}

static void UpdatePhaseResults(CapacityTestContext *ctx, PhaseResults *results, 
                               CapacityDataPoint *point, double capacityIncrement, double energyIncrement) {
    results->dataPoints++;
    results->sumCurrent += fabs(point->current);
    results->sumVoltage += point->voltage;
}

static void UpdateGraphs(CapacityTestContext *ctx, CapacityDataPoint *point) {
    // Plot current point on graph 1
    PlotPoint(ctx->mainPanelHandle, ctx->graph1Handle, 
              point->time, fabs(point->current), 
              VAL_SOLID_CIRCLE, VAL_RED);
    
    // Plot voltage point on graph 2
    PlotPoint(ctx->mainPanelHandle, ctx->graph2Handle, 
              point->time, point->voltage, 
              VAL_SOLID_CIRCLE, VAL_BLUE);
    
    // Check if Y-axis needs rescaling
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

static void ClearGraphs(CapacityTestContext *ctx) {
    int graphs[] = {ctx->graph1Handle, ctx->graph2Handle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 2);
}

static int WriteResultsFile(CapacityTestContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    SAFE_SPRINTF(filename, sizeof(filename), "%s%s%s", 
                ctx->testDirectory, PATH_SEPARATOR, CAPACITY_TEST_RESULTS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create results file");
        return ERR_BASE_FILE;
    }
    
    // Get timestamp for test start
    time_t startTime = (time_t)ctx->testStartTime;
    char startTimeStr[64];
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%dT%H:%M:%S", localtime(&startTime));
    
    // Get timestamp for test end
    time_t endTime = (time_t)ctx->testEndTime;
    char endTimeStr[64];
    strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%dT%H:%M:%S", localtime(&endTime));
    
    // Write header
    fprintf(file, "# Battery Capacity Test Results\n");
    fprintf(file, "# Generated by Battery Tester v%s\n\n", PROJECT_VERSION);
    
    // Test Information
    WriteINISection(file, "Test_Information");
    WriteINIValue(file, "Test_Start_Time", "%s", startTimeStr);
    WriteINIValue(file, "Test_End_Time", "%s", endTimeStr);
    WriteINIDouble(file, "Total_Duration_s", ctx->testEndTime - ctx->testStartTime, 1);
    fprintf(file, "\n");
    
    // Test Parameters
    WriteINISection(file, "Test_Parameters");
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
    double coulombicEff = CalculateCoulombicEfficiency(ctx->chargeResults.capacity_mAh, 
                                                       ctx->dischargeResults.capacity_mAh);
    double energyEff = CalculateEnergyEfficiency(ctx->chargeResults.energy_Wh,
                                               ctx->dischargeResults.energy_Wh);
    
    WriteINISection(file, "Calculated_Metrics");
    WriteINIDouble(file, "Coulombic_Efficiency_Percent", coulombicEff, 1);
    WriteINIDouble(file, "Round_Trip_Energy_Efficiency_Percent", energyEff, 1);
    WriteINIDouble(file, "Capacity_Retention_Percent", coulombicEff, 1); // Same as coulombic for single cycle
    
    fclose(file);
    
    LogMessage("Results written to: %s", filename);
    return SUCCESS;
}

static void RestoreUI(CapacityTestContext *ctx) {
    // Re-enable controls
    DimCapacityExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, controls, numControls);
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void CapacityTest_Cleanup(void) {
    if (CapacityTest_IsRunning()) {
        CapacityTest_Abort();
    }
}

int CapacityTest_Abort(void) {
    if (CapacityTest_IsRunning()) {
        g_testContext.state = CAPACITY_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_testThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_testThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_testThreadId = 0;
        }
    }
    return SUCCESS;
}