/******************************************************************************
 * battery_utils.c
 * 
 * Battery management utility functions implementation
 ******************************************************************************/

#include "battery_utils.h"
#include "logging.h"
#include "status.h"
#include <ansi_c.h>

/******************************************************************************
 * Module Constants
 ******************************************************************************/
#define MIN_UPDATE_INTERVAL_MS    100    // Minimum update interval
#define DISCHARGE_STABILIZE_TIME  2.0    // Time to wait after enabling output

/******************************************************************************
 * Public Function Implementations
 ******************************************************************************/

double Battery_CalculateCoulombicEfficiency(double chargeCapacity_mAh, double dischargeCapacity_mAh) {
    if (dischargeCapacity_mAh <= 0) return 0.0;
    return (chargeCapacity_mAh / dischargeCapacity_mAh) * 100.0;
}

double Battery_CalculateEnergyEfficiency(double chargeEnergy_Wh, double dischargeEnergy_Wh) {
    if (chargeEnergy_Wh <= 0) return 0.0;
    return (dischargeEnergy_Wh / chargeEnergy_Wh) * 100.0;
}

double Battery_CalculateCapacityIncrement(double current1_A, double current2_A, 
                                         double deltaTime_s) {
    // Trapezoidal rule: average current * time
    // Convert from A*s to mAh: A * s * 1000 / 3600 = A * s / 3.6
    double averageCurrent = (current1_A + current2_A) / 2.0;
    return averageCurrent * deltaTime_s * 1000.0 / 3600.0;
}

double Battery_CalculateEnergyIncrement(double voltage1_V, double current1_A,
                                       double voltage2_V, double current2_A,
                                       double deltaTime_s) {
    // Calculate average power using trapezoidal rule
    // P = V * I, so average power = average of (V1*I1 + V2*I2)
    double power1_W = voltage1_V * fabs(current1_A);
    double power2_W = voltage2_V * fabs(current2_A);
    double averagePower_W = (power1_W + power2_W) / 2.0;
    
    // Convert from W*s to Wh: W * s / 3600 = Wh
    return averagePower_W * deltaTime_s / 3600.0;
}

