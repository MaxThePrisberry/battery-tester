/******************************************************************************
 * exp_ocv.c
 *
 * OCV (Open Circuit Voltage) Experiment Module Implementation
 ******************************************************************************/

#include "common.h"
#include "exp_ocv.h"
#include "BatteryTester.h"
#include "psb10000_queue.h"
#include "logging.h"
#include "status.h"
#include <ansi_c.h>
#include <utility.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static OCVExperimentContext g_ocvContext = {0};
static CmtThreadFunctionID g_ocvThreadId = 0;

// TODO: Replace these placeholder constants with actual UIR constants
// once the OCV tab is added in the LabWindows/CVI UIR editor.
// The constants below must match the control names defined in BatteryTester.uir.
#ifndef OCV_BATTERYNAME
#define OCV_BATTERYNAME       0
#define OCV_NUM_TEMPERATURE   0
#define OCV_NUM_REST_TIME     0
#define OCV_NUM_INTERVAL      0
#define OCV_BTN_START         0
#define OCV_STR_STATUS        0
#define OCV_NUM_OUTPUT        0
#endif

// Control arrays for dimming during experiment
static const int g_ocvControls[] = {
    OCV_BATTERYNAME,
    OCV_NUM_TEMPERATURE,
    OCV_NUM_REST_TIME,
    OCV_NUM_INTERVAL,
    OCV_BTN_START,
    OCV_STR_STATUS,
    OCV_NUM_OUTPUT
};
static const int g_numOcvControls = sizeof(g_ocvControls) / sizeof(g_ocvControls[0]);

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int OCVExperimentThread(void *functionData);
static int CheckCancellation(OCVExperimentContext *ctx);
static int SwitchToBioLogic(void);
static int DisconnectBioLogic(void);
static int SetupTemperatureControl(OCVExperimentContext *ctx);
static int WaitForTargetTemperature(OCVExperimentContext *ctx);
static int StabilizeTemperature(OCVExperimentContext *ctx);
static int ReadAllTemperatures(TemperatureDataPoint *tempData, double timestamp);
static int RunRestPeriod(OCVExperimentContext *ctx);
static int RunOCVMeasurement(OCVExperimentContext *ctx, OCVMeasurementResult *result, volatile int *cancelFlag);
static int CreateOCVFileSystem(OCVExperimentContext *ctx);
static int SaveExperimentSettings(OCVExperimentContext *ctx);
static int SaveOCVResults(OCVExperimentContext *ctx);
static void CleanupExperiment(OCVExperimentContext *ctx);
static void RestoreUI(OCVExperimentContext *ctx);

/******************************************************************************
 * Public Functions
 ******************************************************************************/

int CVICALLBACK StartOCVExperimentCallback(int panel, int control, int event,
                                           void *callbackData, int eventData1,
                                           int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }

    // Check if OCV experiment is already running - this is a Stop request
    if (OCVExperiment_IsRunning()) {
        LogMessage("User requested to stop OCV experiment");
        g_ocvContext.cancelRequested = 1;
        g_ocvContext.state = OCV_STATE_CANCELLED;
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

    // Verify Bio-Logic is available
    if (!BIO_IsAbstractInitialized()) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        MessagePopup("Bio-Logic Not Connected",
                     "The Bio-Logic potentiostat is not connected.\n"
                     "Please ensure it is connected before running OCV.");
        return 0;
    }

    // Initialize experiment context
    memset(&g_ocvContext, 0, sizeof(g_ocvContext));
    g_ocvContext.state = OCV_STATE_PREPARING;
    g_ocvContext.mainPanelHandle = g_mainPanelHandle;
    g_ocvContext.tabPanelHandle = panel;
    g_ocvContext.buttonControl = control;
    g_ocvContext.statusControl = OCV_STR_STATUS;
    g_ocvContext.outputControl = OCV_NUM_OUTPUT;

    // Read experiment parameters from UI
    GetCtrlVal(panel, OCV_BATTERYNAME, g_ocvContext.params.batteryName);
    GetCtrlVal(panel, OCV_NUM_TEMPERATURE, &g_ocvContext.params.targetTemperature);
    GetCtrlVal(panel, OCV_NUM_REST_TIME, &g_ocvContext.params.restTime);
    g_ocvContext.params.restTime *= 60.0;  // UI is in minutes, internal is seconds
    {
        double tempLogInterval;
        GetCtrlVal(panel, OCV_NUM_INTERVAL, &tempLogInterval);
        g_ocvContext.params.logInterval = (unsigned int)tempLogInterval;
    }
    g_ocvContext.params.tempTolerance = OCV_TEMP_TOLERANCE;

    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");

    // Dim controls (all except the active button)
    int controlsToDim[g_numOcvControls - 1];
    int dimCount = 0;
    for (int i = 0; i < g_numOcvControls; i++) {
        if (g_ocvControls[i] != control) {
            controlsToDim[dimCount++] = g_ocvControls[i];
        }
    }
    DimExperimentControls(g_mainPanelHandle, panel, 1, controlsToDim, dimCount);

    // Start experiment thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, OCVExperimentThread,
                                              &g_ocvContext, &g_ocvThreadId);
    if (error != 0) {
        g_ocvContext.state = OCV_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start");
        DimExperimentControls(g_mainPanelHandle, panel, 0,
                              (int *)g_ocvControls, g_numOcvControls);

        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);

        MessagePopup("Error", "Failed to start OCV experiment thread.");
        return 0;
    }

    return 0;
}

