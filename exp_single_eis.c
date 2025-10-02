/******************************************************************************
 * exp_single_eis.c
 * 
 * Single EIS Measurement Module Implementation
 * Performs OCV + GEIS measurement at current battery state
 ******************************************************************************/

#include "common.h"
#include "exp_single_eis.h"
#include "BatteryTester.h"
#include "logging.h"
#include "status.h"
#include <ansi_c.h>
#include <analysis.h>
#include <utility.h>
#include <time.h>

/******************************************************************************
 * Module Variables
 ******************************************************************************/

static SingleEISContext g_context = {0};
static CmtThreadFunctionID g_threadId = 0;

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/

static int MeasurementThread(void *functionData);
static int VerifyDevicesAndInitialize(SingleEISContext *ctx);
static int CreateDataDirectory(SingleEISContext *ctx);
static int SaveSettings(SingleEISContext *ctx);
static int SwitchToBioLogic(SingleEISContext *ctx);
static int SafeDisconnectAllDevices(SingleEISContext *ctx);
static int ReadAllTemperatures(SingleEISContext *ctx, SingleEISTemperatureData *tempData);
static int PerformEISMeasurement(SingleEISContext *ctx);
static int RunOCVMeasurement(SingleEISContext *ctx);
static int RunGEISMeasurement(SingleEISContext *ctx);
static int ProcessGEISData(SingleEISContext *ctx);
static int SaveMeasurementData(SingleEISContext *ctx);
static void UpdateNyquistPlot(SingleEISContext *ctx);
static int CheckCancellation(SingleEISContext *ctx);
static void CleanupMeasurement(SingleEISContext *ctx);

/******************************************************************************
 * Public Functions
 ******************************************************************************/

int CVICALLBACK StartSingleEISCallback(int panel, int control, int event,
                                      void *callbackData, int eventData1, 
                                      int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;
    }
    
    // Check if measurement is already running
    if (SingleEIS_IsRunning()) {
        LogMessage("User requested to stop EIS measurement");
        g_context.cancelRequested = 1;
        g_context.state = SINGLE_EIS_STATE_CANCELLED;
        return 0;
    }
    
    // Check if system is busy
    CmtGetLock(g_busyLock);
    if (g_systemBusy) {
        CmtReleaseLock(g_busyLock);
        MessagePopup("System Busy", 
                     "Another operation is in progress.\n"
                     "Please wait for it to complete before starting the EIS measurement.");
        return 0;
    }
    g_systemBusy = 1;
    CmtReleaseLock(g_busyLock);
    
    // Initialize context
    memset(&g_context, 0, sizeof(g_context));
    g_context.cancelRequested = 0;
    g_context.emergencyStop = 0;
    g_context.state = SINGLE_EIS_STATE_PREPARING;
    g_context.mainPanelHandle = g_mainPanelHandle;
    g_context.tabPanelHandle = panel;
    g_context.buttonControl = control;
    g_context.statusControl = STR_SINGLE_EIS_STATUS; // Tab panel status control
    g_context.graphBiologicHandle = PANEL_GRAPH_BIOLOGIC;
    
    // Verify devices
    int result = VerifyDevicesAndInitialize(&g_context);
    if (result != SUCCESS) {
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        g_context.state = SINGLE_EIS_STATE_ERROR;
        return 0;
    }
    
    // Change button text to "Stop"
    SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Stop");
    
    // Start measurement thread
    int error = CmtScheduleThreadPoolFunction(g_threadPool, MeasurementThread, 
                                            &g_context, &g_threadId);
    if (error != 0) {
        g_context.state = SINGLE_EIS_STATE_ERROR;
        SetCtrlAttribute(panel, control, ATTR_LABEL_TEXT, "Start EIS");
        
        CmtGetLock(g_busyLock);
        g_systemBusy = 0;
        CmtReleaseLock(g_busyLock);
        
        MessagePopup("Error", "Failed to start measurement thread.");
        return 0;
    }
    
    return 0;
}

int SingleEIS_IsRunning(void) {
    return !(g_context.state == SINGLE_EIS_STATE_IDLE ||
             g_context.state == SINGLE_EIS_STATE_COMPLETED ||
             g_context.state == SINGLE_EIS_STATE_ERROR ||
             g_context.state == SINGLE_EIS_STATE_CANCELLED);
}

