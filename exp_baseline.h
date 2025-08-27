/******************************************************************************
 * exp_baseline.h
 * 
 * Baseline Battery Experiment Module
 * Comprehensive experiment combining discharge, capacity testing, EIS, and temperature control
 ******************************************************************************/

#ifndef EXP_BASELINE_H
#define EXP_BASELINE_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "teensy_dll.h"
#include "teensy_queue.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include "cdaq_utils.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Battery Settling Time
#define BASELINE_SETTLING_TIME          60.0    // Seconds to wait for battery relaxation after operations

// Temperature Control Constants (when ENABLE_DTB is 1)
#define BASELINE_TEMP_TOLERANCE         2.0     // °C tolerance for temperature target
#define BASELINE_TEMP_CHECK_INTERVAL    5.0     // Seconds between temperature checks
#define BASELINE_TEMP_TIMEOUT_SEC       1800    // 30 minutes max wait for temperature
#define BASELINE_TEMP_STABILIZE_TIME    300     // 5 minutes stabilization after reaching target

// EIS Retry and Timeout
#define BASELINE_MAX_EIS_RETRY          2       // Retry failed measurements twice
#define BASELINE_EIS_RETRY_DELAY        5.0     // Seconds to wait between retries

// Dynamic SOC Management
#define BASELINE_SOC_TOLERANCE          1.0     // SOC tolerance for target matching
#define BASELINE_MAX_DYNAMIC_TARGETS    20      // Maximum additional SOC targets beyond 100%
#define BASELINE_SOC_OVERCHARGE_LIMIT   150.0   // Maximum SOC before stopping (safety)

// File System Structure
#define BASELINE_DATA_DIR               "data"
#define BASELINE_SUMMARY_FILE           "summary.txt"
#define BASELINE_SETTINGS_FILE          "experiment_settings.ini"
#define BASELINE_MAIN_LOG_FILE          "experiment_log.csv"
#define BASELINE_ERROR_LOG_FILE         "errors.log"

#define BASELINE_PHASE1_DIR             "phase_1"
#define BASELINE_PHASE2_DIR             "phase_2" 
#define BASELINE_PHASE3_DIR             "phase_3"
#define BASELINE_PHASE4_DIR             "phase_4"
#define BASELINE_DIAGNOSTICS_DIR        "diagnostics"

#define BASELINE_PHASE1_DISCHARGE_FILE  "discharge_data.csv"
#define BASELINE_PHASE2_CHARGE_FILE     "charge_data.csv"
#define BASELINE_PHASE2_DISCHARGE_FILE  "discharge_data.csv"
#define BASELINE_PHASE2_RESULTS_FILE    "capacity_results.ini"
#define BASELINE_PHASE3_CHARGE_FILE     "charge_data.csv"
#define BASELINE_PHASE3_EIS_DIR         "eis_measurements"
#define BASELINE_PHASE3_OCV_FILE        "ocv_vs_soc.csv"
#define BASELINE_PHASE4_DISCHARGE_FILE  "discharge_data.csv"
#define BASELINE_PHASE_SUMMARY_FILE     "phase_summary.ini"

// Experiment Limits and Safety
#define BASELINE_MAX_EXPERIMENT_TIME    72000   // 20 hours maximum experiment time
#define BASELINE_POWER_LIMIT            30      // 30W power limit
#define BASELINE_VOLTAGE_SAFETY_MARGIN  0.05    // 50mV safety margin for voltage targets

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    BASELINE_STATE_IDLE = 0,
    BASELINE_STATE_PREPARING,
    BASELINE_STATE_PHASE1_DISCHARGE,
    BASELINE_STATE_PHASE1_TEMP_WAIT,
    BASELINE_STATE_PHASE1_TEMP_STABILIZE,
    BASELINE_STATE_PHASE2_CHARGE,
    BASELINE_STATE_PHASE2_DISCHARGE,
    BASELINE_STATE_PHASE3_SETUP,
    BASELINE_STATE_PHASE3_CHARGING,
    BASELINE_STATE_PHASE3_EIS_MEASUREMENT,
    BASELINE_STATE_PHASE4_DISCHARGE,
    BASELINE_STATE_COMPLETED,
    BASELINE_STATE_ERROR,
    BASELINE_STATE_CANCELLED
} BaselineExperimentState;