int Battery_GoToVoltage(VoltageTargetParams *params) {
    if (!params) return ERR_INVALID_PARAMETER;
    
    // Validate input parameters
    if (params->targetVoltage_V <= 0 || 
        params->maxCurrent_A <= 0 ||
        params->currentThreshold_A < 0 ||
        params->timeoutSeconds <= 0) {
        LogError("Battery_GoToVoltage: Invalid parameters");
        return ERR_INVALID_PARAMETER;
    }
    
    // Ensure minimum update interval
    if (params->updateIntervalMs < MIN_UPDATE_INTERVAL_MS) {
        params->updateIntervalMs = MIN_UPDATE_INTERVAL_MS;
    }
    
    // Initialize results
    params->actualCapacity_mAh = 0.0;
    params->actualEnergy_Wh = 0.0;
    params->elapsedTime_s = 0.0;
    params->startVoltage_V = 0.0;
    params->finalVoltage_V = 0.0;
    params->result = BATTERY_OP_ERROR;
    params->wasCharging = 0;
    
    int result;
    
    // Update status
    if (params->statusCallback) {
        params->statusCallback("Reading battery voltage...");
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, 
                   "Reading battery voltage...");
    }
    
    // Get current battery voltage to determine direction
    PSB_Status initialStatus;
    result = PSB_GetStatusQueued(&initialStatus, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to read initial status: %s", PSB_GetErrorString(result));
        return result;
    }
    
    params->startVoltage_V = initialStatus.voltage;
    
    // Determine if we need to charge or discharge
    double voltageDiff = params->targetVoltage_V - initialStatus.voltage;
    params->wasCharging = (voltageDiff > 0) ? 1 : 0;
    
    LogMessage("Battery voltage: %.3f V, Target: %.3f V - Will %s", 
               initialStatus.voltage, params->targetVoltage_V,
               params->wasCharging ? "CHARGE" : "DISCHARGE");
    
    // Check if already at target
    if (fabs(voltageDiff) < 0.05) {  // 50mV tolerance
        LogMessage("Battery already at target voltage");
        params->finalVoltage_V = initialStatus.voltage;
        params->result = BATTERY_OP_SUCCESS;
        params->elapsedTime_s = 0.0;
        return SUCCESS;
    }
    
    // Update status
    if (params->statusCallback) {
        params->statusCallback(params->wasCharging ? "Configuring charge parameters..." : 
                                                     "Configuring discharge parameters...");
    }
    
    // Set voltage
    result = PSB_SetVoltageQueued(params->targetVoltage_V, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Set current based on direction
    if (params->wasCharging) {
        result = PSB_SetCurrentQueued(params->maxCurrent_A, DEVICE_PRIORITY_NORMAL);
    } else {
        result = PSB_SetSinkCurrentQueued(params->maxCurrent_A, DEVICE_PRIORITY_NORMAL);
    }
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Enable output
    PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Wait for output to stabilize
    LogMessage("Waiting for output to stabilize...");
    Delay(DISCHARGE_STABILIZE_TIME);
    
    // Initialize timing and coulomb counting
    double startTime = Timer();
    double lastUpdateTime = startTime;
    double lastLogTime = startTime;
    double accumulatedCapacity_mAh = 0.0;
    double accumulatedEnergy_Wh = 0.0;
    double lastCurrent = 0.0;
    double lastVoltage = 0.0;
    double lastTime = 0.0;
    int firstReading = 1;
    
    // Update status
    if (params->statusCallback) {
        params->statusCallback(params->wasCharging ? "Charging..." : "Discharging...");
    }
    
    // Main control loop
    while (1) {
        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;
        
        // Check timeout
        if (elapsedTime > params->timeoutSeconds) {
            LogWarning("Voltage target timeout reached after %.1f seconds", elapsedTime);
            params->result = BATTERY_OP_TIMEOUT;
            break;
        }
        
        // Check if update needed
        if ((currentTime - lastUpdateTime) * 1000.0 >= params->updateIntervalMs) {
            lastUpdateTime = currentTime;
            
            // Get current status
            PSB_Status status;
            result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
            if (result != PSB_SUCCESS) {
                LogError("Failed to read status: %s", PSB_GetErrorString(result));
                params->result = BATTERY_OP_ERROR;
                break;
            }
            
            // Store final voltage
            params->finalVoltage_V = status.voltage;
            
            // Check completion criteria
            double currentVoltageDiff = fabs(status.voltage - params->targetVoltage_V);
            bool voltageAtTarget = currentVoltageDiff < 0.05; // 50mV tolerance
            bool currentBelowThreshold = fabs(status.current) < params->currentThreshold_A;
            
            if (voltageAtTarget && currentBelowThreshold) {
                LogMessage("Target voltage reached (%.3f V) and current below threshold (%.3f A < %.3f A)",
                          status.voltage, fabs(status.current), params->currentThreshold_A);
                params->result = BATTERY_OP_SUCCESS;
                break;
            }
            
            // Calculate increments (skip first reading)
            if (!firstReading) {
                double deltaTime = elapsedTime - lastTime;
                
                // Capacity increment
                double capacityIncrement = Battery_CalculateCapacityIncrement(
                    fabs(lastCurrent), fabs(status.current), deltaTime);
                
                // Sign based on charge/discharge
                if (!params->wasCharging) {
                    capacityIncrement = -capacityIncrement;
                }
                accumulatedCapacity_mAh += capacityIncrement;
                
                // Energy increment
                double energyIncrement = Battery_CalculateEnergyIncrement(
                    lastVoltage, lastCurrent, status.voltage, status.current, deltaTime);
                
                // Sign based on charge/discharge
                if (!params->wasCharging) {
                    energyIncrement = -energyIncrement;
                }
                accumulatedEnergy_Wh += energyIncrement;
                
                // Update progress
                if (params->progressCallback) {
                    params->progressCallback(status.voltage, status.current, accumulatedCapacity_mAh);
                }
                
                // Log progress periodically (every 5 seconds)
                if ((currentTime - lastLogTime) >= 5.0) {
                    LogMessage("%s progress: V=%.3f, I=%.3f A, Capacity=%.2f mAh", 
                              params->wasCharging ? "Charge" : "Discharge",
                              status.voltage, status.current, fabs(accumulatedCapacity_mAh));
                    
                    // Update status message
                    if (params->panelHandle > 0 && params->statusControl > 0) {
                        char statusMsg[256];
                        snprintf(statusMsg, sizeof(statusMsg), 
                                 "%s: %.3f V, %.3f A (%.2f mAh)", 
                                 params->wasCharging ? "Charging" : "Discharging",
                                 status.voltage, status.current, fabs(accumulatedCapacity_mAh));
                        SetCtrlVal(params->panelHandle, params->statusControl, statusMsg);
                    }
                    
                    lastLogTime = currentTime;
                }
            }
            
            // Store for next calculation
            lastCurrent = status.current;
            lastVoltage = status.voltage;
            lastTime = elapsedTime;
            firstReading = 0;
        }
        
        // Process events and brief delay
        ProcessSystemEvents();
        Delay(0.05);  // 50ms
    }
    
    // Store final results
    params->actualCapacity_mAh = accumulatedCapacity_mAh;
    params->actualEnergy_Wh = accumulatedEnergy_Wh;
    params->elapsedTime_s = Timer() - startTime;
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Final status update
    char finalMsg[256];
    snprintf(finalMsg, sizeof(finalMsg), "%s complete: %.2f mAh, %.2f Wh in %.1f minutes", 
             params->wasCharging ? "Charge" : "Discharge",
             fabs(params->actualCapacity_mAh), fabs(params->actualEnergy_Wh), 
             params->elapsedTime_s / 60.0);
    
    if (params->statusCallback) {
        params->statusCallback(finalMsg);
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, finalMsg);
    }
    
    LogMessage("%s", finalMsg);
    
    return (params->result == BATTERY_OP_SUCCESS) ? SUCCESS : ERR_OPERATION_FAILED;
}

