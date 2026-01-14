/******************************************************************************
 * exp_cdc.h
 * 
 * Battery Charge/Discharge Control (CDC) Experiment Module
 * Simple battery charging and discharging operations
 ******************************************************************************/

#ifndef EXP_CDC_H
#define EXP_CDC_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Voltage tolerance for state verification (Volts)
#define CDC_VOLTAGE_TOLERANCE           0.2     // Consider charged/discharged within 0.2V

// Graph update rate during operation (seconds)
#define CDC_GRAPH_UPDATE_RATE          1.0      // Update graph every second

// Maximum operation duration for safety (hours)
#define CDC_MAX_DURATION_H             10.0     // 10 hour timeout

// Power limit to avoid CP mode (Watts)
// CRITICAL: Must use HIGH power limit (1224W) to prevent CP mode!
// Discovery from Phase 3 debugging: LOW power limits (20W/30W) CAUSE CP mode
// PSB firmware interprets "voltage setpoint + low power limit" as CP mode
// Solution: Use PSB_SAFE_POWER_MAX (1224W) like Battery_GoToVoltage()
#define CDC_POWER_LIMIT_W              PSB_SAFE_POWER_MAX  // 1224W - HIGH to prevent CP mode

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Operation mode
typedef enum {
    CDC_MODE_CHARGE = 0,
    CDC_MODE_DISCHARGE
} CDCOperationMode;

// Experiment state
typedef enum {
    CDC_STATE_IDLE = 0,
    CDC_STATE_PREPARING,
    CDC_STATE_RUNNING,
    CDC_STATE_COMPLETED,
    CDC_STATE_ERROR,
    CDC_STATE_CANCELLED
} CDCExperimentState;

// Experiment parameters from UI
typedef struct {
    double targetVoltage;       // Target voltage (charge or discharge)
    double targetCurrent;       // Target current (charge or discharge)
    double currentThreshold;    // Current threshold to stop
    unsigned int logInterval;   // Measurement/update interval in seconds
} CDCExperimentParams;

// Experiment context
typedef struct {
    CDCExperimentState state;
    CDCOperationMode mode;      // CHARGE or DISCHARGE
    CDCExperimentParams params;
    
    // Timing
    double experimentStartTime;
    double lastLogTime;
    double lastGraphUpdate;
    
    // Tracking
    double elapsedTime;
    int dataPointCount;
    double lastCurrent;
    double peakCurrent;         // Track peak current during operation
    
    // UI handles
    int mainPanelHandle;        // Main panel handle
    int tabPanelHandle;         // Tab panel handle
    int activeButtonControl;    // Which button was pressed (charge or discharge)
    int graphHandle;            // Graph control (PANEL_GRAPH_1)
    
    // PSB handle
    PSB_Handle *psbHandle;
    
} CDCExperimentContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

// Callback for charge button
int CVICALLBACK CDCChargeCallback(int panel, int control, int event,
                                 void *callbackData, int eventData1, 
                                 int eventData2);

// Callback for discharge button
int CVICALLBACK CDCDischargeCallback(int panel, int control, int event,
                                    void *callbackData, int eventData1, 
                                    int eventData2);

// Cleanup CDC experiment module
void CDCExperiment_Cleanup(void);

// Check if a CDC operation is running
int CDCExperiment_IsRunning(void);

// Get current operation mode (returns CDC_MODE_CHARGE or CDC_MODE_DISCHARGE, or -1 if not running)
int CDCExperiment_GetMode(void);

// Abort a running CDC operation
int CDCExperiment_Abort(void);

#endif // EXP_CDC_H