/******************************************************************************
 * exp_soceis.h
 * 
 * Battery State of Charge EIS (SOCEIS) Experiment Module
 * Tests electrochemical impedance spectroscopy at various SOC levels
 ******************************************************************************/

#ifndef EXP_SOCEIS_H
#define EXP_SOCEIS_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "teensy_dll.h"
#include "teensy_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Relay Control
#define SOCEIS_RELAY_PSB_PIN          0    // Teensy pin for PSB relay
#define SOCEIS_RELAY_BIOLOGIC_PIN     1    // Teensy pin for BioLogic relay
#define SOCEIS_RELAY_SWITCH_DELAY_MS  100  // Delay after relay switching
#define SOCEIS_RELAY_STATE_CONNECTED  1    // HIGH = connected
#define SOCEIS_RELAY_STATE_DISCONNECTED 0  // LOW = disconnected

// Measurement Parameters
#define SOCEIS_OCV_DURATION_S         10.0   // OCV measurement duration
#define SOCEIS_OCV_SAMPLE_INTERVAL_S  0.1    // OCV sampling interval
#define SOCEIS_OCV_RECORD_EVERY_DE    1.0    // mV threshold
#define SOCEIS_OCV_RECORD_EVERY_DT    1.0    // seconds threshold
#define SOCEIS_OCV_E_RANGE            KBIO_ERANGE_AUTO  // Auto range

// GEIS parameters (short test values)
#define SOCEIS_GEIS_INITIAL_CURRENT   0.0    // vs OCV
#define SOCEIS_GEIS_DURATION_S        1.0    // Step duration
#define SOCEIS_GEIS_RECORD_EVERY_DT   0.1    // seconds
#define SOCEIS_GEIS_RECORD_EVERY_DE   0.001  // V
#define SOCEIS_GEIS_INITIAL_FREQ      100000.0  // 100 kHz
#define SOCEIS_GEIS_FINAL_FREQ        10.0     // 10 Hz
#define SOCEIS_GEIS_SWEEP_LINEAR      false    // Logarithmic
#define SOCEIS_GEIS_AMPLITUDE_I       0.01     // 10 mA
#define SOCEIS_GEIS_FREQ_NUMBER       10       // Number of frequencies
#define SOCEIS_GEIS_AVERAGE_N         1        // No averaging for speed
#define SOCEIS_GEIS_CORRECTION        false    // No correction
#define SOCEIS_GEIS_WAIT_FOR_STEADY   0.0      // No wait
#define SOCEIS_GEIS_I_RANGE           KBIO_IRANGE_10mA  // 10mA range

// Other Constants
#define SOCEIS_VOLTAGE_MARGIN         0.1     // V - same as capacity test
#define SOCEIS_DATA_DIR               "data"
#define SOCEIS_RESULTS_FILE           "summary.txt"
#define SOCEIS_DETAILS_FILE_PREFIX    "details_"
#define SOCEIS_MAX_EIS_RETRY          1       // Retry failed measurements once

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    SOCEIS_STATE_IDLE = 0,
    SOCEIS_STATE_PREPARING,
    SOCEIS_STATE_CHARGING,
    SOCEIS_STATE_MEASURING_EIS,
    SOCEIS_STATE_DISCHARGING,
    SOCEIS_STATE_COMPLETED,
    SOCEIS_STATE_ERROR,
    SOCEIS_STATE_CANCELLED
} SOCEISTestState;

// Test parameters
typedef struct {
    double eisInterval;          // SOC percentage between measurements
    double batteryCapacity_mAh;  // Battery capacity
    double currentThreshold;     // Current threshold for charge completion
    unsigned int logInterval;    // Logging interval in seconds
    double chargeVoltage;        // From NUM_SET_CHARGE_V
    double dischargeVoltage;     // From NUM_SET_DISCHARGE_V
    double chargeCurrent;        // From NUM_SET_CHARGE_I
    double dischargeCurrent;     // From NUM_SET_DISCHARGE_I
    int dischargeAfter;         // Discharge battery after test
} SOCEISTestParams;

// EIS Measurement Data
typedef struct {
    double targetSOC;            // Target SOC for this measurement
    double actualSOC;            // Actual SOC when measured
    double ocvVoltage;           // Open circuit voltage
    double timestamp;            // Time since experiment start
    BIO_TechniqueData *ocvData;   // Raw OCV data
    BIO_TechniqueData *geisData;  // Raw GEIS data
    // Calculated impedance values from GEIS
    double *frequencies;         // Array of frequencies
    double *zReal;              // Real impedance values
    double *zImag;              // Imaginary impedance values
    int numPoints;              // Number of impedance points
} EISMeasurement;

// Test context
typedef struct {
    SOCEISTestState state;
    SOCEISTestParams params;
    
    // Timing
    double testStartTime;
    double testEndTime;
    double phaseStartTime;
    double lastLogTime;
    double lastGraphUpdate;
    
    // SOC tracking
    double currentSOC;           // Current state of charge (0-100%)
    double accumulatedCapacity_mAh;  // For coulomb counting
    double lastCurrent;
    double lastTime;
    
    // EIS measurements
    EISMeasurement *measurements;    // Array of measurements
    int measurementCount;
    int measurementCapacity;        // Array capacity
    double *targetSOCs;             // Array of target SOC points
    int numTargetSOCs;
    
    // Data collection
    char testDirectory[MAX_PATH_LENGTH];
    FILE *currentLogFile;           // Current phase log file
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int socControl;
    int graph1Handle;        // Current vs Time
    int graph2Handle;        // OCV vs SOC
    int graphBiologicHandle; // Nyquist plot
    
    // Device handles
    PSB_Handle *psbHandle;
    int biologicID;
    
    // Graphs
    int currentPlotHandle;
    int ocvPlotHandle;
    int nyquistPlotHandle;
    
} SOCEISTestContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

// Main entry points
int CVICALLBACK StartSOCEISExperimentCallback(int panel, int control, int event,
                                             void *callbackData, int eventData1, 
                                             int eventData2);

int CVICALLBACK ImportSOCEISSettingsCallback(int panel, int control, int event,
                                            void *callbackData, int eventData1, 
                                            int eventData2);

// Module management
void SOCEISTest_Cleanup(void);
int SOCEISTest_IsRunning(void);
int SOCEISTest_Abort(void);

#endif // EXP_SOCEIS_H