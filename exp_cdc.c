/******************************************************************************
 * exp_cdc.c
 * 
 * Battery Charge/Discharge Control (CDC) Experiment Module Implementation
 ******************************************************************************/

#include "common.h"
#include "exp_cdc.h"
#include "BatteryTester.h"
#include "teensy_queue.h"
#include "logging.h"
#include "status.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

// Experiment context and thread management
static CDCExperimentContext g_experimentContext = {0};
static CmtThreadFunctionID g_experimentThreadId = 0;

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
static int VerifyBatteryState(CDCExperimentContext *ctx);
static int RunOperation(CDCExperimentContext *ctx);
static void UpdateGraph(CDCExperimentContext *ctx, double current, double time);
static void RestoreUI(CDCExperimentContext *ctx);
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

int CDCExperiment_IsRunning(void) {
    return !(g_experimentContext.state == CDC_STATE_IDLE ||
             g_experimentContext.state == CDC_STATE_COMPLETED ||
             g_experimentContext.state == CDC_STATE_ERROR ||
             g_experimentContext.state == CDC_STATE_CANCELLED);
}

int CDCExperiment_GetMode(void) {
    if (CDCExperiment_IsRunning()) {
        return g_experimentContext.mode;
    }
    return -1;
}

/******************************************************************************
 * Common Start Function
 ******************************************************************************/