int OCVExperiment_IsRunning(void) {
    return !(g_ocvContext.state == OCV_STATE_IDLE ||
             g_ocvContext.state == OCV_STATE_COMPLETED ||
             g_ocvContext.state == OCV_STATE_ERROR ||
             g_ocvContext.state == OCV_STATE_CANCELLED);
}

int OCVExperiment_Abort(void) {
    if (OCVExperiment_IsRunning()) {
        g_ocvContext.cancelRequested = 1;
        g_ocvContext.state = OCV_STATE_CANCELLED;

        if (g_ocvThreadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_ocvThreadId,
                                                   OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_ocvThreadId = 0;
        }
    }
    return SUCCESS;
}

void OCVExperiment_Cleanup(void) {
    if (OCVExperiment_IsRunning()) {
        OCVExperiment_Abort();
    }
}

void OCV_FreeResult(OCVMeasurementResult *result) {
    if (result && result->rawOCVData) {
        BIO_FreeTechniqueData(result->rawOCVData);
        result->rawOCVData = NULL;
    }
}

/******************************************************************************
 * Public API for Baseline Integration
 ******************************************************************************/

int OCV_RunExperimentInDir(const OCVExperimentParams *params,
                           const char *experimentDir,
                           const char *phaseSubDir,
                           const char *phaseName,
                           int statusControl,
                           int tabPanelHandle,
                           volatile int *cancelFlag,
                           OCVMeasurementResult *result) {
    int err = SUCCESS;

    if (!params || !experimentDir || !phaseSubDir || !phaseName || !result) {
        return ERR_NULL_POINTER;
    }

    memset(result, 0, sizeof(OCVMeasurementResult));

    LogMessage("=== OCV Experiment (%s) ===", phaseName);

    // Create phase subdirectory
    char phaseDir[MAX_PATH_LENGTH];
    snprintf(phaseDir, sizeof(phaseDir), "%s%s%s", experimentDir, PATH_SEPARATOR, phaseSubDir);
    err = CreateDirectoryPath(phaseDir);
    if (err != SUCCESS) {
        LogError("Failed to create %s directory: %s", phaseName, phaseDir);
        return err;
    }

    // Switch to Bio-Logic
    if (statusControl && tabPanelHandle) {
        char statusMsg[MEDIUM_BUFFER_SIZE];
        snprintf(statusMsg, sizeof(statusMsg), "%s: Switching to Bio-Logic...", phaseName);
        SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
    }
    err = SwitchToBioLogic();
    if (err != SUCCESS) {
        LogError("%s: Failed to switch to Bio-Logic", phaseName);
        return err;
    }

    // Temperature control (if enabled)
    if (ENABLE_DTB) {
        // Set up a temporary context for temperature functions
        OCVExperimentContext tempCtx = {0};
        tempCtx.params = *params;
        tempCtx.cancelRequested = 0;
        tempCtx.experimentStartTime = Timer();
        if (statusControl && tabPanelHandle) {
            tempCtx.tabPanelHandle = tabPanelHandle;
            tempCtx.statusControl = statusControl;
        }

        if (statusControl && tabPanelHandle) {
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), "%s: Setting up temperature control...", phaseName);
            SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
        }
        err = SetupTemperatureControl(&tempCtx);
        if (err != SUCCESS) return err;

        if (cancelFlag && *cancelFlag) return ERR_CANCELLED;

        if (statusControl && tabPanelHandle) {
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), "%s: Waiting for target temperature...", phaseName);
            SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
        }
        err = WaitForTargetTemperature(&tempCtx);
        if (err != SUCCESS) return err;

        if (cancelFlag && *cancelFlag) return ERR_CANCELLED;

        if (statusControl && tabPanelHandle) {
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), "%s: Stabilizing temperature...", phaseName);
            SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
        }
        err = StabilizeTemperature(&tempCtx);
        if (err != SUCCESS) return err;
    }

    if (cancelFlag && *cancelFlag) return ERR_CANCELLED;

    // Rest period
    if (params->restTime > 0) {
        LogMessage("%s: Resting for %.0f seconds...", phaseName, params->restTime);
        if (statusControl && tabPanelHandle) {
            char statusMsg[MEDIUM_BUFFER_SIZE];
            snprintf(statusMsg, sizeof(statusMsg), "%s: Resting (%.0f s)...", phaseName, params->restTime);
            SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
        }

        // Open rest temperature log
        char restLogPath[MAX_PATH_LENGTH];
        snprintf(restLogPath, sizeof(restLogPath), "%s%s%s", phaseDir, PATH_SEPARATOR, OCV_REST_TEMP_FILE);
        FILE *restLog = fopen(restLogPath, "w");
        if (restLog) {
            fprintf(restLog, "%s\n", OCV_REST_TEMP_HEADER);
        }

        double restStart = Timer();
        double lastLog = restStart;
        while ((Timer() - restStart) < params->restTime) {
            if (cancelFlag && *cancelFlag) {
                if (restLog) fclose(restLog);
                return ERR_CANCELLED;
            }

            double now = Timer();
            if ((now - lastLog) >= params->logInterval) {
                TemperatureDataPoint tempData;
                ReadAllTemperatures(&tempData, now - restStart);

                if (restLog) {
                    fprintf(restLog, "%.1f,%.2f", tempData.timestamp, tempData.dtbAverageTemperature);
                    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                        fprintf(restLog, ",%.2f", tempData.dtbTemperatures[i]);
                    }
                    fprintf(restLog, ",%.2f,%.2f\n", tempData.tc0Temperature, tempData.tc1Temperature);
                    fflush(restLog);
                }
                lastLog = now;
            }

            ProcessSystemEvents();
            Delay(1.0);
        }

        if (restLog) fclose(restLog);
        LogMessage("%s: Rest period completed", phaseName);
    }

    if (cancelFlag && *cancelFlag) return ERR_CANCELLED;

    // Run OCV measurement
    if (statusControl && tabPanelHandle) {
        char statusMsg[MEDIUM_BUFFER_SIZE];
        snprintf(statusMsg, sizeof(statusMsg), "%s: Measuring OCV...", phaseName);
        SetCtrlVal(tabPanelHandle, statusControl, statusMsg);
    }

    // Use a mutable cancel flag if the caller's is NULL
    volatile int localCancel = 0;
    volatile int *useCancel = cancelFlag ? cancelFlag : &localCancel;

    err = RunOCVMeasurement(NULL, result, useCancel);
    if (err != SUCCESS) {
        LogError("%s: OCV measurement failed", phaseName);
        return err;
    }

    // Save results to phase directory
    char resultsPath[MAX_PATH_LENGTH];
    snprintf(resultsPath, sizeof(resultsPath), "%s%s%s", phaseDir, PATH_SEPARATOR, OCV_RESULTS_FILE);
    FILE *resultsFile = fopen(resultsPath, "w");
    if (resultsFile) {
        WriteINISection(resultsFile, "OCV_Results");
        WriteINIDouble(resultsFile, "Final_OCV_V", result->finalOCV_V, 4);
        WriteINIDouble(resultsFile, "Average_OCV_V", result->averageOCV_V, 4);
        WriteINIDouble(resultsFile, "Min_OCV_V", result->minOCV_V, 4);
        WriteINIDouble(resultsFile, "Max_OCV_V", result->maxOCV_V, 4);
        WriteINIDouble(resultsFile, "Measurement_Duration_s", result->measurementDuration_s, 1);
        WriteINIValue(resultsFile, "Num_Data_Points", "%d", result->numDataPoints);
        WriteINIDouble(resultsFile, "Temperature_At_Measurement_C", result->tempAtMeasurement, 1);
        fclose(resultsFile);
    }

    LogMessage("%s: OCV = %.4f V (avg: %.4f V)", phaseName, result->finalOCV_V, result->averageOCV_V);
    return SUCCESS;
}

