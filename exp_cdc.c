/******************************************************************************
 * exp_cdc.c
 * 
 * Battery Charge/Discharge Control (CDC) Experiment Module Implementation
 ******************************************************************************/

#include "common.h"
#include "exp_cdc.h"
#include "BatteryTester.h"
#include "logging.h"
#include "status.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

// Test context and thread management
static CDCTestContext g_testContext = {0};
static CmtThreadFunctionID g_testThreadId = 0;

// Control arrays for dimming
static const int g_cdcControls[] = {
    CDC_NUM_CURRENT_THRESHOLD,
    CDC_NUM_INTERVAL,
    CDC_BTN_CHARGE,
    CDC_BTN_DISCHARGE
};
static const int g_numCdcControls = sizeof(g_cdcControls) / sizeof(g_cdcControls[0]);

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int CDCExperimentThread(void *functionData);
static int StartCDCOperation(int panel, int control, CDCOperationMode mode);
static int VerifyBatteryState(CDCTestContext *ctx);
static int RunOperation(CDCTestContext *ctx);
static void UpdateGraph(CDCTestContext *ctx, double current, double time);
static void ClearGraph(CDCTestContext *ctx);
static void RestoreUI(CDCTestContext *ctx);
static const char* GetModeName(CDCOperationMode mode);

/******************************************************************************
 * Public Functions Implementation
 ******************************************************************************/

int CVICALLBACK CDCChargeCallback(int panel, int control, int event,
                                 void *callbackData, int eventData1, 
                                 int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    return StartCDCOperation(panel, control, CDC_MODE_CHARGE);
}

int CVICALLBACK CDCDischargeCallback(int panel, int control, int event,
                                    void *callbackData, int eventData1, 
                                    int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    return StartCDCOperation(panel, control, CDC_MODE_DISCHARGE);
}

int CDCTest_IsRunning(void) {
    return !(g_testContext.state == CDC_STATE_IDLE ||
             g_testContext.state == CDC_STATE_COMPLETED ||
             g_testContext.state == CDC_STATE_ERROR ||
             g_testContext.state == CDC_STATE_CANCELLED);
}

int CDCTest_GetMode(void) {
    if (CDCTest_IsRunning()) {
        return g_testContext.mode;
    }
    return -1;
}

/******************************************************************************
 * Common Start Function
 ******************************************************************************/

static int StartCDCOperation(int panel, int control, CDCOperationMode mode) {
    // Check if CDC operation is already running
    if (CDCTest_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop %s", GetModeName(g_testContext.mode));
        g_testContext.state = CDC_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting.");
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
                     "Please ensure it is connected before running.");
        return 0;
    }
    
    PSB_Handle *psbHandle = PSB_QueueGetHandle(psbQueueMgr);
    if (!psbHandle || !psbHandle->isConnected) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("PSB Not Connected", 
                     "The PSB power supply is not connected.\n"
                     "Please ensure it is connected before running.");
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
                         "The PSB output must be disabled before starting.\n"
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
    g_testContext.state = CDC_STATE_PREPARING;
    g_testContext.mode = mode;
    g_testContext.mainPanelHandle = g_mainPanelHandle;
    g_testContext.tabPanelHandle = panel;
    g_testContext.activeButtonControl = control;
    g_testContext.psbHandle = psbHandle;
    g_testContext.graphHandle = PANEL_GRAPH_1;
    
    // Read test parameters from UI based on mode
    if (mode == CDC_MODE_CHARGE) {
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_testContext.params.targetVoltage);
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_testContext.params.targetCurrent);
    } else {
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_testContext.params.targetVoltage);
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_testContext.params.targetCurrent);
    }
    
    GetCtrlVal(panel, CDC_NUM_CURRENT_THRESHOLD, &g_testContext.params.currentThreshold);
    GetCtrlVal(panel, CDC_NUM_INTERVAL, &g_testContext.params.logInterval);
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Dim appropriate controls (but NOT the active button)
    int controlsToDim[g_numCdcControls - 1];
    int dimCount = 0;
    
    // Add all controls except the active button
    for (int i = 0; i < g_numCdcControls; i++) {
        if (g_cdcControls[i] != control) {
            controlsToDim[dimCount++] = g_cdcControls[i];
        }
    }
    
    DimCapacityExperimentControls(g_mainPanelHandle, panel, 1, 
                                  controlsToDim, dimCount);
    
    // Start test thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CDCExperimentThread, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        // Failed to start thread
        g_testContext.state = CDC_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, 
                        mode == CDC_MODE_CHARGE ? "Charge" : "Discharge");
        DimCapacityExperimentControls(g_mainPanelHandle, panel, 0, 
                                     (int*)g_cdcControls, g_numCdcControls);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start CDC thread.");
        return 0;
    }
    
    return 0;
}

