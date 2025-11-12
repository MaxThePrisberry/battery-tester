/******************************************************************************
 * biologic_eclab.c
 *
 * Implementation of EC-Lab technique interface
 *
 * This file implements the high-level technique API using EC-Lab OLE COM
 * automation. It handles the workflow of loading settings, running experiments,
 * monitoring status, and converting data to the standard format.
 ******************************************************************************/

#include "biologic_eclab.h"
#include "logging.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>

/******************************************************************************
 * Module State
 ******************************************************************************/

static ECLAB_Config g_config = {0};
static bool g_initialized = false;

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

/**
 * Build full path to .mps template file
 */
static void BuildMpsPath(const char *templateName, char *fullPath, size_t maxLen) {
    snprintf(fullPath, maxLen, "%s\\%s", g_config.settingsDir, templateName);
}

/**
 * Build full path to .mpr output file
 */
static void BuildMprPath(const char *mprFilename, char *fullPath, size_t maxLen) {
    snprintf(fullPath, maxLen, "%s\\%s", g_config.dataDir, mprFilename);
}

/**
 * Monitor measurement until complete or timeout
 *
 * Polls MeasureStatus every 500ms and calls progress callback.
 * Returns when status changes to STOP or timeout occurs.
 */
static int MonitorMeasurement(int timeout_ms,
                             BioTechniqueProgressCallback progressCallback,
                             void *userData,
                             volatile int *cancelled,
                             ECLAB_Status *finalStatus) {
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    double startTime = Timer();
    double lastCallbackTime = startTime;
    int pollCount = 0;
    int measurementStarted = 0;  // Track if measurement has actually started running

    LogMessageEx(LOG_DEVICE_BIO, "Monitoring measurement (timeout: %d ms)", timeout_ms);

    while (1) {
        // Check cancellation
        if (cancelled && *cancelled) {
            LogMessageEx(LOG_DEVICE_BIO, "Measurement cancelled by user");
            ECLAB_StopChannel(g_config.conn, g_config.deviceNumber, g_config.channelNumber);
            return BIO_ERR_TECHNIQUE_CANCELLED;
        }

        // Check timeout
        double elapsed = (Timer() - startTime) * 1000.0;
        if (elapsed > timeout_ms) {
            LogErrorEx(LOG_DEVICE_BIO, "Measurement timeout after %.1f seconds", elapsed / 1000.0);
            ECLAB_StopChannel(g_config.conn, g_config.deviceNumber, g_config.channelNumber);
            return BIO_ERR_TECHNIQUE_TIMEOUT;
        }

        // Get status
        ECLAB_Status status;
        int result = ECLAB_MeasureStatus(g_config.conn, g_config.deviceNumber,
                                        g_config.channelNumber, &status);

        if (result != SUCCESS) {
            LogWarningEx(LOG_DEVICE_BIO, "Failed to get status: %s", ECLAB_GetErrorString(result));
            Delay(0.5);
            continue;
        }

        // Log status periodically
        if (pollCount % 10 == 0) {  // Every 5 seconds
            LogMessageEx(LOG_DEVICE_BIO,
                        "Status: %d, Technique: %d, Time: %.1f s, Points: %d",
                        status.status, status.techniqueCode, status.time,
                        status.totalPointIndex);
        }
        pollCount++;

        // Call progress callback (throttled to once per second)
        if (progressCallback && (Timer() - lastCallbackTime) >= 1.0) {
            progressCallback(status.time, status.totalPointIndex, userData);
            lastCallbackTime = Timer();
        }

        // Track measurement state transitions
        if (status.status == ECLAB_STATUS_RUN) {
            if (!measurementStarted) {
                measurementStarted = 1;
                LogMessageEx(LOG_DEVICE_BIO, "Measurement started running");
            }
        }

        // Check if measurement is complete
        // Only accept STOP after we've seen RUN to avoid detecting initial state
        if (status.status == ECLAB_STATUS_STOP && measurementStarted) {
            LogMessageEx(LOG_DEVICE_BIO, "Measurement completed (%.1f s, %d points)",
                        status.time, status.totalPointIndex);
            if (finalStatus) {
                *finalStatus = status;
            }
            return SUCCESS;
        }

        // Check for disconnection
        if (status.connection == ECLAB_CONN_DISCONNECTED) {
            LogErrorEx(LOG_DEVICE_BIO, "Device disconnected during measurement");
            return ECLAB_ERR_NOT_CONNECTED;
        }

        // Check for safety limits
        if (status.safetyLimit != ECLAB_SAFETY_OK) {
            LogWarningEx(LOG_DEVICE_BIO, "Safety limit triggered: %d", status.safetyLimit);
        }

        // Wait before next poll
        Delay(0.5);
    }

    return SUCCESS;
}