int SingleEIS_Abort(void) {
    if (SingleEIS_IsRunning()) {
        LogMessage("Aborting EIS measurement...");
        g_context.cancelRequested = 1;
        g_context.state = SINGLE_EIS_STATE_CANCELLED;
        
        if (g_threadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_threadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_threadId = 0;
        }
    }
    return SUCCESS;
}

int SingleEIS_EmergencyStop(void) {
    if (SingleEIS_IsRunning()) {
        LogMessage("EMERGENCY STOP - Single EIS measurement");
        g_context.emergencyStop = 1;
        g_context.cancelRequested = 1;
        g_context.state = SINGLE_EIS_STATE_ERROR;
        
        SafeDisconnectAllDevices(&g_context);
        
        if (g_threadId != 0) {
            CmtWaitForThreadPoolFunctionCompletion(g_threadPool, g_threadId,
                                                 OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
            g_threadId = 0;
        }
    }
    return SUCCESS;
}

void SingleEIS_Cleanup(void) {
    if (SingleEIS_IsRunning()) {
        SingleEIS_Abort();
    }
}

/******************************************************************************
 * Main Measurement Thread
 ******************************************************************************/

static int MeasurementThread(void *functionData) {
    SingleEISContext *ctx = (SingleEISContext*)functionData;
    char message[LARGE_BUFFER_SIZE];
    int result = SUCCESS;
    
    LogMessage("=== Starting Single EIS Measurement ===");
    
    ctx->measurementStartTime = Timer();
    
    // Confirmation popup
    snprintf(message, sizeof(message),
        "SINGLE EIS MEASUREMENT\n"
        "=====================\n\n"
        "This will perform:\n"
        "1. Open Circuit Voltage (OCV) measurement\n"
        "2. Galvanostatic EIS (GEIS) measurement\n\n"
        "The battery will be switched to the BioLogic potentiostat.\n"
        "No charging or discharging will occur.\n\n"
        "ESTIMATED DURATION: 5-10 minutes\n\n"
        "Continue with measurement?");
    
    int response = ConfirmPopup("Confirm EIS Measurement", message);
    if (!response || CheckCancellation(ctx)) {
        LogMessage("EIS measurement cancelled by user");
        ctx->state = SINGLE_EIS_STATE_CANCELLED;
        goto cleanup;
    }
    
    // Create data directory
    result = CreateDataDirectory(ctx);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogError("Failed to create data directory");
        MessagePopup("Error", "Failed to create data directory.\nPlease check disk space and permissions.");
        ctx->state = SINGLE_EIS_STATE_ERROR;
        goto cleanup;
    }
    
    // Save settings
    SaveSettings(ctx);
    
    // Configure Nyquist plot
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, "Nyquist Plot");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_XNAME, "Z' (Ohms)");
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_YNAME, "-Z'' (Ohms)");
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphBiologicHandle, -1, VAL_IMMEDIATE_DRAW);
    
    // Initialize relay states (both OFF for safety)
    LogMessage("Initializing relay states...");
    result = TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogError("Failed to initialize PSB relay");
        ctx->state = SINGLE_EIS_STATE_ERROR;
        goto cleanup;
    }
    
    result = TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    if (result != SUCCESS || CheckCancellation(ctx)) {
        LogError("Failed to initialize BioLogic relay");
        ctx->state = SINGLE_EIS_STATE_ERROR;
        goto cleanup;
    }
    
    // Perform the measurement
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Performing EIS measurement...");
    result = PerformEISMeasurement(ctx);
    
    if (result != SUCCESS || CheckCancellation(ctx)) {
        if (!CheckCancellation(ctx)) {
            ctx->state = SINGLE_EIS_STATE_ERROR;
        }
        goto cleanup;
    }
    
    // Success
    ctx->state = SINGLE_EIS_STATE_COMPLETED;
    LogMessage("=== EIS MEASUREMENT COMPLETED SUCCESSFULLY ===");
    LogMessage("OCV: %.4f V", ctx->measurement.ocvVoltage);
    LogMessage("Impedance points: %d", ctx->measurement.numPoints);
    
