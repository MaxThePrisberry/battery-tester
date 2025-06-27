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

double Battery_CalculateCapacityIncrement(double current1_A, double current2_A, 
                                         double deltaTime_s) {
    // Trapezoidal rule: average current * time
    // Convert from A*s to mAh: A * s * 1000 / 3600 = A * s / 3.6
    double averageCurrent = (current1_A + current2_A) / 2.0;
    return averageCurrent * deltaTime_s * 1000.0 / 3600.0;
}

int Battery_DischargeCapacity(PSB_Handle *psbHandle, DischargeParams *params) {
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
    
    // Get queue manager if no handle provided
    PSBQueueManager *queueMgr = NULL;
    if (!psbHandle) {
        queueMgr = PSB_GetGlobalQueueManager();
        if (!queueMgr) {
            LogError("Battery_DischargeCapacity: No PSB handle or queue manager");
            return PSB_ERROR_NOT_CONNECTED;
        }
        psbHandle = PSB_QueueGetHandle(queueMgr);
        if (!psbHandle) {
            LogError("Battery_DischargeCapacity: Failed to get PSB handle from queue");
            return PSB_ERROR_NOT_CONNECTED;
        }
    }
    
    // Initialize results
    params->actualDischarged_mAh = 0.0;
    params->elapsedTime_s = 0.0;
    params->finalVoltage_V = 0.0;
    params->result = BATTERY_OP_ERROR;
    
    int result;
    PSBCommandParams cmdParams = {0};
    PSBCommandResult cmdResult = {0};
    
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
    
    // Pause status monitoring
    Status_Pause();
    
    // Set discharge voltage
    result = PSB_SetVoltageQueued(psbHandle, params->dischargeVoltage_V);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set discharge voltage: %s", PSB_GetErrorString(result));
        Status_Resume();
        return result;
    }
    
    // Set sink current
    result = PSB_SetSinkCurrentQueued(psbHandle, params->dischargeCurrent_A);
    if (result != PSB_SUCCESS) {
        LogError("Failed to set sink current: %s", PSB_GetErrorString(result));
        Status_Resume();
        return result;
    }
    
    // Enable output
    cmdParams.outputEnable.enable = 1;
    result = PSB_QueueCommandBlocking(queueMgr ? queueMgr : PSB_GetGlobalQueueManager(), 
                                    PSB_CMD_SET_OUTPUT_ENABLE,
                                    &cmdParams, PSB_PRIORITY_HIGH, &cmdResult,
                                    PSB_QUEUE_COMMAND_TIMEOUT_MS);
    if (result != PSB_SUCCESS) {
        LogError("Failed to enable output: %s", PSB_GetErrorString(result));
        Status_Resume();
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
            result = PSB_GetStatusQueued(psbHandle, &status);
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
                        SAFE_SPRINTF(statusMsg, sizeof(statusMsg), 
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
    cmdParams.outputEnable.enable = 0;
    PSB_QueueCommandBlocking(queueMgr ? queueMgr : PSB_GetGlobalQueueManager(), 
                           PSB_CMD_SET_OUTPUT_ENABLE,
                           &cmdParams, PSB_PRIORITY_HIGH, &cmdResult,
                           PSB_QUEUE_COMMAND_TIMEOUT_MS);
    
    // Resume status monitoring
    Status_Resume();
    
    // Final status update
    char finalMsg[256];
    SAFE_SPRINTF(finalMsg, sizeof(finalMsg), "Discharge complete: %.2f mAh in %.1f minutes", 
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