/******************************************************************************
 * Initialization and Shutdown
 ******************************************************************************/

int BIO_ECLAB_Init(const ECLAB_Config *config) {
    if (!config) return ERR_NULL_POINTER;
    if (g_initialized) return ERR_ALREADY_INITIALIZED;

    LogMessageEx(LOG_DEVICE_BIO, "Initializing EC-Lab backend");
    LogMessageEx(LOG_DEVICE_BIO, "  Settings dir: %s", config->settingsDir);
    LogMessageEx(LOG_DEVICE_BIO, "  Data dir: %s", config->dataDir);
    LogMessageEx(LOG_DEVICE_BIO, "  Device: %d, Channel: %d",
                config->deviceNumber, config->channelNumber);

    // Copy configuration
    memcpy(&g_config, config, sizeof(ECLAB_Config));

    // Verify directories exist
    if (GetFileAttributesA(g_config.settingsDir) == INVALID_FILE_ATTRIBUTES) {
        LogWarningEx(LOG_DEVICE_BIO, "Settings directory does not exist: %s", g_config.settingsDir);
        LogWarningEx(LOG_DEVICE_BIO, "Please create it and add .mps template files");
    }

    if (GetFileAttributesA(g_config.dataDir) == INVALID_FILE_ATTRIBUTES) {
        LogWarningEx(LOG_DEVICE_BIO, "Data directory does not exist: %s", g_config.dataDir);
        LogMessageEx(LOG_DEVICE_BIO, "Creating data directory...");
        if (!CreateDirectoryA(g_config.dataDir, NULL)) {
            LogErrorEx(LOG_DEVICE_BIO, "Failed to create data directory");
            return ERR_OPERATION_FAILED;
        }
    }

    // Initialize EC-Lab OLE COM connection
    int result = ECLAB_Initialize(&g_config.conn, g_config.dataDir);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize EC-Lab: %s",
                  ECLAB_GetErrorString(result));
        return result;
    }

    // Connect to device
    result = ECLAB_ConnectDevice(g_config.conn, g_config.deviceNumber);
    if (result != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to connect to device: %s",
                  ECLAB_GetErrorString(result));
        ECLAB_Shutdown(g_config.conn);
        g_config.conn = NULL;
        return result;
    }

    // Set deviceID for API compatibility (use deviceNumber as ID in EC-Lab mode)
    g_config.deviceID = g_config.deviceNumber;

    // Disable EC-Lab message windows for automated operation
    ECLAB_EnableMessagesWindows(g_config.conn, false);

    // DIAGNOSTIC: Quick connection stability check (5 seconds)
    // This runs synchronously during init to detect immediate connection issues
    // Set ECLAB_CONNECTION_DIAGNOSTIC_ENABLED to 0 in common.h to disable
#ifndef ECLAB_CONNECTION_DIAGNOSTIC_ENABLED
#define ECLAB_CONNECTION_DIAGNOSTIC_ENABLED 1
#endif

#if ECLAB_CONNECTION_DIAGNOSTIC_ENABLED
    LogMessageEx(LOG_DEVICE_BIO, "Running connection diagnostic (5 tests over ~5 seconds)...");

    int testCount = 0;
    int passCount = 0;
    int failCount = 0;
    double testStart = Timer();

    for (int i = 0; i < 5; i++) {
        if (i > 0) Delay(1.0);  // Wait 1 second between tests (not before first test)
        testCount++;

        int testResult = ECLAB_TestConnection(g_config.conn);
        double elapsed = Timer() - testStart;

        if (testResult == SUCCESS) {
            passCount++;
            LogMessageEx(LOG_DEVICE_BIO, "  [Test %d @ %.1fs] Connection: OK", testCount, elapsed);
        } else {
            failCount++;
            LogWarningEx(LOG_DEVICE_BIO, "  [Test %d @ %.1fs] Connection: FAILED (%s)",
                        testCount, elapsed, ECLAB_GetErrorString(testResult));
        }
    }

    LogMessageEx(LOG_DEVICE_BIO, "Connection diagnostic complete: %d/%d tests passed", passCount, testCount);

    if (failCount > 0) {
        LogWarningEx(LOG_DEVICE_BIO, "WARNING: %d connection test(s) failed - device may be unstable", failCount);
    }