cleanup:
    CleanupMeasurement(ctx);
    
    // Update final status
    const char *finalStatus;
    switch (ctx->state) {
        case SINGLE_EIS_STATE_COMPLETED:
            finalStatus = "EIS measurement completed successfully";
            break;
        case SINGLE_EIS_STATE_CANCELLED:
            finalStatus = "EIS measurement cancelled by user";
            break;
        case SINGLE_EIS_STATE_ERROR:
            finalStatus = ctx->emergencyStop ? "EIS measurement emergency stopped" : 
                                               "EIS measurement failed";
            break;
        default:
            finalStatus = "EIS measurement ended unexpectedly";
            break;
    }
    
    SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, finalStatus);
    
    // Restore button text
    SetCtrlAttribute(ctx->tabPanelHandle, ctx->buttonControl, ATTR_LABEL_TEXT, "Start EIS");
    
    // Clear busy flag
    CmtGetLock(g_busyLock);
    g_systemBusy = 0;
    CmtReleaseLock(g_busyLock);
    
    g_threadId = 0;
    return 0;
}

/******************************************************************************
 * Setup and Verification Functions
 ******************************************************************************/

static int VerifyDevicesAndInitialize(SingleEISContext *ctx) {
    // Check BioLogic connection (REQUIRED)
    BioQueueManager *bioQueueMgr = BIO_GetGlobalQueueManager();
    if (!bioQueueMgr) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before starting the measurement.");
        return ERR_NOT_CONNECTED;
    }
    
    ctx->biologicID = BIO_QueueGetDeviceID(bioQueueMgr);
    if (ctx->biologicID < 0) {
        MessagePopup("BioLogic Not Connected", 
                     "The BioLogic potentiostat is not connected.\n"
                     "Please ensure it is connected before starting the measurement.");
        return ERR_NOT_CONNECTED;
    }
    
    // Check Teensy connection (REQUIRED for relay control)
    TNYQueueManager *tnyQueueMgr = TNY_GetGlobalQueueManager();
    if (!tnyQueueMgr) {
        MessagePopup("Teensy Not Connected", 
                     "The Teensy relay controller is not connected.\n"
                     "Please ensure it is connected before starting the measurement.");
        return ERR_NOT_CONNECTED;
    }
    
    // DTB and CDAQ are optional - just log if unavailable
    if (!ENABLE_DTB) {
        LogMessage("DTB temperature control disabled");
    }
    if (!ENABLE_CDAQ) {
        LogMessage("CDAQ thermocouple readings disabled");
    }
    
    LogMessage("All required devices verified successfully");
    return SUCCESS;
}

static int CreateDataDirectory(SingleEISContext *ctx) {
    char basePath[MAX_PATH_LENGTH];
    char dataPath[MAX_PATH_LENGTH];
    
    // Get executable directory
    if (GetExecutableDirectory(basePath, sizeof(basePath)) != SUCCESS) {
        strcpy(basePath, ".");
    }
    
    // Create main data directory
    snprintf(dataPath, sizeof(dataPath), "%s%s%s", 
             basePath, PATH_SEPARATOR, SINGLE_EIS_DATA_DIR);
    
    if (CreateDirectoryPath(dataPath) != SUCCESS) {
        LogError("Failed to create data directory: %s", dataPath);
        return ERR_BASE_FILE;
    }
    
    // Create timestamped directory
    int result = CreateTimestampedDirectory(dataPath, "eis", 
                                          ctx->dataDirectory, sizeof(ctx->dataDirectory));
    if (result != SUCCESS) {
        LogError("Failed to create timestamped directory");
        return result;
    }
    
    LogMessage("Created data directory: %s", ctx->dataDirectory);
    return SUCCESS;
}

