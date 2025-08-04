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

typedef struct {
    // Input parameters
    double targetVoltage_V;        // Target voltage to reach
    double maxCurrent_A;           // Maximum current (will be charge or discharge)
    double currentThreshold_A;     // Stop when current drops below this
    double timeoutSeconds;         // Maximum time to run
    int updateIntervalMs;          // How often to update/check (minimum 100ms)
    
    // UI update callbacks (optional)
    void (*progressCallback)(double voltage_V, double current_A, double mAhTransferred);
    void (*statusCallback)(const char* message);
    
    // Control handles for UI updates (optional)
    int panelHandle;
    int statusControl;
    int progressControl;
    
    // Output results
    double actualCapacity_mAh;     // Actual capacity transferred (+ for charge, - for discharge)
    double actualEnergy_Wh;        // Actual energy transferred (+ for charge, - for discharge)
    double startVoltage_V;         // Starting battery voltage
    double finalVoltage_V;         // Final battery voltage
    double elapsedTime_s;          // Time taken
    BatteryOpResult result;        // Completion reason
    int wasCharging;               // 1 if charged, 0 if discharged
} VoltageTargetParams;

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
 * Calculate coulombic efficiency (charge efficiency)
 * @param chargeCapacity_mAh - Capacity during charge
 * @param dischargeCapacity_mAh - Capacity during discharge
 * @return Efficiency percentage (0-100)
 */
double Battery_CalculateCoulombicEfficiency(double chargeCapacity_mAh, double dischargeCapacity_mAh);

/**
 * Calculate round-trip energy efficiency
 * @param chargeEnergy_Wh - Energy consumed during charge
 * @param dischargeEnergy_Wh - Energy delivered during discharge
 * @return Efficiency percentage (0-100)
 */
double Battery_CalculateEnergyEfficiency(double chargeEnergy_Wh, double dischargeEnergy_Wh);

/**
 * Calculate capacity increment using trapezoidal rule
 * @param current1_A - Current at start of interval (A)
 * @param current2_A - Current at end of interval (A)
 * @param deltaTime_s - Time interval (seconds)
 * @return Capacity in mAh
 */
double Battery_CalculateCapacityIncrement(double current1_A, double current2_A, 
                                         double deltaTime_s);

/**
 * Calculate energy increment using trapezoidal rule
 * @param voltage1_V - Voltage at start of interval (V)
 * @param current1_A - Current at start of interval (A)
 * @param voltage2_V - Voltage at end of interval (V)
 * @param current2_A - Current at end of interval (A)
 * @param deltaTime_s - Time interval (seconds)
 * @return Energy in Wh (positive value, caller determines sign based on charge/discharge)
 */
double Battery_CalculateEnergyIncrement(double voltage1_V, double current1_A,
                                       double voltage2_V, double current2_A,
                                       double deltaTime_s);

/**
 * Charge or discharge battery to a target voltage
 * Automatically determines whether to charge or discharge based on current voltage
 * This is a blocking function that uses coulomb counting
 * 
 * @param params - Target voltage parameters and results
 * @return SUCCESS or error code
 */
int Battery_GoToVoltage(VoltageTargetParams *params);

/**
 * Discharge a specific amount of capacity from the battery
 * This is a blocking function that uses coulomb counting
 * 
 * @param params - Discharge parameters and results
 * @return SUCCESS or error code
 */
int Battery_DischargeCapacity(DischargeParams *params);

#endif // BATTERY_UTILS_H