#else
    LogMessageEx(LOG_DEVICE_BIO, "Connection diagnostic disabled (ECLAB_CONNECTION_DIAGNOSTIC_ENABLED=0)");
#endif

    g_initialized = true;

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab backend initialized successfully");
    return SUCCESS;
}

void BIO_ECLAB_Shutdown(void) {
    if (!g_initialized) return;

    LogMessageEx(LOG_DEVICE_BIO, "Shutting down EC-Lab backend");

    if (g_config.conn) {
        ECLAB_Shutdown(g_config.conn);
        g_config.conn = NULL;
    }

    memset(&g_config, 0, sizeof(g_config));
    g_initialized = false;

    LogMessageEx(LOG_DEVICE_BIO, "EC-Lab backend shutdown complete");
}

bool BIO_ECLAB_IsInitialized(void) {
    return g_initialized;
}

int BIO_ECLAB_GetConfig(ECLAB_Config *config) {
    if (!config) return ERR_NULL_POINTER;
    if (!g_initialized) return ERR_NOT_INITIALIZED;

    memcpy(config, &g_config, sizeof(ECLAB_Config));
    return SUCCESS;
}

/******************************************************************************
 * Technique Functions
 ******************************************************************************/

int BIO_ECLAB_RunOCV(const char *mpsFilePath,
                     const char *outputMprPath,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData,
                     volatile int *cancelled) {
    if (!result) return ERR_NULL_POINTER;
    if (!g_initialized) return ERR_NOT_INITIALIZED;

    LogMessageEx(LOG_DEVICE_BIO, "Starting OCV measurement via EC-Lab");

    // Build paths
    char mpsPath[MAX_PATH];
    char mprPath[MAX_PATH];

    if (mpsFilePath) {
        strncpy(mpsPath, mpsFilePath, MAX_PATH - 1);
    } else {
        BuildMpsPath(g_config.ocvTemplate, mpsPath, MAX_PATH);
    }

    if (outputMprPath) {
        strncpy(mprPath, outputMprPath, MAX_PATH - 1);
    } else {
        char filename[MAX_PATH];
        BIO_ECLAB_GenerateMprFilename(BIO_TECHNIQUE_OCV, filename);
        BuildMprPath(filename, mprPath, MAX_PATH);
    }

    LogMessageEx(LOG_DEVICE_BIO, "  Settings: %s", mpsPath);
    LogMessageEx(LOG_DEVICE_BIO, "  Output: %s", mprPath);

    // Load settings
    int ret = ECLAB_LoadSettings(g_config.conn, g_config.deviceNumber,
                                 g_config.channelNumber, mpsPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load settings: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Start measurement
    ret = ECLAB_RunChannel(g_config.conn, g_config.deviceNumber,
                          g_config.channelNumber, mprPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start measurement: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Monitor until complete
    ECLAB_Status finalStatus;
    ret = MonitorMeasurement(timeout_ms, progressCallback, userData, cancelled, &finalStatus);
    (void)finalStatus;  // Status retrieved but not currently used
    if (ret != SUCCESS) {
        return ret;
    }

    // Small delay to ensure file I/O completes after EC-Lab reports STOP
    Delay(1.0);

    // Convert .mpr data to BIO_TechniqueData
    ret = BIO_ECLAB_ConvertMprToTechniqueData(mprPath, BIO_TECHNIQUE_OCV, result);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert data: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    LogMessageEx(LOG_DEVICE_BIO, "OCV measurement completed successfully");
    return SUCCESS;
}

int BIO_ECLAB_RunPEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled) {
    if (!result) return ERR_NULL_POINTER;
    if (!g_initialized) return ERR_NOT_INITIALIZED;

    LogMessageEx(LOG_DEVICE_BIO, "Starting PEIS measurement via EC-Lab");

    // Build paths
    char mpsPath[MAX_PATH];
    char mprPath[MAX_PATH];

    if (mpsFilePath) {
        strncpy(mpsPath, mpsFilePath, MAX_PATH - 1);
    } else {
        BuildMpsPath(g_config.peisTemplate, mpsPath, MAX_PATH);
    }

    if (outputMprPath) {
        strncpy(mprPath, outputMprPath, MAX_PATH - 1);
    } else {
        char filename[MAX_PATH];
        BIO_ECLAB_GenerateMprFilename(BIO_TECHNIQUE_PEIS, filename);
        BuildMprPath(filename, mprPath, MAX_PATH);
    }

    LogMessageEx(LOG_DEVICE_BIO, "  Settings: %s", mpsPath);
    LogMessageEx(LOG_DEVICE_BIO, "  Output: %s", mprPath);

    // Load settings
    int ret = ECLAB_LoadSettings(g_config.conn, g_config.deviceNumber,
                                 g_config.channelNumber, mpsPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load settings: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Start measurement
    ret = ECLAB_RunChannel(g_config.conn, g_config.deviceNumber,
                          g_config.channelNumber, mprPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start measurement: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Monitor until complete
    ECLAB_Status finalStatus;
    ret = MonitorMeasurement(timeout_ms, progressCallback, userData, cancelled, &finalStatus);
    (void)finalStatus;  // Status retrieved but not currently used
    if (ret != SUCCESS) {
        return ret;
    }

    // Small delay to ensure file I/O completes after EC-Lab reports STOP
    Delay(1.0);

    // Convert .mpr data to BIO_TechniqueData
    ret = BIO_ECLAB_ConvertMprToTechniqueData(mprPath, BIO_TECHNIQUE_PEIS, result);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert data: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    LogMessageEx(LOG_DEVICE_BIO, "PEIS measurement completed successfully");
    return SUCCESS;
}

int BIO_ECLAB_RunGEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled) {
    if (!result) return ERR_NULL_POINTER;
    if (!g_initialized) return ERR_NOT_INITIALIZED;

    LogMessageEx(LOG_DEVICE_BIO, "Starting GEIS measurement via EC-Lab");

    // Build paths
    char mpsPath[MAX_PATH];
    char mprPath[MAX_PATH];

    if (mpsFilePath) {
        strncpy(mpsPath, mpsFilePath, MAX_PATH - 1);
    } else {
        BuildMpsPath(g_config.geisTemplate, mpsPath, MAX_PATH);
    }

    if (outputMprPath) {
        strncpy(mprPath, outputMprPath, MAX_PATH - 1);
    } else {
        char filename[MAX_PATH];
        BIO_ECLAB_GenerateMprFilename(BIO_TECHNIQUE_GEIS, filename);
        BuildMprPath(filename, mprPath, MAX_PATH);
    }

    LogMessageEx(LOG_DEVICE_BIO, "  Settings: %s", mpsPath);
    LogMessageEx(LOG_DEVICE_BIO, "  Output: %s", mprPath);

    // Load settings
    int ret = ECLAB_LoadSettings(g_config.conn, g_config.deviceNumber,
                                 g_config.channelNumber, mpsPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to load settings: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Start measurement
    ret = ECLAB_RunChannel(g_config.conn, g_config.deviceNumber,
                          g_config.channelNumber, mprPath);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to start measurement: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    // Monitor until complete
    ECLAB_Status finalStatus;
    ret = MonitorMeasurement(timeout_ms, progressCallback, userData, cancelled, &finalStatus);
    (void)finalStatus;  // Status retrieved but not currently used
    if (ret != SUCCESS) {
        return ret;
    }

    // Small delay to ensure file I/O completes after EC-Lab reports STOP
    Delay(1.0);

    // Convert .mpr data to BIO_TechniqueData
    ret = BIO_ECLAB_ConvertMprToTechniqueData(mprPath, BIO_TECHNIQUE_GEIS, result);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to convert data: %s", ECLAB_GetErrorString(ret));
        return ret;
    }

    LogMessageEx(LOG_DEVICE_BIO, "GEIS measurement completed successfully");
    return SUCCESS;
}

/******************************************************************************
 * Data Conversion Functions
 ******************************************************************************/

int BIO_ECLAB_ConvertMprToTechniqueData(const char *mprPath,
                                        BioTechniqueType type,
                                        BIO_TechniqueData **data) {
    if (!mprPath || !data) return ERR_NULL_POINTER;

    LogMessageEx(LOG_DEVICE_BIO, "Converting .mpr file to BIO_TechniqueData: %s", mprPath);

    // Check file exists
    if (GetFileAttributesA(mprPath) == INVALID_FILE_ATTRIBUTES) {
        LogErrorEx(LOG_DEVICE_BIO, ".mpr file not found: %s", mprPath);
        return ECLAB_ERR_FILE_NOT_FOUND;
    }

    // Get number of data points in file
    int numPoints = 0;
    int ret = ECLAB_MeasureNumberOfPoints(mprPath, &numPoints);
    if (ret != SUCCESS) {
        LogErrorEx(LOG_DEVICE_BIO, "Failed to read number of points: %s",
                  ECLAB_GetErrorString(ret));
        return ret;
    }

    if (numPoints <= 0) {
        LogWarningEx(LOG_DEVICE_BIO, "File contains no data points");
        numPoints = 0;
    }

    LogMessageEx(LOG_DEVICE_BIO, "Reading %d data points from .mpr file", numPoints);

    // Allocate main structure
    BIO_TechniqueData *techData = (BIO_TechniqueData*)calloc(1, sizeof(BIO_TechniqueData));
    if (!techData) return ERR_OUT_OF_MEMORY;

    // Allocate placeholder raw data (EC-Lab mode doesn't have raw device buffer)
    techData->rawData = (BIO_RawDataBuffer*)calloc(1, sizeof(BIO_RawDataBuffer));
    if (!techData->rawData) {
        free(techData);
        return ERR_OUT_OF_MEMORY;
    }
    techData->rawData->numPoints = numPoints;
    techData->rawData->numVariables = 0;  // No raw data in EC-Lab mode
    techData->rawData->rawData = NULL;
    techData->rawData->bufferSize = 0;

    // Allocate converted data structure
    techData->convertedData = (BIO_ConvertedData*)calloc(1, sizeof(BIO_ConvertedData));
    if (!techData->convertedData) {
        free(techData->rawData);
        free(techData);
        return ERR_OUT_OF_MEMORY;
    }

    // Determine number of variables based on technique type
    int numVars = 0;
    const char **varNames = NULL;
    const char **varUnits = NULL;

    if (type == BIO_TECHNIQUE_OCV) {
        // OCV: time, voltage, current
        numVars = 3;
        static const char *ocvNames[] = {"time", "Ewe", "I"};
        static const char *ocvUnits[] = {"s", "V", "mA"};
        varNames = ocvNames;
        varUnits = ocvUnits;
    } else if (type == BIO_TECHNIQUE_PEIS || type == BIO_TECHNIQUE_GEIS) {
        // EIS: time, frequency, Re(Z), -Im(Z)
        numVars = 4;
        static const char *eisNames[] = {"time", "freq", "Re(Z)", "-Im(Z)"};
        static const char *eisUnits[] = {"s", "Hz", "Ohm", "Ohm"};
        varNames = eisNames;
        varUnits = eisUnits;
    } else {
        LogErrorEx(LOG_DEVICE_BIO, "Unknown technique type: %d", type);
        BIO_FreeTechniqueData(techData);
        return ERR_INVALID_PARAMETER;
    }

    techData->convertedData->numPoints = numPoints;
    techData->convertedData->numVariables = numVars;

    // Allocate variable names and units
    techData->convertedData->variableNames = (char**)malloc(numVars * sizeof(char*));
    techData->convertedData->variableUnits = (char**)malloc(numVars * sizeof(char*));
    if (!techData->convertedData->variableNames || !techData->convertedData->variableUnits) {
        BIO_FreeTechniqueData(techData);
        return ERR_OUT_OF_MEMORY;
    }

    for (int i = 0; i < numVars; i++) {
        // Manually duplicate strings for C99 compatibility
        size_t nameLen = strlen(varNames[i]) + 1;
        techData->convertedData->variableNames[i] = (char*)malloc(nameLen);
        if (techData->convertedData->variableNames[i]) {
            strcpy(techData->convertedData->variableNames[i], varNames[i]);
        }

        size_t unitLen = strlen(varUnits[i]) + 1;
        techData->convertedData->variableUnits[i] = (char*)malloc(unitLen);
        if (techData->convertedData->variableUnits[i]) {
            strcpy(techData->convertedData->variableUnits[i], varUnits[i]);
        }
    }

    // Allocate 2D data array [numVars][numPoints]
    techData->convertedData->data = (double**)malloc(numVars * sizeof(double*));
    if (!techData->convertedData->data) {
        BIO_FreeTechniqueData(techData);
        return ERR_OUT_OF_MEMORY;
    }

    for (int i = 0; i < numVars; i++) {
        techData->convertedData->data[i] = (double*)malloc(numPoints * sizeof(double));
        if (!techData->convertedData->data[i]) {
            BIO_FreeTechniqueData(techData);
            return ERR_OUT_OF_MEMORY;
        }
    }

    // Read data points from .mpr file
    if (type == BIO_TECHNIQUE_OCV) {
        // Read OCV data (time, voltage, current)
        for (int i = 0; i < numPoints; i++) {
            double time, voltage, current;
            ret = ECLAB_MeasureDcValue(mprPath, i, &time, &voltage, &current);
            if (ret != SUCCESS) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to read DC value at index %d: %s",
                          i, ECLAB_GetErrorString(ret));
                BIO_FreeTechniqueData(techData);
                return ret;
            }

            techData->convertedData->data[0][i] = time;
            techData->convertedData->data[1][i] = voltage;
            techData->convertedData->data[2][i] = current;
        }
    } else {
        // Read EIS data (time, frequency, Re(Z), -Im(Z))
        for (int i = 0; i < numPoints; i++) {
            double time, freq, zReal, zImag;
            ret = ECLAB_MeasureEisValue(mprPath, i, &time, &freq, &zReal, &zImag);
            if (ret != SUCCESS) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to read EIS value at index %d: %s",
                          i, ECLAB_GetErrorString(ret));
                BIO_FreeTechniqueData(techData);
                return ret;
            }

            techData->convertedData->data[0][i] = time;
            techData->convertedData->data[1][i] = freq;
            techData->convertedData->data[2][i] = zReal;
            techData->convertedData->data[3][i] = zImag;
        }
    }

    LogMessageEx(LOG_DEVICE_BIO, "Successfully converted %d data points", numPoints);

    *data = techData;
    return SUCCESS;
}