static int SaveSettings(SingleEISContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->dataDirectory, PATH_SEPARATOR, SINGLE_EIS_SETTINGS_FILE);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create settings file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    fprintf(file, "# Single EIS Measurement Settings\n");
    fprintf(file, "# Created: %s\n", timeStr);
    fprintf(file, "# Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Measurement_Configuration");
    WriteINIDouble(file, "OCV_Duration_s", OCV_DURATION_S, 1);
    WriteINIDouble(file, "OCV_Sample_Interval_s", OCV_SAMPLE_INTERVAL_S, 1);
    WriteINIDouble(file, "GEIS_Initial_Freq_Hz", GEIS_INITIAL_FREQ, 0);
    WriteINIDouble(file, "GEIS_Final_Freq_Hz", GEIS_FINAL_FREQ, 1);
    WriteINIValue(file, "GEIS_Freq_Points", "%d", GEIS_FREQ_NUMBER);
    WriteINIDouble(file, "GEIS_Amplitude_A", GEIS_AMPLITUDE_I, 3);
    WriteINIValue(file, "GEIS_Average_N", "%d", GEIS_AVERAGE_N);
    fprintf(file, "\n");
    
    WriteINISection(file, "Device_Configuration");
    WriteINIValue(file, "BioLogic_Device_ID", "%d", ctx->biologicID);
    WriteINIValue(file, "ENABLE_DTB", "%d", ENABLE_DTB);
    WriteINIValue(file, "ENABLE_CDAQ", "%d", ENABLE_CDAQ);
    fprintf(file, "\n");
    
    fclose(file);
    
    LogMessage("Settings saved to: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Device Control Functions
 ******************************************************************************/

static int SwitchToBioLogic(SingleEISContext *ctx) {
    int result;
    
    LogMessage("Switching to BioLogic...");
    
    // Disconnect PSB relay first (safety)
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

static int SafeDisconnectAllDevices(SingleEISContext *ctx) {
    LogMessage("Safely disconnecting all devices...");
    
    // Stop BioLogic channel
    BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
    
    // Disconnect all relays
    TNY_SetPinQueued(TNY_PSB_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    TNY_SetPinQueued(TNY_BIOLOGIC_PIN, TNY_STATE_DISCONNECTED, DEVICE_PRIORITY_NORMAL);
    
    LogMessage("Device disconnect completed");
    return SUCCESS;
}

/******************************************************************************
 * Temperature Reading Function
 ******************************************************************************/

static int ReadAllTemperatures(SingleEISContext *ctx, SingleEISTemperatureData *tempData) {
    tempData->timestamp = Timer() - ctx->measurementStartTime;
    tempData->dtbDeviceCount = 0;
    tempData->dtbAverageTemperature = 0.0;
    
    for (int i = 0; i < DTB_NUM_DEVICES; i++) {
        tempData->dtbTemperatures[i] = 0.0;
    }
    
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
                     "DTB Avg: %.1f°C (%d devices)", tempData->dtbAverageTemperature, numDevices);
        } else {
            strcpy(tempData->status, "DTB: Error");
        }
    } else {
        strcpy(tempData->status, "DTB: Disabled");
    }
    
    if (ENABLE_CDAQ) {
        CDAQ_ReadTC(2, 0, &tempData->tc0Temperature);
        CDAQ_ReadTC(2, 1, &tempData->tc1Temperature);
    } else {
        tempData->tc0Temperature = 0.0;
        tempData->tc1Temperature = 0.0;
    }
    
    return SUCCESS;
}

/******************************************************************************
 * EIS Measurement Functions
 ******************************************************************************/