int OCV_RunExperiment(const OCVExperimentParams *params,
                      const char *experimentDir,
                      int statusControl,
                      int tabPanelHandle,
                      volatile int *cancelFlag,
                      OCVMeasurementResult *result) {
    return OCV_RunExperimentInDir(params, experimentDir, "phase_0", "Phase 0",
                                  statusControl, tabPanelHandle, cancelFlag, result);
}

int OCV_QuickMeasurement(volatile int *cancelFlag, OCVMeasurementResult *result) {
    if (!result) {
        return ERR_NULL_POINTER;
    }

    memset(result, 0, sizeof(OCVMeasurementResult));

    LogMessage("Running quick OCV measurement...");

    // Switch to Bio-Logic
    int err = SwitchToBioLogic();
    if (err != SUCCESS) {
        LogError("Quick OCV: Failed to switch to Bio-Logic");
        return err;
    }

    // Use a mutable cancel flag if the caller's is NULL
    volatile int localCancel = 0;
    volatile int *useCancel = cancelFlag ? cancelFlag : &localCancel;

    // Run measurement
    err = RunOCVMeasurement(NULL, result, useCancel);

    // Disconnect Bio-Logic relay
    DisconnectBioLogic();

    if (err != SUCCESS) {
        LogError("Quick OCV: Measurement failed");
        return err;
    }

    LogMessage("Quick OCV: %.4f V", result->finalOCV_V);
    return SUCCESS;
}

