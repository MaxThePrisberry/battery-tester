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
 * Define Constants
 ******************************************************************************/

// Add these constants to common.h for the baseline experiment

/******************************************************************************
 * Baseline Experiment Configuration Constants
 ******************************************************************************/

// Temperature and Timing Constants
#define BASELINE_TEMP_TOLERANCE         2.0     // °C tolerance for temperature target
#define BASELINE_TEMP_CHECK_INTERVAL    5.0     // Seconds between temperature checks
#define BASELINE_TEMP_TIMEOUT_SEC       1800    // 30 minutes max wait for temperature
#define BASELINE_VOLTAGE_MARGIN         0.1     // V - voltage tolerance
#define BASELINE_MAX_POWER              30      // 30 Watts
#define BASELINE_TIMEOUT_SEC            36000   // Experiment timeout in seconds

// Data Directory and Files
#define BASELINE_DATA_DIR               "data"
#define BASELINE_RESULTS_FILE           "baseline_summary.txt"
#define BASELINE_DETAILS_FILE_PREFIX    "eis_details_"
#define BASELINE_PHASE1_FILE            "phase1_discharge.csv"
#define BASELINE_PHASE2_CHARGE_FILE     "phase2_charge.csv"
#define BASELINE_PHASE2_DISCHARGE_FILE  "phase2_discharge.csv"
#define BASELINE_PHASE2_RESULTS_FILE    "phase2_capacity_results.txt"
#define BASELINE_PHASE3_FILE            "phase3_charge_eis.csv"
#define BASELINE_PHASE4_FILE            "phase4_discharge_50pct.csv"

// Experiment Limits
#define BASELINE_MAX_EIS_RETRY          1       // Retry failed measurements once

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    BASELINE_STATE_IDLE = 0,
    BASELINE_STATE_PREPARING,
    BASELINE_STATE_PHASE1_DISCHARGE,
    BASELINE_STATE_PHASE1_TEMP_WAIT,
    BASELINE_STATE_PHASE2_CAPACITY,
    BASELINE_STATE_PHASE3_EIS_CHARGE,
    BASELINE_STATE_PHASE4_DISCHARGE_50,
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
    double targetTemperature;    // DTB target temperature
    double eisInterval;          // SOC percentage between EIS measurements
    double currentThreshold;     // Current threshold for operation completion
    unsigned int logInterval;    // Data logging interval in seconds
    double chargeVoltage;        // From PANEL_NUM_SET_CHARGE_V
    double dischargeVoltage;     // From PANEL_NUM_SET_DISCHARGE_V
    double chargeCurrent;        // From PANEL_NUM_SET_CHARGE_I
    double dischargeCurrent;     // From PANEL_NUM_SET_DISCHARGE_I
} BaselineExperimentParams;

// Temperature data point
typedef struct {
    double timestamp;            // Time since experiment start
    double dtbTemperature;       // DTB measured temperature
    double tc0Temperature;       // Thermocouple 0 temperature
    double tc1Temperature;       // Thermocouple 1 temperature
} TemperatureDataPoint;

// Data point for capacity phases
typedef struct {
    double time;                 // Elapsed time in seconds
    double voltage;              // Voltage in V
    double current;              // Current in A
    double power;                // Power in W
    double temperature_dtb;      // DTB temperature
    double temperature_tc0;      // TC0 temperature
    double temperature_tc1;      // TC1 temperature
} BaselineDataPoint;

// EIS measurement data (similar to SOCEIS)
typedef struct {
    double targetSOC;            // Target SOC for this measurement
    double actualSOC;            // Actual SOC when measured
    double ocvVoltage;           // Open circuit voltage
    double timestamp;            // Time since experiment start
    TemperatureDataPoint tempData; // Temperature readings during measurement
    BIO_TechniqueData *ocvData;   // Raw OCV data
    BIO_TechniqueData *geisData;  // Raw GEIS data
    // Calculated impedance values from GEIS
    double *frequencies;         // Array of frequencies
    double *zReal;              // Real impedance values
    double *zImag;              // Imaginary impedance values
    int numPoints;              // Number of impedance points
} BaselineEISMeasurement;

// Phase results tracking
typedef struct {
    double capacity_mAh;         // Total capacity for this phase
    double energy_Wh;            // Total energy for this phase
    double duration_s;           // Phase duration
    double startVoltage;         // Starting voltage
    double endVoltage;           // Ending voltage
    double avgCurrent;           // Average current
    double avgVoltage;           // Average voltage
    double avgTemperature_dtb;   // Average DTB temperature
    double avgTemperature_tc0;   // Average TC0 temperature
    double avgTemperature_tc1;   // Average TC1 temperature
    int dataPoints;              // Number of data points
} BaselinePhaseResults;

// Experiment context
typedef struct {
    BaselineExperimentState state;
    BaselineExperimentParams params;
    BaselineExperimentPhase currentPhase;
    
    // Timing
    double experimentStartTime;
    double experimentEndTime;
    double phaseStartTime;
    double lastLogTime;
    double lastGraphUpdate;
    double lastTempCheck;
    
    // Temperature control
    int dtbReady;                // Flag indicating DTB reached target
    
    // Phase 2 (Capacity) results
    BaselinePhaseResults chargeResults;
    BaselinePhaseResults dischargeResults;
    double measuredChargeCapacity_mAh;   // For Phase 4 calculations
    double measuredDischargeCapacity_mAh;
    
    // Phase 3 (EIS) tracking
    double currentSOC;           // Current state of charge (0-100%)
    double accumulatedCapacity_mAh;  // For coulomb counting
    double lastCurrent;
    double lastTime;
    
    // EIS measurements
    BaselineEISMeasurement *eisMeasurements;    // Array of measurements
    int eisMeasurementCount;
    int eisMeasurementCapacity;  // Array capacity
    double *targetSOCs;          // Array of target SOC points
    int numTargetSOCs;
    
    // Data collection
    char experimentDirectory[MAX_PATH_LENGTH];
    FILE *currentLogFile;        // Current phase log file
    int dataPointCount;
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int outputControl;           // BASELINE_NUM_OUTPUT
    int statusControl;           // BASELINE_STR_BASELINE_STATUS
    int graph1Handle;            // Current vs Time
    int graph2Handle;            // Voltage vs Time / OCV vs SOC
    int graphBiologicHandle;     // Nyquist plot
    
    // Device handles
    PSB_Handle *psbHandle;
    int biologicID;
    int dtbSlaveAddress;
    
    // Plot handles
    int currentPlotHandle;
    int ocvPlotHandle;
    int nyquistPlotHandle;
	
	// Cancellation support
	volatile int cancelRequested;
    
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
 * Cleanup baseline experiment module
 */
void BaselineExperiment_Cleanup(void);

#endif // EXP_BASELINE_H