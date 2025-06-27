/******************************************************************************
 * battery_utils.h
 * 
 * Battery management utility functions
 * Provides common battery operations used across multiple experiments
 ******************************************************************************/

#ifndef BATTERY_UTILS_H
#define BATTERY_UTILS_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Completion reasons for battery operations
typedef enum {
    BATTERY_OP_SUCCESS = 0,        // Operation completed successfully
    BATTERY_OP_TIMEOUT,            // Operation timed out
    BATTERY_OP_CURRENT_THRESHOLD,  // Current dropped below threshold
    BATTERY_OP_ERROR,              // PSB communication error
    BATTERY_OP_ABORTED            // Operation was aborted
} BatteryOpResult;

// Parameters for discharging a specific capacity
typedef struct {
    // Input parameters
    double targetCapacity_mAh;     // How much to discharge
    double dischargeCurrent_A;     // Discharge current to use
    double dischargeVoltage_V;     // Voltage limit during discharge
    double currentThreshold_A;     // Stop if current drops below this
    double timeoutSeconds;         // Maximum time to run
    int updateIntervalMs;          // How often to update/check (minimum 100ms)
    
    // UI update callbacks (optional)
    void (*progressCallback)(double percentComplete, double mAhDischarged);
    void (*statusCallback)(const char* message);
    
    // Control handles for UI updates (optional)
    int panelHandle;
    int statusControl;
    int progressControl;
    
    // Output results
    double actualDischarged_mAh;   // Actual amount discharged
    double finalVoltage_V;         // Final battery voltage
    double elapsedTime_s;          // Time taken
    BatteryOpResult result;        // Completion reason
} DischargeParams;

/******************************************************************************
 * Public Functions
 ******************************************************************************/

/**
 * Discharge a specific amount of capacity from the battery
 * This is a blocking function that uses coulomb counting
 * 
 * @param psbHandle - PSB handle (can be NULL to use global queue manager)
 * @param params - Discharge parameters and results
 * @return SUCCESS or error code
 */
int Battery_DischargeCapacity(PSB_Handle *psbHandle, DischargeParams *params);

/**
 * Calculate capacity increment using trapezoidal rule
 * @param current1_A - Current at start of interval (A)
 * @param current2_A - Current at end of interval (A)
 * @param deltaTime_s - Time interval (seconds)
 * @return Capacity in mAh
 */
double Battery_CalculateCapacityIncrement(double current1_A, double current2_A, 
                                         double deltaTime_s);

#endif // BATTERY_UTILS_H