static int StartCDCOperation(int panel, int control, CDCOperationMode mode) {
    // Check if CDC operation is already running
    if (CDCExperiment_IsRunning()) {
        // This is a Stop request
        LogMessage("User requested to stop %s", GetModeName(g_experimentContext.mode));
        g_experimentContext.state = CDC_STATE_CANCELLED;
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
    if (PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL) == PSB_SUCCESS) {
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
    
    // Initialize experiment context
    memset(&g_experimentContext, 0, sizeof(g_experimentContext));
    g_experimentContext.state = CDC_STATE_PREPARING;
    g_experimentContext.mode = mode;
    g_experimentContext.mainPanelHandle = g_mainPanelHandle;
    g_experimentContext.tabPanelHandle = panel;
    g_experimentContext.activeButtonControl = control;
    g_experimentContext.psbHandle = psbHandle;
    g_experimentContext.graphHandle = PANEL_GRAPH_1;
    
    // Read experiment parameters from UI based on mode
    if (mode == CDC_MODE_CHARGE) {
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_V, &g_experimentContext.params.targetVoltage);
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &g_experimentContext.params.targetCurrent);
    } else {
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_V, &g_experimentContext.params.targetVoltage);
        GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &g_experimentContext.params.targetCurrent);
    }
    
    GetCtrlVal(panel, CDC_NUM_CURRENT_THRESHOLD, &g_experimentContext.params.currentThreshold);
    GetCtrlVal(panel, CDC_NUM_INTERVAL, &g_experimentContext.params.logInterval);
    
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
    
    DimExperimentControls(g_mainPanelHandle, panel, 1, 
                                  controlsToDim, dimCount);
    
    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CDCExperimentThread, 
                                            &g_experimentContext, &g_experimentThreadId);
    if (error != 0) {
        // Failed to start thread
        g_experimentContext.state = CDC_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, 
                        mode == CDC_MODE_CHARGE ? "Charge" : "Discharge");
        DimExperimentControls(g_mainPanelHandle, panel, 0, 
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
 * Experiment Thread Implementation
 ******************************************************************************/

static int CDCExperimentThread(void *functionData) {
    CDCExperimentContext *ctx = (CDCExperimentContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting %s Operation ===", GetModeName(ctx->mode));
    
    // Record experiment start time
    ctx->experimentStartTime = GetTimestamp();
    
    // Check if cancelled before showing confirmation
    if (ctx->state == CDC_STATE_CANCELLED) {
        LogMessage("CDC operation cancelled before confirmation");
        goto cleanup;
    }
    
    // Show confirmation popup
    snprintf(message, sizeof(message),
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
    result = PSB_ZeroAllValuesQueued(DEVICE_PRIORITY_NORMAL);
    
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
    
    // Configure graph using common utility
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
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
	
	// Disconnect PSB to battery using Teensy relay
	result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB via relay: %s", PSB_GetErrorString(result));
        return result;
    }
    
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
    g_experimentThreadId = 0;
    
    return 0;
}

/******************************************************************************
 * Helper Functions Implementation
 ******************************************************************************/

static int VerifyBatteryState(CDCExperimentContext *ctx) {
    char message[LARGE_BUFFER_SIZE];
    
    LogMessage("Verifying battery state...");
    
    // Check for cancellation
    if (ctx->state == CDC_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Get current battery voltage
	PSB_Status status;
    int error = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
	
    if (error != PSB_SUCCESS) {
        LogError("Failed to read PSB status: %s", PSB_GetErrorString(error));
        return ERR_COMM_FAILED;
    }
    
    double voltageDiff = fabs(status.voltage - ctx->params.targetVoltage);
    
    LogMessage("Battery voltage: %.3f V, Target: %.3f V, Difference: %.3f V", 
               status.voltage, ctx->params.targetVoltage, voltageDiff);
    
    // Check if battery is already at target state
    if (voltageDiff < CDC_VOLTAGE_TOLERANCE) {
        const char *stateStr = (ctx->mode == CDC_MODE_CHARGE) ? "charged" : "discharged";
        
        snprintf(message, sizeof(message),
            "Battery appears to already be %s:\n\n"
            "Current Voltage: %.3f V\n"
            "Target Voltage: %.3f V\n"
            "Difference: %.3f V\n"
            "Tolerance: %.3f V\n\n"
            "Do you want to continue anyway?",
            stateStr,
            status.voltage,
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

static int RunOperation(CDCExperimentContext *ctx) {
    int result;
    
    // Check for cancellation
    if (ctx->state == CDC_STATE_CANCELLED) {
        return ERR_CANCELLED;
    }
    
    // Update state
    ctx->state = CDC_STATE_RUNNING;
    
    // Configure experiment parameters
    LogMessage("Configuring experiment parameters...");
    
    // Get charge and discharge current values from UI (both needed for PSB configuration)
    double chargeCurrent, dischargeCurrent;
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_CHARGE_I, &chargeCurrent);
    GetCtrlVal(g_mainPanelHandle, PANEL_NUM_SET_DISCHARGE_I, &dischargeCurrent);
    
    // Set both source and sink current values to allow backflow
    result = PSB_SetCurrentQueued(chargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set source current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    result = PSB_SetSinkCurrentQueued(dischargeCurrent, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set sink current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    LogMessage("Current values set - Source: %.2fA, Sink: %.2fA", chargeCurrent, dischargeCurrent);
    
    // Set the target voltage from context (already read in StartCDCOperation)
    result = PSB_SetVoltageQueued(ctx->params.targetVoltage, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set target voltage: %s", PSB_GetErrorString(result));
        return result;
    }
    
    LogMessage("Target voltage set to %.2fV", ctx->params.targetVoltage);
    
    // Set power values high enough to avoid CP mode
    result = PSB_SetPowerQueued(CDC_POWER_LIMIT_W, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
    
    result = PSB_SetSinkPowerQueued(CDC_POWER_LIMIT_W, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power: %s", PSB_GetErrorString(result));
    }
	
	// Connect PSB to battery using Teensy relay
	result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to connect PSB via relay: %s", PSB_GetErrorString(result));
        return result;
    }
	
    // Enable PSB output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
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
    double operationStartTime = GetTimestamp();
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
        
        double currentTime = GetTimestamp();
        ctx->elapsedTime = currentTime - operationStartTime;
        
        // Check for timeout
        if (ctx->elapsedTime > (CDC_MAX_DURATION_H * 3600.0)) {
            LogWarning("%s timeout reached", GetModeName(ctx->mode));
            break;
        }
        
        // Get current status
		PSB_Status status;
        result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
		
        if (result != PSB_SUCCESS) {
            LogError("Failed to read status: %s", PSB_GetErrorString(result));
            break;
        }
        
        // Track peak current
        if (fabs(status.current) > ctx->peakCurrent) {
            ctx->peakCurrent = fabs(status.current);
        }
        
        // Check completion criteria
        // For both charge and discharge: voltage at target AND current below threshold
        double voltageDiff = fabs(status.voltage - ctx->params.targetVoltage);
        bool voltageAtTarget = voltageDiff < CDC_VOLTAGE_TOLERANCE;
        bool currentBelowThreshold = fabs(status.current) < ctx->params.currentThreshold;
        
        if (voltageAtTarget && currentBelowThreshold) {
            LogMessage("%s completed - voltage at target (%.3f V) and current below threshold (%.3f A < %.3f A)",
                      GetModeName(ctx->mode), status.voltage, 
                      fabs(status.current), ctx->params.currentThreshold);
            break;
        }
        
        // Log status if interval reached
        if ((currentTime - ctx->lastLogTime) >= ctx->params.logInterval) {
            LogDebug("Time: %.1fs, V: %.3fV, I: %.3fA, P: %.3fW", 
                     ctx->elapsedTime, status.voltage, status.current, status.power);
            ctx->lastLogTime = currentTime;
            ctx->dataPointCount++;
        }
        
        // Update graph if needed
        if ((currentTime - ctx->lastGraphUpdate) >= CDC_GRAPH_UPDATE_RATE) {
            UpdateGraph(ctx, status.current, ctx->elapsedTime);
            ctx->lastGraphUpdate = currentTime;
        }
        
        // Process events and delay
        ProcessSystemEvents();
        Delay(0.1);
    }
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
	
	// Disconnect PSB to battery using Teensy relay
	result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB via relay: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Log summary
    LogMessage("%s completed - Duration: %.1f minutes, Peak current: %.3f A", 
              GetModeName(ctx->mode), ctx->elapsedTime / 60.0, ctx->peakCurrent);
    
    return (ctx->state == CDC_STATE_CANCELLED) ? ERR_CANCELLED : SUCCESS;
}

static void UpdateGraph(CDCExperimentContext *ctx, double current, double time) {
    // Use common utility function for plotting
    PlotDataPoint(ctx->mainPanelHandle, ctx->graphHandle, 
                  time, fabs(current), VAL_SOLID_CIRCLE, VAL_RED);
    
    // Check if Y-axis needs rescaling
    double yMin, yMax;
    GetAxisScalingMode(ctx->mainPanelHandle, ctx->graphHandle, VAL_LEFT_YAXIS, 
                       NULL, &yMin, &yMax);
    
    if (fabs(current) > yMax) {
        SetAxisScalingMode(ctx->mainPanelHandle, ctx->graphHandle, VAL_LEFT_YAXIS, 
                          VAL_AUTOSCALE, 0.0, 0.0);
    }
}

static void RestoreUI(CDCExperimentContext *ctx) {
    // Re-enable all controls
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0, 
                                 (int*)g_cdcControls, g_numCdcControls);
}

static const char* GetModeName(CDCOperationMode mode) {
    return (mode == CDC_MODE_CHARGE) ? "Charge" : "Discharge";
}

/******************************************************************************
 * Module Management Functions
 ******************************************************************************/

void CDCExperiment_Cleanup(void) {
    if (CDCExperiment_IsRunning()) {
        CDCExperiment_Abort();
    }
}

int CDCExperiment_Abort(void) {
    if (CDCExperiment_IsRunning()) {
        g_experimentContext.state = CDC_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_experimentThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_experimentThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_experimentThreadId = 0;
        }
    }
    return SUCCESS;
}