// Experiment phase identifier
typedef enum {
    BASELINE_PHASE_1 = 1,  // Initial discharge + temperature setup
    BASELINE_PHASE_2,      // Capacity experiment (charge ? discharge)
    BASELINE_PHASE_3,      // EIS measurements during charge
    BASELINE_PHASE_4       // Discharge to 50%
} BaselineExperimentPhase;

// Experiment parameters from UI
typedef struct {
    double targetTemperature;    // DTB target temperature (°C)
    double eisInterval;          // SOC percentage between EIS measurements
    double currentThreshold;     // Current threshold for operation completion (A)
    unsigned int logInterval;    // Data logging interval in seconds
    double chargeVoltage;        // Maximum charge voltage (V)
    double dischargeVoltage;     // Minimum discharge voltage (V)
    double chargeCurrent;        // Maximum charge current (A)
    double dischargeCurrent;     // Maximum discharge current (A)
} BaselineExperimentParams;

// Temperature data point
typedef struct {
    double timestamp;            // Time since experiment start (s)
    double dtbTemperature;       // DTB measured temperature (°C)
    double tc0Temperature;       // Thermocouple 0 temperature (°C)
    double tc1Temperature;       // Thermocouple 1 temperature (°C)
    char status[64];             // Temperature controller status
} TemperatureDataPoint;

// Generic data point for logging
typedef struct {
    double timestamp;            // Time since experiment start (s)
    double voltage;              // Battery voltage (V)
    double current;              // Battery current (A)
    double power;                // Power (W)
    double soc;                  // State of charge (%)
    TemperatureDataPoint tempData; // Temperature measurements
    BaselineExperimentPhase phase; // Current phase
    char phaseDescription[128];  // Human-readable phase description
} BaselineDataPoint;

// EIS measurement data
typedef struct {
    int measurementIndex;        // Sequential measurement number
    double targetSOC;            // Target SOC for this measurement
    double actualSOC;            // Actual SOC when measured
    double ocvVoltage;           // Open circuit voltage (V)
    double timestamp;            // Time since experiment start (s)
    TemperatureDataPoint tempData; // Temperature readings during measurement
    BIO_TechniqueData *ocvData;   // Raw OCV data
    BIO_TechniqueData *geisData;  // Raw GEIS data
    // Processed impedance data
    double *frequencies;         // Array of frequencies (Hz)
    double *zReal;              // Real impedance values (Ohm)
    double *zImag;              // Imaginary impedance values (Ohm)
    int numPoints;              // Number of impedance points
    int retryCount;             // Number of retries for this measurement
    char filename[MAX_PATH_LENGTH]; // Saved data filename
} BaselineEISMeasurement;

// Phase results tracking
typedef struct {
    BaselineExperimentPhase phase;
    double startTime;            // Phase start time (s since experiment start)
    double endTime;              // Phase end time (s since experiment start)
    double duration;             // Phase duration (s)
    double capacity_mAh;         // Capacity transferred in this phase (mAh)
    double energy_Wh;            // Energy transferred in this phase (Wh)
    double startVoltage;         // Starting voltage (V)
    double endVoltage;           // Ending voltage (V)
    double avgCurrent;           // Average current magnitude (A)
    double avgVoltage;           // Average voltage (V)
    double avgTemperature_dtb;   // Average DTB temperature (°C)
    double avgTemperature_tc0;   // Average TC0 temperature (°C)
    double avgTemperature_tc1;   // Average TC1 temperature (°C)
    int dataPointCount;          // Number of data points collected
    double peakCurrent;          // Peak current observed (A)
    char completionReason[128];  // Why the phase ended
    char phaseDirectory[MAX_PATH_LENGTH]; // Directory for this phase's data
} BaselinePhaseResults;