int Battery_DischargeCapacity(DischargeParams *params) {
    if (!params) return ERR_INVALID_PARAMETER;
    
    // Validate input parameters
    if (params->targetCapacity_mAh <= 0 || 
        params->dischargeCurrent_A <= 0 ||
        params->dischargeVoltage_V < 0 ||
        params->timeoutSeconds <= 0) {
        LogError("Battery_DischargeCapacity: Invalid parameters");
        return ERR_INVALID_PARAMETER;
    }
    
    // Ensure minimum update interval
    if (params->updateIntervalMs < MIN_UPDATE_INTERVAL_MS) {
        params->updateIntervalMs = MIN_UPDATE_INTERVAL_MS;
    }
    
    // Initialize results
    params->actualDischarged_mAh = 0.0;
    params->elapsedTime_s = 0.0;
    params->finalVoltage_V = 0.0;
    params->result = BATTERY_OP_ERROR;
    
    int result;
    
    LogMessage("Starting discharge of %.2f mAh at %.2f A", 
               params->targetCapacity_mAh, params->dischargeCurrent_A);
    
    // Update status
    if (params->statusCallback) {
        params->statusCallback("Configuring discharge parameters...");
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, 
                   "Configuring discharge parameters...");
    }
    
    // Set discharge voltage
    result = PSB_SetVoltageQueued(params->dischargeVoltage_V, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set discharge voltage: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Set sink current
    result = PSB_SetSinkCurrentQueued(params->dischargeCurrent_A, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set sink current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Enable output
    PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
	
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Wait for output to stabilize
    LogMessage("Waiting for output to stabilize...");
    Delay(DISCHARGE_STABILIZE_TIME);
    
    // Initialize timing and coulomb counting
    double startTime = Timer();
    double lastUpdateTime = startTime;
    double lastLogTime = startTime;
    double accumulatedCapacity_mAh = 0.0;
    double lastCurrent = 0.0;
    double lastTime = 0.0;
    int firstReading = 1;
    
    // Update status
    if (params->statusCallback) {
        params->statusCallback("Discharging...");
    }
    
    // Main discharge loop
    while (1) {
        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;
        
        // Check timeout
        if (elapsedTime > params->timeoutSeconds) {
            LogWarning("Discharge timeout reached after %.1f seconds", elapsedTime);
            params->result = BATTERY_OP_TIMEOUT;
            break;
        }
        
        // Check if update needed
        if ((currentTime - lastUpdateTime) * 1000.0 >= params->updateIntervalMs) {
            lastUpdateTime = currentTime;
            
            // Get current status
            PSB_Status status;
            result = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_NORMAL);
            if (result != PSB_SUCCESS) {
                LogError("Failed to read status during discharge: %s", PSB_GetErrorString(result));
                params->result = BATTERY_OP_ERROR;
                break;
            }
            
            // Store final voltage
            params->finalVoltage_V = status.voltage;
            
            // Check current threshold
            if (fabs(status.current) < params->currentThreshold_A) {
                LogMessage("Discharge stopped - current below threshold (%.3f A < %.3f A)",
                          fabs(status.current), params->currentThreshold_A);
                params->result = BATTERY_OP_CURRENT_THRESHOLD;
                break;
            }
            
            // Calculate capacity increment (skip first reading)
            if (!firstReading) {
                double deltaTime = elapsedTime - lastTime;
                double capacityIncrement = Battery_CalculateCapacityIncrement(
                    fabs(lastCurrent), fabs(status.current), deltaTime);
                
                accumulatedCapacity_mAh += capacityIncrement;
                
                // Check if target reached
                if (accumulatedCapacity_mAh >= params->targetCapacity_mAh) {
                    LogMessage("Target capacity reached: %.2f mAh", accumulatedCapacity_mAh);
                    params->result = BATTERY_OP_SUCCESS;
                    break;
                }
                
                // Update progress
                double percentComplete = (accumulatedCapacity_mAh / params->targetCapacity_mAh) * 100.0;
                percentComplete = CLAMP(percentComplete, 0.0, 100.0);
                
                if (params->progressCallback) {
                    params->progressCallback(percentComplete, accumulatedCapacity_mAh);
                }
                
                // Update progress control if provided
                if (params->panelHandle > 0 && params->progressControl > 0) {
                    SetCtrlVal(params->panelHandle, params->progressControl, percentComplete);
                }
                
                // Log progress periodically (every 5 seconds)
                if ((currentTime - lastLogTime) >= 5.0) {
                    LogMessage("Discharge progress: %.1f%% (%.2f / %.2f mAh)", 
                              percentComplete, accumulatedCapacity_mAh, params->targetCapacity_mAh);
                    
                    // Update status message
                    if (params->panelHandle > 0 && params->statusControl > 0) {
                        char statusMsg[256];
                        snprintf(statusMsg, sizeof(statusMsg), 
                                 "Discharging: %.1f%% (%.2f mAh)", 
                                 percentComplete, accumulatedCapacity_mAh);
                        SetCtrlVal(params->panelHandle, params->statusControl, statusMsg);
                    }
                    
                    lastLogTime = currentTime;
                }
            }
            
            // Store for next calculation
            lastCurrent = status.current;
            lastTime = elapsedTime;
            firstReading = 0;
        }
        
        // Process events and brief delay
        ProcessSystemEvents();
        Delay(0.05);  // 50ms
    }
    
    // Store final results
    params->actualDischarged_mAh = accumulatedCapacity_mAh;
    params->elapsedTime_s = Timer() - startTime;
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Final status update
    char finalMsg[256];
    snprintf(finalMsg, sizeof(finalMsg), "Discharge complete: %.2f mAh in %.1f minutes", 
             params->actualDischarged_mAh, params->elapsedTime_s / 60.0);
    
    if (params->statusCallback) {
        params->statusCallback(finalMsg);
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, finalMsg);
    }
    
    LogMessage("%s", finalMsg);
    
    return (params->result == BATTERY_OP_SUCCESS) ? SUCCESS : ERR_OPERATION_FAILED;
}