static int PerformEISMeasurement(SingleEISContext *ctx) {
    int result;
    int retryCount = 0;
    
    // Read temperatures before measurement
    ReadAllTemperatures(ctx, &ctx->measurement.tempData);
    
    // Log current conditions
    LogMessage("Starting EIS measurement:");
    LogMessage("  Temperature: %s", ctx->measurement.tempData.status);
    if (ENABLE_CDAQ) {
        LogMessage("  TC0: %.1f°C, TC1: %.1f°C", 
                  ctx->measurement.tempData.tc0Temperature,
                  ctx->measurement.tempData.tc1Temperature);
    }
    
    while (retryCount <= SINGLE_EIS_MAX_RETRY) {
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        // Switch to BioLogic
        result = SwitchToBioLogic(ctx);
        if (result != SUCCESS) {
            LogError("Failed to switch to BioLogic");
            return result;
        }
        
        // Wait for settling after relay switch
        if (retryCount > 0) {
            LogMessage("Retry %d after %.1f second delay", retryCount, SINGLE_EIS_RETRY_DELAY);
            Delay(SINGLE_EIS_RETRY_DELAY);
        } else {
            LogMessage("Waiting for battery settling...");
            Delay(2.0);
        }
        
        // Run OCV measurement
        ctx->state = SINGLE_EIS_STATE_MEASURING_OCV;
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Measuring OCV...");
        
        result = RunOCVMeasurement(ctx);
        if (result != SUCCESS) {
            if (retryCount < SINGLE_EIS_MAX_RETRY) {
                LogWarning("OCV measurement failed (attempt %d), retrying...", retryCount + 1);
                retryCount++;
                continue;
            } else {
                LogError("OCV measurement failed after %d retries", SINGLE_EIS_MAX_RETRY + 1);
                return result;
            }
        }
        
        if (CheckCancellation(ctx)) {
            return ERR_CANCELLED;
        }
        
        // Run GEIS measurement
        ctx->state = SINGLE_EIS_STATE_MEASURING_GEIS;
        SetCtrlVal(ctx->tabPanelHandle, ctx->statusControl, "Measuring GEIS...");
        
        result = RunGEISMeasurement(ctx);
        if (result != SUCCESS) {
            if (retryCount < SINGLE_EIS_MAX_RETRY) {
                LogWarning("GEIS measurement failed (attempt %d), retrying...", retryCount + 1);
                retryCount++;
                continue;
            } else {
                LogError("GEIS measurement failed after %d retries", SINGLE_EIS_MAX_RETRY + 1);
                return result;
            }
        }
        
        // Process GEIS data
        result = ProcessGEISData(ctx);
        if (result != SUCCESS) {
            LogWarning("Failed to process GEIS data");
            // Continue anyway - we have raw data
        }
        
        // Save measurement data
        result = SaveMeasurementData(ctx);
        if (result != SUCCESS) {
            LogWarning("Failed to save measurement data");
        }
        
        // Update Nyquist plot
        UpdateNyquistPlot(ctx);
        
        // Success
        ctx->measurement.retryCount = retryCount;
        if (retryCount > 0) {
            LogMessage("EIS measurement succeeded after %d retries", retryCount);
        }
        return SUCCESS;
    }
    
    return ERR_OPERATION_FAILED;
}

static int RunOCVMeasurement(SingleEISContext *ctx) {
    LogMessage("Starting OCV measurement...");
    
    int result = BIO_RunOCVQueued(ctx->biologicID, 0,
                                OCV_DURATION_S,
                                OCV_SAMPLE_INTERVAL_S,
                                OCV_RECORD_EVERY_DE,
                                OCV_RECORD_EVERY_DT,
                                OCV_E_RANGE,
                                true,
                                &ctx->measurement.ocvData,
                                OCV_TIMEOUT_MS,
                                DEVICE_PRIORITY_NORMAL,
                                NULL, NULL, &ctx->cancelRequested);
    
    if (result != SUCCESS) {
        LogError("OCV measurement failed: %s", BIO_GetErrorString(result));
        BIO_StopChannelQueued(ctx->biologicID, 0, DEVICE_PRIORITY_NORMAL);
        Delay(0.5);
        return result;
    }
    
    // Extract final voltage
    if (ctx->measurement.ocvData && ctx->measurement.ocvData->convertedData) {
        BIO_ConvertedData *convData = ctx->measurement.ocvData->convertedData;
        
        if (convData->numPoints > 0 && convData->numVariables >= 2 && convData->data[1] != NULL) {
            int lastPoint = convData->numPoints - 1;
            ctx->measurement.ocvVoltage = convData->data[1][lastPoint];
            LogMessage("OCV measurement complete: %.4f V", ctx->measurement.ocvVoltage);
        } else {
            LogWarning("OCV data incomplete");
            ctx->measurement.ocvVoltage = 0.0;
        }
    } else {
        LogWarning("No OCV data received");
        ctx->measurement.ocvVoltage = 0.0;
    }
    
    return SUCCESS;
}

static int RunGEISMeasurement(SingleEISContext *ctx) {
    LogMessage("Starting GEIS measurement...");
    
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
                                 &ctx->measurement.geisData,
                                 GEIS_TIMEOUT_MS,
                                 DEVICE_PRIORITY_NORMAL,
                                 NULL, NULL, &ctx->cancelRequested);
    
    if (result != SUCCESS) {
        LogError("GEIS measurement failed: %s", BIO_GetErrorString(result));
        return result;
    }
    
    LogMessage("GEIS measurement complete");
    return SUCCESS;
}