/******************************************************************************
 * Experiment Thread
 ******************************************************************************/

static int OCVExperimentThread(void *functionData) {
    OCVExperimentContext *ctx = (OCVExperimentContext *)functionData;
    int result = SUCCESS;

    // Initialize COM for this thread (required for EC-Lab OLE COM mode)
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE && hrCom != S_FALSE) {
        LogError("Failed to initialize COM for OCV experiment thread (HRESULT: 0x%08X)", hrCom);
    }

    LogMessage("=== Starting OCV Experiment ===");
    ctx->experimentStartTime = Timer();

    if (CheckCancellation(ctx)) {
        LogMessage("OCV experiment cancelled before start");
        goto cleanup;
    }

    // Create file system
    ctx->state = OCV_STATE_PREPARING;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Preparing experiment...");
    result = CreateOCVFileSystem(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
        goto cleanup;
    }

    // Open experiment log and redirect logging
    {
        char logPath[MAX_PATH_LENGTH];
        snprintf(logPath, sizeof(logPath), "%s%s%s",
                 ctx->experimentDirectory, PATH_SEPARATOR, OCV_LOG_FILE);
        ctx->experimentLogFile = fopen(logPath, "w");
        if (ctx->experimentLogFile) {
            SetExternalLogFile(ctx->experimentLogFile);
        }
    }

    SaveExperimentSettings(ctx);

    // Switch to Bio-Logic
    ctx->state = OCV_STATE_SWITCHING_RELAY;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Switching to Bio-Logic...");

    // Disconnect all relays first
    TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    Delay(TNY_SWITCH_DELAY_MS / 1000.0);

    result = SwitchToBioLogic();
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
        goto cleanup;
    }

    // Temperature control (if enabled)
    if (ENABLE_DTB) {
        ctx->state = OCV_STATE_TEMP_WAIT;
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Setting up temperature control...");
        result = SetupTemperatureControl(ctx);
        if (result != SUCCESS || CheckCancellation(ctx)) {
            if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
            goto cleanup;
        }

        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Waiting for target temperature...");
        result = WaitForTargetTemperature(ctx);
        if (result != SUCCESS || CheckCancellation(ctx)) {
            if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
            goto cleanup;
        }

        ctx->state = OCV_STATE_TEMP_STABILIZE;
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Stabilizing temperature...");
        result = StabilizeTemperature(ctx);
        if (result != SUCCESS || CheckCancellation(ctx)) {
            if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
            goto cleanup;
        }
    }

    // Rest period
    if (ctx->params.restTime > 0) {
        ctx->state = OCV_STATE_RESTING;
        result = RunRestPeriod(ctx);
        if (result != SUCCESS || CheckCancellation(ctx)) {
            if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
            goto cleanup;
        }
    }

    // OCV measurement
    ctx->state = OCV_STATE_MEASURING;
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Measuring OCV...");
    result = RunOCVMeasurement(ctx, &ctx->result, &ctx->cancelRequested);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) ctx->state = OCV_STATE_ERROR;
        goto cleanup;
    }

    // Display result
    SetCtrlVal(ctx->tabPanelHandle, ctx->outputControl, ctx->result.finalOCV_V);

    // Save results
    SaveOCVResults(ctx);

    ctx->state = OCV_STATE_COMPLETED;
    LogMessage("=== OCV Experiment Completed: %.4f V ===", ctx->result.finalOCV_V);

cleanup:
    CleanupExperiment(ctx);

    // Update final status
    const char *finalStatus;
    switch (ctx->state) {
        case OCV_STATE_COMPLETED:
            finalStatus = "OCV experiment completed successfully";
            break;
        case OCV_STATE_CANCELLED:
            finalStatus = "OCV experiment cancelled by user";
            break;
        case OCV_STATE_ERROR:
            finalStatus = "OCV experiment failed";
            break;
        default:
            finalStatus = "OCV experiment ended unexpectedly";
            break;
    }

    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, finalStatus);

    // Restore button and UI
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start");
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0,
                          (int *)g_ocvControls, g_numOcvControls);

    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);

    g_ocvThreadId = 0;
    CoUninitialize();

    return 0;
}

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static int CheckCancellation(OCVExperimentContext *ctx) {
    return (ctx->cancelRequested ||
            ctx->state == OCV_STATE_CANCELLED ||
            ctx->state == OCV_STATE_ERROR);
}

