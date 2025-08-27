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
#define STABILIZE_TIME            2.0    // Time to wait after enabling output

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
	
	result = PSB_SetPowerQueued(PSB_BATTERY_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
	result = PSB_SetSinkPowerQueued(PSB_BATTERY_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power: %s", PSB_GetErrorString(result));
    }
    
    // Enable output
    PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Wait for output to stabilize
    LogMessage("Waiting for output to stabilize...");
    Delay(STABILIZE_TIME);
    
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
    
	LogMessage(params->wasCharging ? "Charging..." : "Discharging...");
	
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
            LogWarning("Voltage target timeout reached after %.1f minutes", elapsedTime / 60.0);
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
			
			// Check for cancellation
			if (params->cancelFlag && *(params->cancelFlag)) {
			    LogMessage("Battery_GoToVoltage operation cancelled by user");
			    params->result = BATTERY_OP_ABORTED;
			    break;
			}
			
			// Update graphs if provided
			if (params->graph1Handle > 0 && params->panelHandle > 0) {
			    PlotDataPoint(params->panelHandle, params->graph1Handle, 
			                  elapsedTime / 60.0, fabs(status.current), VAL_SOLID_CIRCLE, VAL_RED);
			}

			if (params->graph2Handle > 0 && params->panelHandle > 0) {
			    PlotDataPoint(params->panelHandle, params->graph2Handle, 
			                  elapsedTime / 60.0, status.voltage, VAL_SOLID_CIRCLE, VAL_BLUE);
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
                
                // Log progress periodically if debug is activated (every 5 seconds)
                if ((currentTime - lastLogTime) >= 5.0) {
                    LogDebug("%s progress: V=%.3f, I=%.3f A, Capacity=%.2f mAh", 
                              params->wasCharging ? "Charge" : "Discharge",
                              status.voltage, status.current, fabs(accumulatedCapacity_mAh));
                    
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

int Battery_TransferCapacity(CapacityTransferParams *params) {
    if (!params) return ERR_INVALID_PARAMETER;
    
    // Validate input parameters
    if (params->targetCapacity_mAh <= 0 || 
        params->current_A <= 0 ||
        params->voltage_V < 0 ||
        params->timeoutSeconds <= 0) {
        LogError("Battery_TransferCapacity: Invalid parameters");
        return ERR_INVALID_PARAMETER;
    }
    
    // Validate mode
    if (params->mode != BATTERY_MODE_CHARGE && params->mode != BATTERY_MODE_DISCHARGE) {
        LogError("Battery_TransferCapacity: Invalid mode %d", params->mode);
        return ERR_INVALID_PARAMETER;
    }
    
    // Ensure minimum update interval
    if (params->updateIntervalMs < MIN_UPDATE_INTERVAL_MS) {
        params->updateIntervalMs = MIN_UPDATE_INTERVAL_MS;
    }
    
    // Initialize results
    params->actualTransferred_mAh = 0.0;
    params->elapsedTime_s = 0.0;
    params->finalVoltage_V = 0.0;
    params->result = BATTERY_OP_ERROR;
    
    int result;
    const char *modeStr = (params->mode == BATTERY_MODE_CHARGE) ? "charge" : "discharge";
    
    LogMessage("Starting %s of %.2f mAh at %.2f A", 
               modeStr, params->targetCapacity_mAh, params->current_A);
    
    // Update status
    char statusMsg[256];
    snprintf(statusMsg, sizeof(statusMsg), "Configuring %s parameters...", modeStr);
    if (params->statusCallback) {
        params->statusCallback(statusMsg);
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, statusMsg);
    }
    
    // Set voltage (as limit/target)
    result = PSB_SetVoltageQueued(params->voltage_V, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set voltage: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Set current based on mode
    if (params->mode == BATTERY_MODE_CHARGE) {
        result = PSB_SetCurrentQueued(params->current_A, DEVICE_PRIORITY_NORMAL);
    } else {
        result = PSB_SetSinkCurrentQueued(params->current_A, DEVICE_PRIORITY_NORMAL);
    }
    if (result != PSB_SUCCESS) {
        LogError("Failed to set current: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Set power limits (like GoToVoltage does)
    result = PSB_SetPowerQueued(PSB_BATTERY_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set power: %s", PSB_GetErrorString(result));
    }
    
    result = PSB_SetSinkPowerQueued(PSB_BATTERY_POWER_MAX, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogWarning("Failed to set sink power: %s", PSB_GetErrorString(result));
    }
    
    // Enable output
    result = PSB_SetOutputEnableQueued(1, DEVICE_PRIORITY_NORMAL);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        return result;
    }
    
    // Wait for output to stabilize
    LogMessage("Waiting for output to stabilize...");
    Delay(STABILIZE_TIME);
    
    // Initialize timing and coulomb counting
    double startTime = Timer();
    double lastUpdateTime = startTime;
    double lastLogTime = startTime;
    double accumulatedCapacity_mAh = 0.0;
    double lastCurrent = 0.0;
    double lastTime = 0.0;
    int firstReading = 1;
    
    // Update status
    snprintf(statusMsg, sizeof(statusMsg), "%s...", 
             (params->mode == BATTERY_MODE_CHARGE) ? "Charging" : "Discharging");
    if (params->statusCallback) {
        params->statusCallback(statusMsg);
    }
    
    // Main transfer loop
    while (1) {
        double currentTime = Timer();
        double elapsedTime = currentTime - startTime;
        
        // Check timeout
        if (elapsedTime > params->timeoutSeconds) {
            LogWarning("%s timeout reached after %.1f minutes", modeStr, elapsedTime / 60.0);
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
                LogError("Failed to read status during %s: %s", modeStr, PSB_GetErrorString(result));
                params->result = BATTERY_OP_ERROR;
                break;
            }
            
            // Check for cancellation
            if (params->cancelFlag && *(params->cancelFlag)) {
                LogMessage("Battery %s operation cancelled by user", modeStr);
                params->result = BATTERY_OP_ABORTED;
                break;
            }
            
            // Store final voltage
            params->finalVoltage_V = status.voltage;
            
            // Check current threshold
            if (fabs(status.current) < params->currentThreshold_A) {
                LogMessage("%s stopped - current below threshold (%.3f A < %.3f A)",
                          modeStr, fabs(status.current), params->currentThreshold_A);
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
                    LogMessage("%s progress: %.1f%% (%.2f / %.2f mAh)", 
                              modeStr, percentComplete, accumulatedCapacity_mAh, params->targetCapacity_mAh);
                    
                    // Update status message
                    if (params->panelHandle > 0 && params->statusControl > 0) {
                        snprintf(statusMsg, sizeof(statusMsg), 
                                 "%s: %.1f%% (%.2f mAh)", 
                                 (params->mode == BATTERY_MODE_CHARGE) ? "Charging" : "Discharging",
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
    params->actualTransferred_mAh = accumulatedCapacity_mAh;
    params->elapsedTime_s = Timer() - startTime;
    
    // Disable output
    PSB_SetOutputEnableQueued(0, DEVICE_PRIORITY_NORMAL);
    
    // Final status update
    char finalMsg[256];
    snprintf(finalMsg, sizeof(finalMsg), "%s complete: %.2f mAh in %.1f minutes", 
             (params->mode == BATTERY_MODE_CHARGE) ? "Charge" : "Discharge",
             params->actualTransferred_mAh, params->elapsedTime_s / 60.0);
    
    if (params->statusCallback) {
        params->statusCallback(finalMsg);
    }
    if (params->panelHandle > 0 && params->statusControl > 0) {
        SetCtrlVal(params->panelHandle, params->statusControl, finalMsg);
    }
    
    LogMessage("%s", finalMsg);
    
    return (params->result == BATTERY_OP_SUCCESS) ? SUCCESS : ERR_OPERATION_FAILED;
}