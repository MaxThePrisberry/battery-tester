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
#include <ansi_c.h>
#include <analysis.h>
#include <utility.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

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
static double CalculateCapacityIncrement(double current1, double current2, double deltaTime);
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
    
    // Disable UI controls on main panel
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 1);
    SetCtrlAttribute(g_mainPanelHandle, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 1);
    SetCtrlAttribute(g_mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 1);
    SetCtrlAttribute(panel, control, ATTR_DIMMED, 1);  // This button is on the tab panel
    
    // Start test thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, CapacityTestThread, 
                                            &g_testContext, &g_testThreadId);
    if (error != 0) {
        // Failed to start thread
        RestoreUI(&g_testContext);
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start capacity test thread.");
        return 0;
    }
    
    return 0;
}

int CapacityTest_IsRunning(void) {
    return (g_testContext.state == CAPACITY_STATE_DISCHARGING || 
            g_testContext.state == CAPACITY_STATE_CHARGING) ? 1 : 0;
}

/******************************************************************************
 * Test Thread Implementation
 ******************************************************************************/

static int CVICALLBACK CapacityTestThread(void *functionData) {
    CapacityTestContext *ctx = (CapacityTestContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting Battery Capacity Test ===");
    
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
    if (!response) {
        LogMessage("Capacity test cancelled by user");
        ctx->state = CAPACITY_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Initialize PSB to safe state with wide limits
    LogMessage("Initializing PSB to safe state...");
    result = PSB_InitializeSafeLimits(NULL,  // Use global queue manager
                                    false,    // Don't set operating limits
                                    0,        // Not used
                                    0,        // Not used
                                    0);       // Not used
    
    if (result != PSB_SUCCESS) {
        LogError("Failed to initialize PSB to safe state: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to initialize PSB to safe state.\nPlease check the connection and try again.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Now set operating parameters for this test
    PSBCommandParams params = {0};
    PSBCommandResult cmdResult = {0};
    
    // First set voltage to midpoint between charge and discharge voltages
    double midVoltage = (ctx->params.chargeVoltage + ctx->params.dischargeVoltage) / 2.0;
    params.setVoltage.voltage = midVoltage;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_VOLTAGE,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set initial voltage to %.2fV: %s", midVoltage, PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set initial voltage.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Now set voltage limits for the test
    params.voltageLimits.minVoltage = ctx->params.dischargeVoltage;
    params.voltageLimits.maxVoltage = ctx->params.chargeVoltage;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_VOLTAGE_LIMITS,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage limits (%.2f-%.2fV): %s", 
                ctx->params.dischargeVoltage, ctx->params.chargeVoltage, PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set voltage limits.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Set current limits
    double maxCurrent = MAX(ctx->params.chargeCurrent, ctx->params.dischargeCurrent);
    params.currentLimits.minCurrent = 0.0;
    params.currentLimits.maxCurrent = maxCurrent * 1.1;  // 10% margin
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_CURRENT_LIMITS,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current limits: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set current limits.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Set sink current limits
    params.sinkCurrentLimits.minCurrent = 0.0;
    params.sinkCurrentLimits.maxCurrent = ctx->params.dischargeCurrent * 1.1;  // 10% margin
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_SINK_CURRENT_LIMITS,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink current limits: %s", PSB_GetErrorString(result));
    }
    
	// Set source power for charging
    params.setPower.power = CAPACITY_TEST_POWER_LIMIT_W;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_POWER,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set source power: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set source power.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    LogMessage("Set source power to %.1f W", CAPACITY_TEST_POWER_LIMIT_W);
    
    // Set sink power for discharging
    params.setSinkPower.power = CAPACITY_TEST_POWER_LIMIT_W;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_SINK_POWER,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set sink power: %s", PSB_GetErrorString(result));
        MessagePopup("Error", "Failed to set sink power.");
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    LogMessage("Set sink power to %.1f W", CAPACITY_TEST_POWER_LIMIT_W);
	
    // Set power limit
    params.powerLimit.maxPower = CAPACITY_TEST_POWER_LIMIT_W;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_POWER_LIMIT,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power limit: %s", PSB_GetErrorString(result));
    }
    
    // Set sink power limit
    params.sinkPowerLimit.maxPower = CAPACITY_TEST_POWER_LIMIT_W;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_SINK_POWER_LIMIT,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power limit: %s", PSB_GetErrorString(result));
    }
    
    // Verify battery is charged
    result = VerifyBatteryCharged(ctx);
    if (result != SUCCESS) {
        ctx->state = CAPACITY_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Configure graphs
    ConfigureGraphs(ctx);
    
    // Run discharge phase
    LogMessage("Starting discharge phase...");
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Discharging battery...");
    
    result = RunTestPhase(ctx, CAPACITY_PHASE_DISCHARGE);
    if (result != SUCCESS) {
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    // Clear graphs between phases
    ClearGraphs(ctx);
    
    // Short delay between phases
    LogMessage("Switching from discharge to charge phase...");
    Delay(2.0);
    
    // Run charge phase
    LogMessage("Starting charge phase...");
    SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Charging battery...");
    
    result = RunTestPhase(ctx, CAPACITY_PHASE_CHARGE);
    if (result != SUCCESS) {
        ctx->state = CAPACITY_STATE_ERROR;
        goto cleanup;
    }
    
    ctx->state = CAPACITY_STATE_COMPLETED;
    LogMessage("=== Battery Capacity Test Completed Successfully ===");
    
cleanup:
    // Turn off PSB output
    PSBCommandParams offParams = {0};
    PSBCommandResult offResult = {0};
    offParams.outputEnable.enable = 0;
    PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), PSB_CMD_SET_OUTPUT_ENABLE,
                           &offParams, PSB_PRIORITY_HIGH, &offResult,
                           PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Restore UI
    RestoreUI(ctx);
    
    // Update status
    if (ctx->state == CAPACITY_STATE_COMPLETED) {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test completed");
    } else if (ctx->state == CAPACITY_STATE_CANCELLED) {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test cancelled");
    } else {
        SetCtrlVal(ctx->mainPanelHandle, ctx->statusControl, "Capacity test failed");
    }
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
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
        
        int response = ConfirmPopup("Battery Not Fully Charged", message);
        if (!response) {
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
    
    // Set target voltage
    params.setVoltage.voltage = targetVoltage;
    result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_VOLTAGE,
                                    &params, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage: %s", PSB_GetErrorString(result));
        fclose(ctx->csvFile);
        return result;
    }
    
    // Set target current
    if (phase == CAPACITY_PHASE_DISCHARGE) {
        params.setSinkCurrent.current = targetCurrent;
        result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                        PSB_CMD_SET_SINK_CURRENT,
                                        &params, PSB_PRIORITY_HIGH, &cmdResult,
                                        PSB_QUEUE_COMMAND_TIMEOUT_MS);
    } else {
        params.setCurrent.current = targetCurrent;
        result = PSB_QueueCommandBlocking(PSB_GetGlobalQueueManager(), 
                                        PSB_CMD_SET_CURRENT,
                                        &params, PSB_PRIORITY_HIGH, &cmdResult,
                                        PSB_QUEUE_COMMAND_TIMEOUT_MS);
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
    Delay(2.0);
    
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
    
    return SUCCESS;
}

static int LogDataPoint(CapacityTestContext *ctx, CapacityDataPoint *point) {
    // Write to CSV
    fprintf(ctx->csvFile, "%.3f,%.3f,%.3f,%.3f\n", 
            point->time, point->voltage, point->current, point->power);
    fflush(ctx->csvFile);
    
    // Calculate capacity increment using trapezoidal rule
    if (ctx->dataPointCount > 0) {
        double deltaTime = point->time - ctx->lastTime;
        double capacityIncrement = CalculateCapacityIncrement(
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

static double CalculateCapacityIncrement(double current1, double current2, double deltaTime) {
    // Trapezoidal rule: average current * time
    // Convert from A*s to mAh: A * s * 1000 / 3600 = A * s / 3.6
    double averageCurrent = (current1 + current2) / 2.0;
    return averageCurrent * deltaTime * 1000.0 / 3600.0;
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
    // Re-enable UI controls on main panel
    SetCtrlAttribute(ctx->mainPanelHandle, PANEL_BTN_TEST_PSB, ATTR_DIMMED, 0);
    SetCtrlAttribute(ctx->mainPanelHandle, PANEL_TOGGLE_REMOTE_MODE, ATTR_DIMMED, 0);
    SetCtrlAttribute(ctx->mainPanelHandle, PANEL_BTN_TEST_BIOLOGIC, ATTR_DIMMED, 0);
    
    // Re-enable the button on the tab panel
    if (ctx->tabPanelHandle > 0 && ctx->buttonControl > 0) {
        SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_DIMMED, 0);
    }
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
    if (g_testContext.state != CAPACITY_STATE_IDLE) {
        CapacityTest_Abort();
    }
}

int CapacityTest_Abort(void) {
    if (g_testContext.state == CAPACITY_STATE_DISCHARGING || 
        g_testContext.state == CAPACITY_STATE_CHARGING) {
        g_testContext.state = CAPACITY_STATE_CANCELLED;
        
        // Wait for thread to complete
        if (g_testThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_testThreadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        }
    }
    return SUCCESS;
}