static int SwitchToBioLogic(void) {
    int result;

    LogMessage("Switching to BioLogic...");

    // Safety: Disable PSB output first
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    Delay(0.5);

    // Disconnect PSB relay first
    result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect PSB relay: %s", GetErrorString(result));
        return result;
    }

    Delay(TNY_SWITCH_DELAY_MS / 1000.0);

    // Connect BioLogic relay
    result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_CONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to connect BioLogic relay: %s", GetErrorString(result));
        return result;
    }

    Delay(TNY_SWITCH_DELAY_MS / 1000.0);

    LogMessage("Successfully switched to BioLogic");
    return SUCCESS;
}

static int DisconnectBioLogic(void) {
    LogMessage("Disconnecting BioLogic relay...");

    int result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS) {
        LogError("Failed to disconnect BioLogic relay: %s", GetErrorString(result));
    }

    Delay(TNY_SWITCH_DELAY_MS / 1000.0);
    return result;
}

static int SetupTemperatureControl(OCVExperimentContext *ctx) {
    int result;

    LogMessage("Setting up temperature control: target %.1f C", ctx->params.targetTemperature);

    // Set DTB target temperature
    result = DTB_SetSetPointAllQueued(ctx->params.targetTemperature, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to set DTB temperature: %s", DTB_GetErrorString(result));
        return result;
    }

    // Start DTB controller
    result = DTB_SetRunStopAllQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != DTB_SUCCESS) {
        LogError("Failed to start DTB: %s", DTB_GetErrorString(result));
        return result;
    }

    return SUCCESS;
}

static int WaitForTargetTemperature(OCVExperimentContext *ctx) {
    if (!ENABLE_DTB) {
        LogMessage("Temperature control disabled - skipping temperature wait");
        return SUCCESS;
    }

    double startTime = Timer();
    double lastCheckTime = startTime;

    LogMessage("Waiting for ALL DTB devices to reach target temperature: %.1f C",
               ctx->params.targetTemperature);

    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }

        double currentTime = Timer();

        if ((currentTime - lastCheckTime) >= OCV_TEMP_CHECK_INTERVAL) {
            DTB_Status dtbStatuses[MAX_DTB_DEVICES];
            int numDevices = 0;
            int result = DTB_GetStatusAllQueued(dtbStatuses, &numDevices, DEVICE_PRIORITY_NORMAL);

            if (result == DTB_SUCCESS) {
                int devicesInTolerance = 0;
                double tempSum = 0.0;
                double maxTempDiff = 0.0;

                for (int i = 0; i < numDevices; i++) {
                    double tempDiff = fabs(dtbStatuses[i].processValue - ctx->params.targetTemperature);
                    tempSum += dtbStatuses[i].processValue;

                    if (tempDiff > maxTempDiff) {
                        maxTempDiff = tempDiff;
                    }

                    if (tempDiff <= ctx->params.tempTolerance) {
                        devicesInTolerance++;
                    }
                }

                double avgTemp = tempSum / numDevices;

                LogMessage("DTB avg: %.1f C (target: %.1f C, max diff: %.1f C, %d/%d in tolerance)",
                           avgTemp, ctx->params.targetTemperature, maxTempDiff,
                           devicesInTolerance, numDevices);

                if (devicesInTolerance == numDevices) {
                    LogMessage("ALL DTB devices reached target temperature (avg: %.1f C)", avgTemp);
                    ctx->dtbReady = 1;
                    ctx->temperatureStabilizationStart = currentTime;
                    return SUCCESS;
                }

                // Update status display
                if (ctx->tabPanelHandle && ctx->statusControl) {
                    char statusMsg[MEDIUM_BUFFER_SIZE];
                    snprintf(statusMsg, sizeof(statusMsg),
                             "Waiting for temperature: %.1f/%.1f C (%d/%d ready)",
                             avgTemp, ctx->params.targetTemperature,
                             devicesInTolerance, numDevices);
                    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
                }
            } else {
                LogError("Failed to read DTB status: %s", DTB_GetErrorString(result));
                return result;
            }

            lastCheckTime = currentTime;
        }

        // Timeout check
        if ((currentTime - startTime) > OCV_TEMP_TIMEOUT_SEC) {
            LogError("Temperature wait timeout - not all DTB devices reached target");
            return ERR_TIMEOUT;
        }

        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int StabilizeTemperature(OCVExperimentContext *ctx) {
    if (!ENABLE_DTB) {
        LogMessage("Temperature control disabled - skipping stabilization");
        return SUCCESS;
    }

    double startTime = ctx->temperatureStabilizationStart;
    double lastCheckTime = startTime;

    LogMessage("Stabilizing temperature for %.0f seconds...", (double)OCV_TEMP_STABILIZE_TIME);

    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }

        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;

        if (elapsedTime >= OCV_TEMP_STABILIZE_TIME) {
            LogMessage("Temperature stabilization completed");
            ctx->temperatureStable = 1;
            return SUCCESS;
        }

        if ((currentTime - lastCheckTime) >= OCV_TEMP_CHECK_INTERVAL) {
            DTB_Status dtbStatuses[MAX_DTB_DEVICES];
            int numDevices = 0;
            int result = DTB_GetStatusAllQueued(dtbStatuses, &numDevices, DEVICE_PRIORITY_NORMAL);

            if (result == DTB_SUCCESS) {
                int devicesInTolerance = 0;
                double tempSum = 0.0;

                for (int i = 0; i < numDevices; i++) {
                    double tempDiff = fabs(dtbStatuses[i].processValue - ctx->params.targetTemperature);
                    tempSum += dtbStatuses[i].processValue;

                    if (tempDiff <= ctx->params.tempTolerance) {
                        devicesInTolerance++;
                    }
                }

                double avgTemp = tempSum / numDevices;

                // If ANY device drifted out of tolerance, restart stabilization
                if (devicesInTolerance < numDevices) {
                    LogWarning("Temperature drift detected during stabilization (avg: %.1f C, %d/%d in tolerance)",
                               avgTemp, devicesInTolerance, numDevices);
                    startTime = currentTime;
                    ctx->temperatureStabilizationStart = currentTime;
                    LogMessage("Restarting temperature stabilization due to drift");
                }

                // Update status display
                if (ctx->tabPanelHandle && ctx->statusControl) {
                    double remainingTime = OCV_TEMP_STABILIZE_TIME - elapsedTime;
                    char statusMsg[MEDIUM_BUFFER_SIZE];
                    snprintf(statusMsg, sizeof(statusMsg),
                             "Stabilizing: %.1f C (%.0f sec remaining)",
                             avgTemp, remainingTime > 0 ? remainingTime : 0);
                    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);
                }
            } else {
                LogError("Failed to read DTB status during stabilization: %s",
                         DTB_GetErrorString(result));
                return result;
            }

            lastCheckTime = currentTime;
        }

        ProcessSystemEvents();
        Delay(1.0);
    }
}