int BIO_ECLAB_GenerateMprFilename(BioTechniqueType type, char *filename) {
    if (!filename) return ERR_NULL_POINTER;

    const char *prefix = "unknown";
    switch (type) {
        case BIO_TECHNIQUE_OCV: prefix = "ocv"; break;
        case BIO_TECHNIQUE_PEIS: prefix = "peis"; break;
        case BIO_TECHNIQUE_GEIS: prefix = "geis"; break;
        default: prefix = "technique"; break;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Include channel suffix (_C01, _C02, etc.) to match EC-Lab's file naming
    // EC-Lab automatically appends channel suffix, so we include it in our path
    snprintf(filename, MAX_PATH, "%s_%04d%02d%02d_%02d%02d%02d_C%02d.mpr",
            prefix,
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec,
            g_config.channelNumber + 1);  // 0-based channel -> 1-based suffix (C01, C02, ...)

    return SUCCESS;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

ECLabConnection* BIO_ECLAB_GetConnection(void) {
    return g_initialized ? g_config.conn : NULL;
}

int BIO_ECLAB_GetDeviceID(void) {
    return g_initialized ? g_config.deviceID : -1;
}

int BIO_ECLAB_Connect(void) {
    if (!g_initialized) return ERR_NOT_INITIALIZED;
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    return ECLAB_ConnectDevice(g_config.conn, g_config.deviceNumber);
}

int BIO_ECLAB_Disconnect(void) {
    if (!g_initialized) return ERR_NOT_INITIALIZED;
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    return ECLAB_DisconnectDevice(g_config.conn);
}

int BIO_ECLAB_ForceReconnect(void) {
    if (!g_initialized) return ERR_NOT_INITIALIZED;
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    return ECLAB_ForceReconnect(g_config.conn, g_config.deviceNumber);
}

int BIO_ECLAB_TestConnection(void) {
    if (!g_initialized) return ERR_NOT_INITIALIZED;
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    return ECLAB_TestConnection(g_config.conn);
}

int BIO_ECLAB_StopMeasurement(void) {
    if (!g_initialized) return ERR_NOT_INITIALIZED;
    if (!g_config.conn) return ECLAB_ERR_INVALID_CONNECTION;

    return ECLAB_StopChannel(g_config.conn, g_config.deviceNumber, g_config.channelNumber);
}