/******************************************************************************
 * Test Thread Implementation
 ******************************************************************************/

static int CDCExperimentThread(void *functionData) {
    CDCTestContext *ctx = (CDCTestContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting %s Operation ===", GetModeName(ctx->mode));
    
    // Record test start time
    ctx->testStartTime = Timer();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == CDC_STATE_CANCELLED) {
        LogMessage("CDC operation cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    SAFE_SPRINTF(message, sizeof(message),
        "%s Operation Parameters:\n\n"
        "Target Voltage: %.2f V\n"
        "Target Current: %.2f A\n"
        "Current Threshold: %.3f A\n"
        "Log Interval: %d seconds\n\n"
        "Please confirm these parameters are correct.",
        GetModeName(ctx->mode),
        ctx->params.targetVoltage,
        ctx->params.targetCurrent,
        ctx->params.currentThreshold,
        ctx->params.logInterval);
    
    int response = ConfirmPopup("Confirm Parameters", message);
    if (!response || ctx->state == CDC_STATE_CANCELLED) {
        LogMessage("CDC operation cancelled by user");
        ctx->state = CDC_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Initialize PSB to safe state
    LogMessage("Initializing PSB to zeroed state...");
    result = PSB_ZeroAllValues(NULL);
    
    if (result != PSB_SUCCESS) {
        LogError("Failed to initialize PSB to safe state: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to initialize PSB to safe state.\nPlease check the connection and try again.");
        ctx->state = CDC_STATE_ERROR;
        goto cleanup;
    }
    
    // Check for cancellation
    if (ctx->state == CDC_STATE_CANCELLED) {
        LogMessage("CDC operation cancelled during initialization");
        goto cleanup;
    }
    
    // Verify battery state
    result = VerifyBatteryState(ctx);
    if (result != SUCCESS || ctx->state == CDC_STATE_CANCELLED) {
        if (ctx->state != CDC_STATE_CANCELLED) {
            ctx->state = CDC_STATE_CANCELLED;
        }
        goto cleanup;
    }
    
    // Configure graph
    ConfigureGraph(ctx->mainPanelHandle, ctx->graphHandle, 
           "Current vs Time", "Time (s)", "Current (A)", 
           0.0, ctx->params.targetCurrent * 1.1);
	
	// Clear any existing plots
	DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphHandle, -1, VAL_DELAYED_DRAW);
    
    // Run the operation
    LogMessage("Starting %s operation...", GetModeName(ctx->mode));
    SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, 
               ctx->mode == CDC_MODE_CHARGE ? "Charging battery..." : "Discharging battery...");
    
    result = RunOperation(ctx);
    if (result != SUCCESS || ctx->state == CDC_STATE_CANCELLED) {
        if (ctx->state != CDC_STATE_CANCELLED) {
            ctx->state = CDC_STATE_ERROR;
        }
        goto cleanup;
    }
    
    ctx->state = CDC_STATE_COMPLETED;
    LogMessage("=== %s Operation Completed Successfully ===", GetModeName(ctx->mode));
    
cleanup:
    // Turn off PSB output
    PSBCommandParams offParams = {0};
    PSBCommandResult offResult = {0};
    offParams.outputEnable.enable = 0;
    PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), PSB_CMD_SET_OUTPUT_ENABLE,
                           &offParams, PSB_PRIORITY_HIGH, &offResult,
                           PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Update status based on final state
    if (ctx->state == CDC_STATE_COMPLETED) {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, 
                   ctx->mode == CDC_MODE_CHARGE ? "Charge complete" : 
                                                  "Discharge complete");
    } else if (ctx->state == CDC_STATE_CANCELLED) {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Operation cancelled");
    } else {
        SetCtrlVal(ctx->mainPanelHandle, PANEL_STR_PSB_STATUS, "Operation failed");
    }
    
    // Restore button text
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->activeButtonControl, ATTR_LABEL_TEXT, 
                    ctx->mode == CDC_MODE_CHARGE ? "Charge" : "Discharge");
    
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

