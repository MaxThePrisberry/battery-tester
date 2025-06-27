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

/******************************************************************************
 * Module Variables
 ******************************************************************************/

// Test context and thread management
// State machine:
//   IDLE -> PREPARING -> DISCHARGING -> CHARGING -> COMPLETED
//   Any state can transition to CANCELLED (user stop) or ERROR (failure)
//   IDLE is only the initial state before any test has run
static CapacityTestContext g_testContext = {0};
static CmtThreadFunctionID g_testThreadId = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CVICALLBACK CapacityTestThread(void *functionData);
static int VerifyBatteryCharged(CapacityTestContext *ctx);
static int ConfigureGraphs(CapacityTestContext *ctx);
static int RunTestPhase(CapacityTestContext *ctx, CapacityTestPhase phase);
static int LogDataPoint(CapacityTestContext *ctx, CapacityDataPoint *point);
static void UpdateGraphs(CapacityTestContext *ctx, CapacityDataPoint *point);
static void ClearGraphs(CapacityTestContext *ctx);
static int SaveCapacityResult(CapacityTestContext *ctx);
static void RestoreUI(CapacityTestContext *ctx);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if capacity test is already running by examining the state
    // We use the test context state instead of a separate flag since g_systemBusy
    // is used for all operations, not just the capacity test
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
    g_testContext.mainPanelHandle = g_mainPanelHandle;  // Store main panel handle
    g_testContext.tabPanelHandle = panel;               // Store tab panel handle
    g_testContext.buttonControl = control;              // Store button control ID
    g_testContext.statusControl = PANEL_STR_PSB_STATUS;
    g_testContext.psbHandle = psbHandle;
    g_testContext.graph1Handle = PANEL_GRAPH_1;
    g_testContext.graph2Handle = PANEL_GRAPH_2;
    
    // Read test parameters from UI (these controls are on the main panel)
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_testContext.params.chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_testContext.params.dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_testContext.params.chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_testContext.params.dischargeCurrent);
    GetCtrlVal(panel, CAPACITY_NUM_CURRENT_THRESHOLD, &g_testContext.params.currentThreshold);
    GetCtrlVal(panel, CAPACITY_NUM_INTERVAL, &g_testContext.params.logInterval);
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Dim appropriate controls
    DimCapacityExperimentControls(g_mainPanelHandle, panel, 1,
                                  CAPACITY_NUM_CURRENT_THRESHOLD,
                                  CAPACITY_NUM_INTERVAL,
                                  CAPACITY_CHECKBOX_RETURN_50);
    
    // Start test thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CapacityTestThread, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        // Failed to start thread
        g_testContext.state = CAPACITY_STATE_ERROR;  // Set error state
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimCapacityExperimentControls(g_mainPanelHandle, panel, 0,
                                      CAPACITY_NUM_CURRENT_THRESHOLD,
                                      CAPACITY_NUM_INTERVAL,
                                      CAPACITY_CHECKBOX_RETURN_50);  // Re-enable controls
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start capacity test thread.");
        return 0;
    }
    
    return 0;
}

int CapacityTest_IsRunning(void) {
    // Test is running if it's NOT in one of these terminal states
    return !(g_testContext.state == CAPACITY_STATE_IDLE ||
             g_testContext.state == CAPACITY_STATE_COMPLETED ||
             g_testContext.state == CAPACITY_STATE_ERROR ||
             g_testContext.state == CAPACITY_STATE_CANCELLED);
}

/******************************************************************************
 * Test Thread Implementation
 ******************************************************************************/