static int ReadAllTemperatures(TemperatureDataPoint *tempData, double timestamp) {
    tempData->timestamp = timestamp;
    tempData->dtbDeviceCount = 0;
    tempData->dtbAverageTemperature = 0.0;

    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        tempData->dtbTemperatures[i] = 0.0;
    }

    // Read DTB temperatures
    if (ENABLE_DTB) {
        DTB_Status dtbStatuses[MAX_DTB_DEVICES];
        int numDevices = 0;

        if (DTB_GetStatusAllQueued(dtbStatuses, &numDevices, DEVICE_PRIORITY_NORMAL) == DTB_SUCCESS) {
            double tempSum = 0.0;
            tempData->dtbDeviceCount = numDevices;

            for (int i = 0; i < numDevices && i < DTB_NUM_DEVICES; i++) {
                tempData->dtbTemperatures[i] = dtbStatuses[i].processValue;
                tempSum += dtbStatuses[i].processValue;
            }

            tempData->dtbAverageTemperature = tempSum / numDevices;
            snprintf(tempData->status, sizeof(tempData->status),
                     "DTB Avg: %.1f C (%d devices)", tempData->dtbAverageTemperature, numDevices);
        } else {
            strcpy(tempData->status, "DTB: Error reading devices");
        }
    } else {
        strcpy(tempData->status, "DTB: Disabled");
    }

    // Read thermocouple temperatures
    if (ENABLE_CDAQ) {
        if (CDAQ_ReadTC(2, 0, &tempData->tc0Temperature) != SUCCESS) {
            tempData->tc0Temperature = 0.0;
        }
        if (CDAQ_ReadTC(2, 1, &tempData->tc1Temperature) != SUCCESS) {
            tempData->tc1Temperature = 0.0;
        }
    } else {
        tempData->tc0Temperature = 0.0;
        tempData->tc1Temperature = 0.0;
    }

    return SUCCESS;
}

