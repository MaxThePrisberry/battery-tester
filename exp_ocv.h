/******************************************************************************
 * exp_ocv.h
 *
 * OCV (Open Circuit Voltage) Experiment Module
 * Standalone OCV measurement with optional temperature control and rest period.
 * Also provides API for integration as Phase 0 in baseline experiment.
 ******************************************************************************/

#ifndef EXP_OCV_H
#define EXP_OCV_H

#include "common.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "biologic_abstract.h"
#include "teensy_dll.h"
#include "teensy_queue.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include "cdaq_utils.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Rest period defaults
#define OCV_REST_DEFAULT_TIME        5.0     // 5 min default rest (minutes, for UI default)

// Temperature control constants
#define OCV_TEMP_TOLERANCE           2.0     // C tolerance for temperature target
#define OCV_TEMP_CHECK_INTERVAL      10.0    // Seconds between temperature checks
#define OCV_TEMP_TIMEOUT_SEC         1800    // 30 minutes max wait for temperature
#define OCV_TEMP_STABILIZE_TIME      300     // 5 minutes stabilization after reaching target

// OCV measurement (duration controlled by Bio-Logic .mps file)
#define OCV_EXP_SAMPLE_INTERVAL_S    0.1     // 10 Hz sampling
#define OCV_EXP_TIMEOUT_MS           120000  // 2 min timeout

// File system
#define OCV_DATA_DIR                 "data"
#define OCV_RESULTS_FILE             "ocv_results.ini"
#define OCV_SETTINGS_FILE            "experiment_settings.ini"
#define OCV_REST_TEMP_FILE           "rest_temperatures.csv"
#define OCV_MEASUREMENT_FILE         "ocv_measurement.csv"
#define OCV_LOG_FILE                 "experiment.log"

// CSV headers
#define OCV_REST_TEMP_HEADER         "Time_s,DTB_Avg_C,DTB1_C,DTB2_C,TC0_C,TC1_C"

// Experiment limits
#define OCV_MAX_EXPERIMENT_TIME      7200    // 2 hours maximum

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    OCV_STATE_IDLE = 0,
    OCV_STATE_PREPARING,
    OCV_STATE_SWITCHING_RELAY,
    OCV_STATE_TEMP_WAIT,
    OCV_STATE_TEMP_STABILIZE,
    OCV_STATE_RESTING,
    OCV_STATE_MEASURING,
    OCV_STATE_COMPLETED,
    OCV_STATE_ERROR,
    OCV_STATE_CANCELLED
} OCVExperimentState;

// Experiment parameters from UI
typedef struct {
    char batteryName[64];        // User-defined battery name
    double targetTemperature;    // DTB target temperature (C)
    double tempTolerance;        // Temperature tolerance (C)
    double restTime;             // Rest time before measurement (seconds)
    unsigned int logInterval;    // Temperature logging interval during rest (seconds)
} OCVExperimentParams;

// OCV measurement result
typedef struct {
    double finalOCV_V;           // Last OCV reading (V)
    double averageOCV_V;         // Average OCV over measurement (V)
    double minOCV_V;             // Minimum OCV during measurement (V)
    double maxOCV_V;             // Maximum OCV during measurement (V)
    double measurementDuration_s; // Actual measurement duration (s)
    int numDataPoints;           // Number of data points collected
    double tempAtMeasurement;    // DTB average temperature at measurement time (C)
    BIO_TechniqueData *rawOCVData; // Raw OCV data from Bio-Logic (caller must free with OCV_FreeResult)
} OCVMeasurementResult;

// Experiment context
typedef struct {
    OCVExperimentState state;
    OCVExperimentParams params;

    // Cancellation
    volatile int cancelRequested;

    // Timing
    double experimentStartTime;
    double experimentEndTime;
    double restStartTime;
    double lastLogTime;

    // Temperature management
    int dtbReady;
    int temperatureStable;
    double temperatureStabilizationStart;

    // Result
    OCVMeasurementResult result;

    // File system
    char experimentDirectory[MAX_PATH_LENGTH];
    FILE *restTempLogFile;
    FILE *experimentLogFile;

    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int statusControl;
    int outputControl;

} OCVExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * UI callback for Start/Stop OCV experiment button
 */
int CVICALLBACK StartOCVExperimentCallback(int panel, int control, int event,
                                           void *callbackData, int eventData1,
                                           int eventData2);

/**
 * Check if an OCV experiment is running
 * @return 1 if running, 0 if not
 */
int OCVExperiment_IsRunning(void);

/**
 * Abort a running OCV experiment
 * @return SUCCESS or error code
 */
int OCVExperiment_Abort(void);

/**
 * Cleanup OCV experiment module (call on application shutdown)
 */
void OCVExperiment_Cleanup(void);

/**
 * Run a full OCV experiment in a specified subdirectory with custom phase name.
 * Called from within an existing experiment thread - does NOT create its own thread.
 *
 * @param params - Experiment parameters
 * @param experimentDir - Base experiment directory (phaseSubDir will be created under this)
 * @param phaseSubDir - Subdirectory name (e.g., "phase_0", "phase_5")
 * @param phaseName - Display name for log/status messages (e.g., "Phase 0", "Phase 5")
 * @param statusControl - Tab panel status control for updates (0 to skip)
 * @param tabPanelHandle - Tab panel handle (0 to skip UI updates)
 * @param cancelFlag - Pointer to volatile cancel flag
 * @param result - Output: measurement result (caller must call OCV_FreeResult)
 * @return SUCCESS or error code
 */
int OCV_RunExperimentInDir(const OCVExperimentParams *params,
                           const char *experimentDir,
                           const char *phaseSubDir,
                           const char *phaseName,
                           int statusControl,
                           int tabPanelHandle,
                           volatile int *cancelFlag,
                           OCVMeasurementResult *result);

/**
 * Run a full OCV experiment (for baseline Phase 0 integration).
 * Convenience wrapper that calls OCV_RunExperimentInDir with "phase_0"/"Phase 0".
 *
 * @param params - Experiment parameters
 * @param experimentDir - Base experiment directory (phase_0 subdir will be created)
 * @param statusControl - Tab panel status control for updates (0 to skip)
 * @param tabPanelHandle - Tab panel handle (0 to skip UI updates)
 * @param cancelFlag - Pointer to volatile cancel flag
 * @param result - Output: measurement result (caller must call OCV_FreeResult)
 * @return SUCCESS or error code
 */
int OCV_RunExperiment(const OCVExperimentParams *params,
                      const char *experimentDir,
                      int statusControl,
                      int tabPanelHandle,
                      volatile int *cancelFlag,
                      OCVMeasurementResult *result);

/**
 * Quick OCV measurement without temperature control or rest period.
 * Just switches to Bio-Logic, measures, and returns.
 *
 * @param cancelFlag - Pointer to volatile cancel flag (can be NULL)
 * @param result - Output: measurement result (caller must call OCV_FreeResult)
 * @return SUCCESS or error code
 */
int OCV_QuickMeasurement(volatile int *cancelFlag, OCVMeasurementResult *result);

/**
 * Free memory associated with an OCV measurement result.
 * Call this when done with the result from OCV_RunExperiment or OCV_QuickMeasurement.
 *
 * @param result - Result to free
 */
void OCV_FreeResult(OCVMeasurementResult *result);

#endif // EXP_OCV_H