static int ProcessGEISData(SingleEISContext *ctx) {
    if (!ctx->measurement.geisData || !ctx->measurement.geisData->convertedData) {
        LogWarning("No GEIS data available for processing");
        return ERR_INVALID_PARAMETER;
    }
    
    BIO_ConvertedData *convData = ctx->measurement.geisData->convertedData;
    int processIndex = -1;
    
    if (ctx->measurement.geisData->rawData) {
        processIndex = ctx->measurement.geisData->rawData->processIndex;
    }
    
    LogMessage("Processing GEIS data: %d points, %d variables (process %d)", 
             convData->numPoints, convData->numVariables, processIndex);
    
    // Process impedance data (process 1 with 11+ variables)
    if (processIndex == 1 && convData->numVariables >= 11) {
        // Allocate impedance arrays
        ctx->measurement.frequencies = (double*)calloc(convData->numPoints, sizeof(double));
        ctx->measurement.zReal = (double*)calloc(convData->numPoints, sizeof(double));
        ctx->measurement.zImag = (double*)calloc(convData->numPoints, sizeof(double));
        
        if (!ctx->measurement.frequencies || !ctx->measurement.zReal || !ctx->measurement.zImag) {
            LogError("Failed to allocate impedance arrays");
            if (ctx->measurement.frequencies) free(ctx->measurement.frequencies);
            if (ctx->measurement.zReal) free(ctx->measurement.zReal);
            if (ctx->measurement.zImag) free(ctx->measurement.zImag);
            ctx->measurement.frequencies = NULL;
            ctx->measurement.zReal = NULL;
            ctx->measurement.zImag = NULL;
            return ERR_OUT_OF_MEMORY;
        }
        
        // Extract impedance data
        for (int i = 0; i < convData->numPoints; i++) {
            ctx->measurement.frequencies[i] = convData->data[0][i];  // Frequency
            ctx->measurement.zReal[i] = convData->data[4][i];       // Re(Zwe)
            ctx->measurement.zImag[i] = convData->data[5][i];       // Im(Zwe)
        }
        
        ctx->measurement.numPoints = convData->numPoints;
        
        LogMessage("Successfully extracted %d impedance points", ctx->measurement.numPoints);
        
    } else {
        LogWarning("Unexpected GEIS data format: process %d with %d variables", 
                  processIndex, convData->numVariables);
        return ERR_OPERATION_FAILED;
    }
    
    return SUCCESS;
}

static int SaveMeasurementData(SingleEISContext *ctx) {
    char filename[MAX_PATH_LENGTH];
    FILE *file;
    
    snprintf(filename, sizeof(filename), "%s%s%s", 
             ctx->dataDirectory, PATH_SEPARATOR, SINGLE_EIS_RESULTS_FILE);
    
    strcpy(ctx->measurement.filename, filename);
    
    file = fopen(filename, "w");
    if (!file) {
        LogError("Failed to create results file: %s", filename);
        return ERR_BASE_FILE;
    }
    
    time_t now = time(NULL);
    char timeStr[64];
    FormatTimestamp(now, timeStr, sizeof(timeStr));
    
    // Write measurement header
    fprintf(file, "# Single EIS Measurement Results\n");
    fprintf(file, "# ===============================\n");
    fprintf(file, "# Timestamp: %s\n", timeStr);
    fprintf(file, "# Battery Tester v%s\n\n", PROJECT_VERSION);
    
    WriteINISection(file, "Measurement_Information");
    WriteINIValue(file, "Timestamp", "%s", timeStr);
    WriteINIDouble(file, "OCV_Voltage_V", ctx->measurement.ocvVoltage, 4);
    WriteINIValue(file, "Retry_Count", "%d", ctx->measurement.retryCount);
    WriteINIValue(file, "Impedance_Points", "%d", ctx->measurement.numPoints);
    fprintf(file, "\n");
    
    WriteINISection(file, "Temperature_Data");
    WriteINIDouble(file, "DTB_Average_Temperature_C", 
                  ctx->measurement.tempData.dtbAverageTemperature, 1);
    WriteINIValue(file, "DTB_Device_Count", "%d", ctx->measurement.tempData.dtbDeviceCount);
    for (int i = 0; i < ctx->measurement.tempData.dtbDeviceCount; i++) {
        char key[64];
        snprintf(key, sizeof(key), "DTB_%d_Temperature_C", i + 1);
        WriteINIDouble(file, key, ctx->measurement.tempData.dtbTemperatures[i], 1);
    }
    WriteINIDouble(file, "TC0_Temperature_C", ctx->measurement.tempData.tc0Temperature, 1);
    WriteINIDouble(file, "TC1_Temperature_C", ctx->measurement.tempData.tc1Temperature, 1);
    fprintf(file, "\n");
    
    // Write impedance data
    WriteINISection(file, "Impedance_Data");
    if (ctx->measurement.numPoints > 0) {
        fprintf(file, "# Frequency_Hz,Z_Real_Ohm,Z_Imag_Ohm,Z_Mag_Ohm,Phase_Deg\n");
        
        for (int i = 0; i < ctx->measurement.numPoints; i++) {
            double magnitude = sqrt(ctx->measurement.zReal[i] * ctx->measurement.zReal[i] + 
                                  ctx->measurement.zImag[i] * ctx->measurement.zImag[i]);
            double phase = atan2(ctx->measurement.zImag[i], ctx->measurement.zReal[i]) * 180.0 / M_PI;
            
            fprintf(file, "%.3e,%.6e,%.6e,%.6e,%.3e\n",
                    ctx->measurement.frequencies[i],
                    ctx->measurement.zReal[i],
                    ctx->measurement.zImag[i],
                    magnitude,
                    phase);
        }
    } else {
        fprintf(file, "# No impedance data available\n");
    }
    
    fclose(file);
    
    LogMessage("Measurement data saved to: %s", filename);
    return SUCCESS;
}