static int CVICALLBACK CapacityTestThread(void *functionData) {
    CapacityTestContext *ctx = (CapacityTestContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    double chargeCapacity_mAh = 0.0;  // Store charge capacity for 50% return
    
    LogMessage("=== Starting Battery Capacity Test ===");
    
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
    
    // Initialize PSB to safe state with wide limits
    LogMessage("Initializing PSB to safe state...");
    result = PSB_InitializeSafeLimits(NULL);
    
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
    
    // Now set operating parameters for this test
    LogMessage("Configuring test parameters...");
    
    // Set voltage to midpoint (limits are already wide from initialization)
    double midVoltage = (ctx->params.chargeVoltage + ctx->params.dischargeVoltage) / 2.0;
    result = PSB_SetVoltageQueued(NULL, midVoltage);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage to %.2fV: %s", midVoltage, PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set voltage.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Narrow voltage limits to test range
    result = PSB_SetVoltageLimitsQueued(NULL, ctx->params.dischargeVoltage, ctx->params.chargeVoltage);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage limits (%.2f-%.2fV): %s", 
                ctx->params.dischargeVoltage, ctx->params.chargeVoltage, PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set voltage limits.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Narrow current limits (current is already 0A from initialization)
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    result = PSB_SetCurrentLimitsQueued(NULL, 0.0, maxCurrent * 1.1);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current limits: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set current limits.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Narrow sink current limits (sink current is already 0A from initialization)
    result = PSB_SetSinkCurrentLimitsQueued(NULL, 0.0, ctx->params.dischargeCurrent * 1.1);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink current limits: %s", PSB_GetErrorString(result));
        // Continue anyway as sink mode might not be available
    }
    
    // Narrow power limits (power is already 0W from initialization)
    result = PSB_SetPowerLimitQueued(NULL, CAPACITY_TEST_POWER_LIMIT_W);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power limit: %s", PSB_GetErrorString(result));
    }
    
    result = PSB_SetSinkPowerLimitQueued(NULL, CAPACITY_TEST_POWER_LIMIT_W);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power limit: %s", PSB_GetErrorString(result));
    }
    
    LogMessage("Test parameters configured successfully");
    
    // Check for cancellation
    if (ctx->state == CAPACITY_STATE_CANCELLED) {
        LogMessage("Capacity test cancelled after configuration");
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
    for (int i = 0; i < 20; i++) {  // 2 seconds in 100ms chunks
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
    chargeCapacity_mAh = ctx->accumulatedCapacity_mAh;
    
    ctx->state = CAPACITY_STATE_COMPLETED;
    LogMessage("=== Battery Capacity Test Completed Successfully ===");
    
    // Check if we should return to 50% capacity (only if test completed successfully)
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
                .timeoutSeconds = 3600.0,  // 1 hour max
                .updateIntervalMs = 1000,   // Update every second
                .panelHandle = ctx->mainPanelHandle,
                .statusControl = ctx->statusControl,
                .progressControl = 0,  // No progress bar
                .progressCallback = NULL,
                .statusCallback = NULL
            };
            
            // Perform the discharge
            int dischargeResult = Battery_DischargeCapacity(ctx->psbHandle, &discharge50);
            
            if (dischargeResult == SUCCESS && discharge50.result == BATTERY_OP_SUCCESS) {
                LogMessage("Successfully returned battery to 50%% capacity");
                LogMessage("  Discharged: %.2f mAh (target: %.2f mAh)", 
                          discharge50.actualDischarged_mAh, discharge50.targetCapacity_mAh);
                LogMessage("  Time taken: %.1f minutes", discharge50.elapsedTime_s / 60.0);
                LogMessage("  Final voltage: %.3f V", discharge50.finalVoltage_V);
                
                SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, 
                          "Capacity test completed - battery at 50% capacity");
            } else {
                const char *reason = "Unknown error";
                switch (discharge50.result) {
                    case BATTERY_OP_TIMEOUT: reason = "Timeout"; break;
                    case BATTERY_OP_CURRENT_THRESHOLD: reason = "Current below threshold"; break;
                    case BATTERY_OP_ERROR: reason = "Communication error"; break;
                    case BATTERY_OP_ABORTED: reason = "Aborted"; break;
                }
                
                LogWarning("Failed to return to 50%% capacity: %s", reason);
                LogWarning("  Discharged: %.2f mAh before stopping", discharge50.actualDischarged_mAh);
                
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
        if (GetCtrlVal(ctx->tabPanelHandle, CAPACITY_CHECKBOX_RETURN_50, &response) == 0 && 
            response && chargeCapacity_mAh > 0) {
            // Status already set by 50% return logic
        } else {
            SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test completed");
        }
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
    
    // Update state
    ctx->state = (phase == CAPACITY_PHASE_DISCHARGE) ? 
                CAPACITY_STATE_DISCHARGING : CAPACITY_STATE_CHARGING;
    ctx->capacityControl = capacityControl;
    
    // Open CSV file
    SAFE_SPRINTF(filename, sizeof(filename), "%s.csv", phaseName);
    ctx->csvFile = fopen(filename, "w");
    if (!ctx->csvFile) {
        LogError("Failed to create %s file", filename);
        return ERR_BASE_FILE;
    }
    
    // Write CSV header
    fprintf(ctx->csvFile, "Time (s),Voltage (V),Current (A),Power (W)\n");
    
    // Set target voltage (limits already set in initialization)
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
    for (int i = 0; i < 20; i++) {  // 2 seconds in 100ms chunks
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
    ctx->lastCurrent = 0.0;
    ctx->lastTime = 0.0;
    ctx->dataPointCount = 0;
    
    // Reset capacity display
    SetCtrlVal(ctx->tabPanelHandle, capacityControl, 0.0);
    
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
        PSBCommandParams statusParams = {0};
        PSBCommandResult statusResult = {0};
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
    
    // Save final capacity
    SaveCapacityResult(ctx);
    
    // Close CSV file
    fclose(ctx->csvFile);
    ctx->csvFile = NULL;
    
    // Log final capacity
    LogMessage("%s phase completed - Capacity: %.2f mAh", 
              phaseName, ctx->accumulatedCapacity_mAh);
    
    return (ctx->state == CAPACITY_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static int LogDataPoint(CapacityTestContext *ctx, CapacityDataPoint *point) {
    // Write to CSV
    fprintf(ctx->csvFile, "%.3f,%.3f,%.3f,%.3f\n", 
            point->time, point->voltage, point->current, point->power);
    fflush(ctx->csvFile);
    
    // Calculate capacity increment using the battery_utils function
    if (ctx->dataPointCount > 0) {
        double deltaTime = point->time - ctx->lastTime;
        double capacityIncrement = Battery_CalculateCapacityIncrement(
            fabs(ctx->lastCurrent), fabs(point->current), deltaTime);
        
        ctx->accumulatedCapacity_mAh += capacityIncrement;
        
        // Update capacity display
        SetCtrlVal(ctx->tabPanelHandle, ctx->capacityControl, ctx->accumulatedCapacity_mAh);
    }
    
    // Store for next calculation
    ctx->lastCurrent = point->current;
    ctx->lastTime = point->time;
    ctx->dataPointCount++;
    
    return SUCCESS;
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
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph1Handle, -1, VAL_IMMEDIATE_DRAW);
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graph2Handle, -1, VAL_IMMEDIATE_DRAW);
}

static int SaveCapacityResult(CapacityTestContext *ctx) {
    if (ctx->csvFile) {
        // Add empty line and capacity result
        fprintf(ctx->csvFile, "\n");
        fprintf(ctx->csvFile, "Total Capacity (mAh),%.2f\n", ctx->accumulatedCapacity_mAh);
        fflush(ctx->csvFile);
    }
    return SUCCESS;
}

static void RestoreUI(CapacityTestContext *ctx) {
    // Re-enable controls
    DimCapacityExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0,
                                  CAPACITY_NUM_CURRENT_THRESHOLD,
                                  CAPACITY_NUM_INTERVAL,
                                  CAPACITY_CHECKBOX_RETURN_50);
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

int CapacityTest_Initialize(void) {
    memset(&g_testContext, 0, sizeof(g_testContext));
    g_testContext.state = CAPACITY_STATE_IDLE;
    return SUCCESS;
}

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