static int RunRestPeriod(OCVExperimentContext *ctx) {
    LogMessage("Resting for %.0f seconds (logging every %d s)...",
               ctx->params.restTime, ctx->params.logInterval);

    // Update status
    char statusMsg[MEDIUM_BUFFER_SIZE];
    snprintf(statusMsg, sizeof(statusMsg), "Resting (%.0f s)...", ctx->params.restTime);
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);

    // Open rest temperature log
    char restLogPath[MAX_PATH_LENGTH];
    snprintf(restLogPath, sizeof(restLogPath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OCV_REST_TEMP_FILE);
    ctx->restTempLogFile = fopen(restLogPath, "w");
    if (ctx->restTempLogFile) {
        fprintf(ctx->restTempLogFile, "%s\n", OCV_REST_TEMP_HEADER);
    }

    ctx->restStartTime = Timer();
    ctx->lastLogTime = ctx->restStartTime;

    while (1) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }

        double now = Timer();
        double elapsed = now - ctx->restStartTime;

        if (elapsed >= ctx->params.restTime) {
            break;
        }

        // Log temperature at intervals
        if ((now - ctx->lastLogTime) >= ctx->params.logInterval) {
            TemperatureDataPoint tempData;
            ReadAllTemperatures(&tempData, elapsed);

            if (ctx->restTempLogFile) {
                fprintf(ctx->restTempLogFile, "%.1f,%.2f", tempData.timestamp, tempData.dtbAverageTemperature);
                for (int i = 0; i < DTB_NUM_DEVICES; i++) {
                    fprintf(ctx->restTempLogFile, ",%.2f", tempData.dtbTemperatures[i]);
                }
                fprintf(ctx->restTempLogFile, ",%.2f,%.2f\n",
                        tempData.tc0Temperature, tempData.tc1Temperature);
                fflush(ctx->restTempLogFile);
            }

            // Update status with remaining time
            double remaining = ctx->params.restTime - elapsed;
            snprintf(statusMsg, sizeof(statusMsg),
                     "Resting: %.0f sec remaining (%.1f C)",
                     remaining, tempData.dtbAverageTemperature);
            SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, statusMsg);

            ctx->lastLogTime = now;
        }

        ProcessSystemEvents();
        Delay(1.0);
    }

    // Close rest log
    if (ctx->restTempLogFile) {
        fclose(ctx->restTempLogFile);
        ctx->restTempLogFile = NULL;
    }

    LogMessage("Rest period completed");
    return SUCCESS;
}

static int RunOCVMeasurement(OCVExperimentContext *ctx, OCVMeasurementResult *result, volatile int *cancelFlag) {
    LogMessage("Starting OCV measurement...");

    result->rawOCVData = NULL;

    int err = BIO_Abstract_RunOCV(0,          // channel
                                  OCV_DURATION_S,
                                  OCV_SAMPLE_INTERVAL_S,
                                  OCV_RECORD_EVERY_DE,
                                  OCV_RECORD_EVERY_DT,
                                  OCV_E_RANGE,
                                  &result->rawOCVData,
                                  OCV_EXP_TIMEOUT_MS,
                                  NULL, NULL, cancelFlag,
                                  ECLAB_OCV_EXPERIMENT_TEMPLATE);

    if (err != SUCCESS) {
        LogError("OCV measurement failed: %s", BIO_GetErrorString(err));
        return err;
    }

    // Extract voltage statistics from OCV data
    if (result->rawOCVData && result->rawOCVData->convertedData) {
        BIO_ConvertedData *convData = result->rawOCVData->convertedData;

        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            result->numDataPoints = convData->numPoints;

            // data[1] is Ewe (voltage) column
            double sum = 0.0;
            double minV = convData->data[1][0];
            double maxV = convData->data[1][0];

            for (int i = 0; i < convData->numPoints; i++) {
                double v = convData->data[1][i];
                sum += v;
                if (v < minV) minV = v;
                if (v > maxV) maxV = v;
            }

            int lastPoint = convData->numPoints - 1;
            result->finalOCV_V = convData->data[1][lastPoint];
            result->averageOCV_V = sum / convData->numPoints;
            result->minOCV_V = minV;
            result->maxOCV_V = maxV;

            // Extract duration from time column (data[0])
            if (convData->data[0] != NULL) {
                result->measurementDuration_s = convData->data[0][lastPoint];
            }

            LogMessage("OCV measurement: final=%.4f V, avg=%.4f V, min=%.4f V, max=%.4f V (%d points)",
                       result->finalOCV_V, result->averageOCV_V,
                       result->minOCV_V, result->maxOCV_V, result->numDataPoints);
        } else {
            LogWarning("OCV data incomplete - using 0.0 V");
        }
    } else {
        LogWarning("No OCV data received from BioLogic");
    }

    // Record temperature at measurement time
    TemperatureDataPoint tempData;
    ReadAllTemperatures(&tempData, 0.0);
    result->tempAtMeasurement = tempData.dtbAverageTemperature;

    return SUCCESS;
}

/******************************************************************************
 * File System and Results
 ******************************************************************************/