/******************************************************************************
 * Graph Functions
 ******************************************************************************/

static void UpdateNyquistPlot(SingleEISContext *ctx) {
    if (ctx->measurement.numPoints == 0) return;
    
    // Clear previous plot
    DeleteGraphPlot(ctx->mainPanelHandle, ctx->graphBiologicHandle, -1, VAL_DELAYED_DRAW);
    
    // Create negative imaginary array
    double *negZImag = (double*)calloc(ctx->measurement.numPoints, sizeof(double));
    if (!negZImag) return;
    
    for (int i = 0; i < ctx->measurement.numPoints; i++) {
        negZImag[i] = -ctx->measurement.zImag[i];
    }
    
    // Plot Nyquist data
    PlotXY(ctx->mainPanelHandle, ctx->graphBiologicHandle,
           ctx->measurement.zReal, negZImag, ctx->measurement.numPoints,
           VAL_DOUBLE, VAL_DOUBLE, VAL_SCATTER,
           VAL_SOLID_CIRCLE, VAL_SOLID, 1, VAL_GREEN);
    
    // Update title
    char title[MEDIUM_BUFFER_SIZE];
    snprintf(title, sizeof(title), "Nyquist Plot - OCV: %.4f V", ctx->measurement.ocvVoltage);
    SetCtrlAttribute(ctx->mainPanelHandle, ctx->graphBiologicHandle, ATTR_LABEL_TEXT, title);
    
    free(negZImag);
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

static int CheckCancellation(SingleEISContext *ctx) {
    return (ctx->cancelRequested || ctx->emergencyStop || 
            ctx->state == SINGLE_EIS_STATE_CANCELLED ||
            ctx->state == SINGLE_EIS_STATE_ERROR);
}

static void CleanupMeasurement(SingleEISContext *ctx) {
    LogMessage("Cleaning up measurement...");
    
    // Safely disconnect devices
    SafeDisconnectAllDevices(ctx);
    
    // Free allocated memory
    if (ctx->measurement.ocvData) {
        BIO_FreeTechniqueData(ctx->measurement.ocvData);
        ctx->measurement.ocvData = NULL;
    }
    if (ctx->measurement.geisData) {
        BIO_FreeTechniqueData(ctx->measurement.geisData);
        ctx->measurement.geisData = NULL;
    }
    if (ctx->measurement.frequencies) {
        free(ctx->measurement.frequencies);
        ctx->measurement.frequencies = NULL;
    }
    if (ctx->measurement.zReal) {
        free(ctx->measurement.zReal);
        ctx->measurement.zReal = NULL;
    }
    if (ctx->measurement.zImag) {
        free(ctx->measurement.zImag);
        ctx->measurement.zImag = NULL;
    }
    
    LogMessage("Cleanup completed");
}