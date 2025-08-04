/******************************************************************************
 * exp_capacity.h
 * 
 * Battery Capacity Testing Experiment Module
 * Tests battery capacity through controlled discharge and charge cycles
 ******************************************************************************/

#ifndef EXP_CAPACITY_H
#define EXP_CAPACITY_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Power limit for capacity testing (Watts)
#define CAPACITY_TEST_POWER_LIMIT_W     20.0

// Voltage error margin for charged state verification (Volts)
#define CAPACITY_TEST_VOLTAGE_MARGIN    0.1

// Graph update rate during testing (seconds)
#define CAPACITY_TEST_GRAPH_UPDATE_RATE 3.0

// Maximum test duration for safety (hours)
#define CAPACITY_TEST_MAX_DURATION_H    10.0

// Data directory name
#define CAPACITY_TEST_DATA_DIR          "data"

// Results file name
#define CAPACITY_TEST_RESULTS_FILE      "results.txt"

// CSV file names
#define CAPACITY_TEST_DISCHARGE_FILE    "discharge.csv"
#define CAPACITY_TEST_CHARGE_FILE       "charge.csv"

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Experiment state
typedef enum {
    CAPACITY_STATE_IDLE = 0,
    CAPACITY_STATE_PREPARING,
    CAPACITY_STATE_DISCHARGING,
    CAPACITY_STATE_CHARGING,
    CAPACITY_STATE_COMPLETED,
    CAPACITY_STATE_ERROR,
    CAPACITY_STATE_CANCELLED
} CapacityTestState;

// Test phase
typedef enum {
    CAPACITY_PHASE_DISCHARGE = 0,
    CAPACITY_PHASE_CHARGE
} CapacityTestPhase;

// Data point for logging
typedef struct {
    double time;        // Elapsed time in seconds
    double voltage;     // Voltage in V
    double current;     // Current in A
    double power;       // Power in W
} CapacityDataPoint;

// Test parameters from UI
typedef struct {
    double chargeVoltage;       // Maximum voltage during charge
    double dischargeVoltage;    // Minimum voltage during discharge
    double chargeCurrent;       // Maximum charge current
    double dischargeCurrent;    // Maximum discharge current
    double currentThreshold;    // Current threshold to stop
    unsigned int logInterval;   // Logging interval in seconds
} CapacityTestParams;

// Phase results for tracking
typedef struct {
    double capacity_mAh;        // Total capacity for this phase
    double energy_Wh;           // Total energy for this phase
    double duration_s;          // Phase duration
    double startVoltage;        // Starting voltage
    double endVoltage;          // Ending voltage
    double avgCurrent;          // Average current
    double avgVoltage;          // Average voltage
    double sumCurrent;          // Sum for averaging
    double sumVoltage;          // Sum for averaging
    int dataPoints;             // Number of data points
} PhaseResults;

// Test context
typedef struct {
    CapacityTestState state;
    CapacityTestParams params;
    
    // Timing
    double testStartTime;        // Overall test start
    double testEndTime;          // Overall test end
    double phaseStartTime;       // Current phase start
    double lastLogTime;
    double lastGraphUpdate;
    
    // Data collection
    FILE *csvFile;
    char testDirectory[MAX_PATH_LENGTH];  // Directory for this test run
    double accumulatedCapacity_mAh;       // Current phase capacity
    double accumulatedEnergy_Wh;          // Current phase energy
    double lastCurrent;
    double lastTime;
    int dataPointCount;
    
    // Results tracking
    PhaseResults dischargeResults;
    PhaseResults chargeResults;
    
    // UI handles
    int mainPanelHandle;     // Main panel handle
    int tabPanelHandle;      // Tab panel handle
    int buttonControl;       // Button control ID on tab panel
    int statusControl;
    int capacityControl;
    int graph1Handle;        // Current vs Time
    int graph2Handle;        // Voltage vs Time    
} CapacityTestContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

// Main callback for starting/stopping capacity experiment
int CVICALLBACK StartCapacityExperimentCallback(int panel, int control, int event,
                                               void *callbackData, int eventData1, 
                                               int eventData2);

// Cleanup capacity test module
void CapacityTest_Cleanup(void);

// Check if a capacity test is running (returns 1 if running, 0 if not)
int CapacityTest_IsRunning(void);

// Abort a running capacity test
int CapacityTest_Abort(void);

#endif // EXP_CAPACITY_H