static int CreateOCVFileSystem(OCVExperimentContext *ctx) {
    int result = CreateTimestampedDirectoryWithBattery(
        OCV_DATA_DIR,
        ctx->params.batteryName,
        "ocv",
        ctx->experimentDirectory,
        sizeof(ctx->experimentDirectory));

    if (result != SUCCESS) {
        LogError("Failed to create OCV experiment directory");
        return result;
    }

    LogMessage("OCV experiment directory: %s", ctx->experimentDirectory);
    return SUCCESS;
}

static int SaveExperimentSettings(OCVExperimentContext *ctx) {
    char settingsPath[MAX_PATH_LENGTH];
    snprintf(settingsPath, sizeof(settingsPath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OCV_SETTINGS_FILE);

    FILE *file = fopen(settingsPath, "w");
    if (!file) {
        LogError("Failed to create settings file: %s", settingsPath);
        return ERR_BASE_FILE;
    }

    WriteINISection(file, "Experiment_Settings");
    WriteINIValue(file, "Experiment_Type", "OCV");
    WriteINIValue(file, "Battery_Name", "%s", ctx->params.batteryName);
    WriteINIDouble(file, "Target_Temperature_C", ctx->params.targetTemperature, 1);
    WriteINIDouble(file, "Temperature_Tolerance_C", ctx->params.tempTolerance, 1);
    WriteINIDouble(file, "Rest_Time_s", ctx->params.restTime, 0);
    WriteINIValue(file, "Temperature_Control_Enabled", "%d", ENABLE_DTB);
    WriteINIValue(file, "Log_Interval_s", "%u", ctx->params.logInterval);

    time_t now = time(NULL);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    WriteINIValue(file, "Start_Time", "%s", timeStr);

    fclose(file);
    return SUCCESS;
}

static int SaveOCVResults(OCVExperimentContext *ctx) {
    char resultsPath[MAX_PATH_LENGTH];
    snprintf(resultsPath, sizeof(resultsPath), "%s%s%s",
             ctx->experimentDirectory, PATH_SEPARATOR, OCV_RESULTS_FILE);

    FILE *file = fopen(resultsPath, "w");
    if (!file) {
        LogError("Failed to create results file: %s", resultsPath);
        return ERR_BASE_FILE;
    }

    WriteINISection(file, "OCV_Results");
    WriteINIDouble(file, "Final_OCV_V", ctx->result.finalOCV_V, 4);
    WriteINIDouble(file, "Average_OCV_V", ctx->result.averageOCV_V, 4);
    WriteINIDouble(file, "Min_OCV_V", ctx->result.minOCV_V, 4);
    WriteINIDouble(file, "Max_OCV_V", ctx->result.maxOCV_V, 4);
    WriteINIDouble(file, "Measurement_Duration_s", ctx->result.measurementDuration_s, 1);
    WriteINIValue(file, "Num_Data_Points", "%d", ctx->result.numDataPoints);
    WriteINIDouble(file, "Temperature_At_Measurement_C", ctx->result.tempAtMeasurement, 1);

    WriteINISection(file, "Experiment_Summary");
    ctx->experimentEndTime = Timer();
    double totalDuration = ctx->experimentEndTime - ctx->experimentStartTime;
    WriteINIDouble(file, "Total_Duration_s", totalDuration, 1);

    const char *completionStatus;
    switch (ctx->state) {
        case OCV_STATE_COMPLETED: completionStatus = "Completed"; break;
        case OCV_STATE_CANCELLED: completionStatus = "Cancelled"; break;
        case OCV_STATE_ERROR:     completionStatus = "Error"; break;
        default:                  completionStatus = "Unknown"; break;
    }
    WriteINIValue(file, "Completion_Status", "%s", completionStatus);

    fclose(file);
    LogMessage("OCV results saved to: %s", resultsPath);
    return SUCCESS;
}

/******************************************************************************
 * Cleanup
 ******************************************************************************/

static void CleanupExperiment(OCVExperimentContext *ctx) {
    LogMessage("Cleaning up OCV experiment...");

    // Disconnect Bio-Logic relay
    DisconnectBioLogic();

    // Stop DTB if we started it
    if (ENABLE_DTB) {
        DTB_SetRunStopAllQueued(0, DEVICE_PRIORITY_NORMAL);
    }

    // Close rest temperature log
    if (ctx->restTempLogFile) {
        fclose(ctx->restTempLogFile);
        ctx->restTempLogFile = NULL;
    }

    // Close experiment log
    ClearExternalLogFile();
    if (ctx->experimentLogFile) {
        fclose(ctx->experimentLogFile);
        ctx->experimentLogFile = NULL;
    }

    // Free OCV data
    OCV_FreeResult(&ctx->result);
}

static void RestoreUI(OCVExperimentContext *ctx) {
    DimExperimentControls(ctx->mainPanelHandle, ctx->tabPanelHandle, 0,
                          (int *)g_ocvControls, g_numOcvControls);
}