static int VerifyBatteryState(CDCTestContext *ctx) {
    PSBCommandParams params = {0};
    PSBCommandResult result = {0};
    char message[LARGE_BUFFER_SIZE];
    
    LogMessage("Verifying battery state...");
    
    // Check for cancellation
    if (ctx->state == CDC_STATE_CANCELLED) {
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
    
    double voltageDiff = fabs(result.data.status.voltage - ctx->params.targetVoltage);
    
    LogMessage("Battery voltage: %.3f V, Target: %.3f V, Difference: %.3f V", 
               result.data.status.voltage, ctx->params.targetVoltage, voltageDiff);
    
    // Check if battery is already at target state
    if (voltageDiff < CDC_VOLTAGE_TOLERANCE) {
        const char *stateStr = (ctx->mode == CDC_MODE_CHARGE) ? "charged" : "discharged";
        
        SAFE_SPRINTF(message, sizeof(message),
            "Battery appears to already be %s:\n\n"
            "Current Voltage: %.3f V\n"
            "Target Voltage: %.3f V\n"
            "Difference: %.3f V\n"
            "Tolerance: %.3f V\n\n"
            "Do you want to continue anyway?",
            stateStr,
            result.data.status.voltage,
            ctx->params.targetVoltage,
            voltageDiff,
            CDC_VOLTAGE_TOLERANCE);
        
        // Check for cancellation before showing dialog
        if (ctx->state == CDC_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        
        int response = ConfirmPopup("Battery State", message);
        if (!response || ctx->state == CDC_STATE_CANCELLED) {
            LogMessage("User cancelled due to battery already being %s", stateStr);
            return ERR_CANCELLED;
        }
    }
    
    LogMessage("Battery state verified");
    return SUCCESS;
}

static int RunOperation(CDCTestContext *ctx) {
    PSBCommandParams params = {0};
    PSBCommandResult cmdResult = {0};
    int result;
    
    // Check for cancellation
    if (ctx->state == CDC_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Update state
    ctx->state = CDC_STATE_RUNNING;
    
    // Configure test parameters
    LogMessage("Configuring test parameters...");
    
    // Get all voltage and current values from UI
    double chargeVoltage, dischargeVoltage, chargeCurrent, dischargeCurrent;
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &chargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &dischargeVoltage);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &dischargeCurrent);
    
    // Set both source and sink current values
    result = PSB_SetCurrentQueued(NULL, chargeCurrent);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set source current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    result = PSB_SetSinkCurrentQueued(NULL, dischargeCurrent);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set sink current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    LogMessage("Current values set - Source: %.2fA, Sink: %.2fA", chargeCurrent, dischargeCurrent);
    
    // Now set the actual target voltage
    result = PSB_SetVoltageQueued(NULL, ctx->params.targetVoltage);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set target voltage: %s", PSB_GetErrorString(result));
        return result;
    }
    
    LogMessage("Target voltage set to %.2fV", ctx->params.targetVoltage);
    
    // Set power values high enough to avoid CP mode
    result = PSB_SetPowerQueued(NULL, CDC_POWER_LIMIT_W);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
    
    result = PSB_SetSinkPowerQueued(NULL, CDC_POWER_LIMIT_W);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power: %s", PSB_GetErrorString(result));
    }
    
    // Enable PSB output
    params.outputEnable.enable = 1;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_OUTPUT_ENABLE,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        return result;
    }
    
    LogMessage("Waiting for output to stabilize...");
    
    // Check for cancellation during stabilization
    for (int i = 0; i < 20; i++) {
        if (ctx->state == CDC_STATE_CANCELLED) {
            return ERR_CANCELLED;
        }
        Delay(0.1);
    }
    
    // Initialize timing
    double operationStartTime = Timer();
    ctx->lastLogTime = operationStartTime;
    ctx->lastGraphUpdate = operationStartTime;
    ctx->dataPointCount = 0;
    ctx->peakCurrent = 0.0;
    
    LogMessage("%s started", GetModeName(ctx->mode));
    
    // Main operation loop
    while (1) {
        // Check for cancellation
        if (ctx->state == CDC_STATE_CANCELLED) {
            LogMessage("%s cancelled by user", GetModeName(ctx->mode));
            break;
        }
        
        double currentTime = Timer();
        ctx->elapsedTime = currentTime - operationStartTime;
        
        // Check for timeout
        if (ctx->elapsedTime > (CDC_MAX_DURATION_H * 3600.0)) {
            LogWarning("%s timeout reached", GetModeName(ctx->mode));
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
        
        // Track peak current
        if (fabs(status->current) > ctx->peakCurrent) {
            ctx->peakCurrent = fabs(status->current);
        }
        
        // Check completion criteria
        // For both charge and discharge: voltage at target AND current below threshold
        double voltageDiff = fabs(status->voltage - ctx->params.targetVoltage);
        bool voltageAtTarget = voltageDiff < CDC_VOLTAGE_TOLERANCE;
        bool currentBelowThreshold = fabs(status->current) < ctx->params.currentThreshold;
        
        if (voltageAtTarget && currentBelowThreshold) {
            LogMessage("%s completed - voltage at target (%.3f V) and current below threshold (%.3f A < %.3f A)",
                      GetModeName(ctx->mode), status->voltage, 
                      fabs(status->current), ctx->params.currentThreshold);
            break;
        }
        
        // Log status if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            LogDebug("Time: %.1fs, V: %.3fV, I: %.3fA, P: %.3fW", 
                     ctx->elapsedTime, status->voltage, status->current, status->power);
            ctx->lastLogTime = currentTime;
            ctx->dataPointCount++;
        }
        
        // Update graph if needed
        if ((currentTime - ctx->lastGraphUpdate) >= CDC_GRAPH_UPDATE_RATE) {
            UpdateGraph(ctx, status->current, ctx->elapsedTime);
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
    
    // Log summary
    LogMessage("%s completed - Duration: %.1f minutes, Peak current: %.3f A", 
              GetModeName(ctx->mode), ctx->elapsedTime / 60.0, ctx->peakCurrent);
    
    return (ctx->state == CDC_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static void UpdateGraph(CDCTestContext *ctx, double current, double time) {
    // Plot current point
    PlotPoint(ctx->mainPanelHandle, ctx->graphHandle, 
              time, fabs(current), 
              VAL_SOLID_CIRCLE, VAL_RED);
    
    // Check if Y-axis needs rescaling
    double yMin, yMax;
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graphHandle, VAL_LEFT_YAXIS, 
                       NULL, &yMin, &yMax);
    
    if (fabs(current) > yMax) {
        SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphHandle, VAL_LEFT_YAXIS, 
                          VAL_AUTOSCALE, 0.0, 0.0);
    }
}

static void ClearGraph(CDCTestContext *ctx) {
    int graphs[] = {ctx->graphHandle};
    ClearAllGraphs(ctx->mainPanelHandle, graphs, 1);
}

static void RestoreUI(CDCTestContext *ctx) {
    // Re-enable all controls
    DimCapacityExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, 
                                 (int*)g_cdcControls, g_numCdcControls);
}

static const char* GetModeName(CDCOperationMode mode) {
    return (mode == CDC_MODE_CHARGE) ? "Charge" : "Discharge";
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void CDCTest_Cleanup(void) {
    if (CDCTest_IsRunning()) {
        CDCTest_Abort();
    }
}

int CDCTest_Abort(void) {
    if (CDCTest_IsRunning()) {
        g_testContext.state = CDC_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_testThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_testThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_testThreadId = 0;
        }
    }
    return SUCCESS;
}