// Experiment context
typedef struct {
    BaselineExperimentState state;
    BaselineExperimentParams params;
    BaselineExperimentPhase currentPhase;
    
    // Cancellation handling (dual approach for redundancy)
    volatile int cancelRequested;     // Thread-safe cancellation flag
    volatile int emergencyStop;       // Emergency stop flag (device failures)
    
    // Timing and progress
    double experimentStartTime;       // Experiment start timestamp
    double experimentEndTime;         // Experiment end timestamp
    double phaseStartTime;           // Current phase start time
    double lastLogTime;              // Last data logging time
    double lastGraphUpdate;          // Last graph update time
    double lastTempCheck;            // Last temperature check time
    
    // Temperature management
    int dtbReady;                    // Flag indicating DTB reached target
    int temperatureStable;           // Flag indicating temperature is stable
    double temperatureStabilizationStart; // When temp reached target
    
    // Capacity tracking and SOC management
    double measuredChargeCapacity_mAh;    // From Phase 2 charge
    double measuredDischargeCapacity_mAh; // From Phase 2 discharge
    double currentSOC;               // Current state of charge (0-100%+)
    double accumulatedCapacity_mAh;  // For coulomb counting
    double lastCurrent;              // For trapezoidal integration
    double lastTime;                 // For time-based calculations
    double estimatedBatteryCapacity_mAh; // Dynamic capacity estimate
    
    // EIS measurements and dynamic SOC management
    BaselineEISMeasurement *eisMeasurements;    // Array of measurements
    int eisMeasurementCount;         // Number of completed measurements
    int eisMeasurementCapacity;      // Array capacity
    double *targetSOCs;              // Array of target SOC points
    int numTargetSOCs;              // Number of planned SOC targets
    int targetSOCCapacity;          // Array capacity for dynamic growth
    int dynamicTargetsAdded;        // Count of targets added beyond initial plan
    
    // Phase results
    BaselinePhaseResults phase1Results;
    BaselinePhaseResults phase2ChargeResults;
    BaselinePhaseResults phase2DischargeResults;
    BaselinePhaseResults phase3Results;
    BaselinePhaseResults phase4Results;
    
    // File system and logging
    char experimentDirectory[MAX_PATH_LENGTH];
    char currentPhaseDirectory[MAX_PATH_LENGTH];
    FILE *mainLogFile;              // Main experiment log
    FILE *currentPhaseLogFile;      // Current phase data log
    FILE *errorLogFile;             // Error and diagnostic log
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int outputControl;              // BASELINE_NUM_OUTPUT
    int statusControl;              // BASELINE_STR_BASELINE_STATUS
    int graph1Handle;               // Current vs Time
    int graph2Handle;               // Voltage vs Time / OCV vs SOC
    int graphBiologicHandle;        // Nyquist plot
    
    // Device handles and configuration
    PSB_Handle *psbHandle;
    int biologicID;
    int dtbSlaveAddress;
    
    // Graph plot handles
    int currentPlotHandle;
    int voltagePlotHandle;
    int ocvPlotHandle;
    int nyquistPlotHandle;
    
} BaselineExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * Main callback for starting/stopping baseline experiment
 */
int CVICALLBACK StartBaselineExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2);

/**
 * Check if a baseline experiment is running
 * @return 1 if running, 0 if not
 */
int BaselineExperiment_IsRunning(void);

/**
 * Abort a running baseline experiment
 * @return SUCCESS or error code
 */
int BaselineExperiment_Abort(void);

/**
 * Emergency stop - immediately halt all device operations
 * @return SUCCESS or error code
 */
int BaselineExperiment_EmergencyStop(void);

/**
 * Cleanup baseline experiment module
 */
void BaselineExperiment_Cleanup(void);

#endif // EXP_BASELINE_H