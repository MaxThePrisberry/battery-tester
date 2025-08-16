/******************************************************************************
 * cdaq_utils.c
 * 
 * cDAQ Utilities Module Implementation
 * Handles NI cDAQ slots 2 and 3 for thermocouple monitoring
 ******************************************************************************/

#include "cdaq_utils.h"
#include "logging.h"

/******************************************************************************
 * Module State
 ******************************************************************************/
static struct {
    TaskHandle slot2TaskHandle;
    TaskHandle slot3TaskHandle;
    int initialized;
} g_cdaq = {0};

/******************************************************************************
 * Internal Function Prototypes
 ******************************************************************************/
static int CDAQ_CreateSlotTask(int slot, TaskHandle *taskHandle);

/******************************************************************************
 * Public Function Implementation
 ******************************************************************************/

int CDAQ_Initialize(void) {
    if (g_cdaq.initialized) {
        LogWarning("cDAQ module already initialized");
        return SUCCESS;
    }
    
    LogMessage("Initializing cDAQ thermocouple slots 2 and 3...");
    
    // Initialize slot 2
    int result = CDAQ_CreateSlotTask(2, &g_cdaq.slot2TaskHandle);
    if (result != SUCCESS) {
        LogError("Failed to initialize cDAQ slot 2");
        return result;
    }
    LogMessage("cDAQ slot 2 initialized with %d thermocouples", CDAQ_CHANNELS_PER_SLOT);
    
    // Initialize slot 3
    result = CDAQ_CreateSlotTask(3, &g_cdaq.slot3TaskHandle);
    if (result != SUCCESS) {
        LogError("Failed to initialize cDAQ slot 3");
        CDAQ_Cleanup();
        return result;
    }
    LogMessage("cDAQ slot 3 initialized with %d thermocouples", CDAQ_CHANNELS_PER_SLOT);
    
    g_cdaq.initialized = 1;
    LogMessage("cDAQ module initialized successfully");
    return SUCCESS;
}

void CDAQ_Cleanup(void) {
    LogMessage("Cleaning up cDAQ module...");
    
    if (g_cdaq.slot2TaskHandle != 0) {
        DAQmxStopTask(g_cdaq.slot2TaskHandle);
        DAQmxClearTask(g_cdaq.slot2TaskHandle);
        g_cdaq.slot2TaskHandle = 0;
        LogMessage("Cleaned up cDAQ slot 2 task");
    }
    
    if (g_cdaq.slot3TaskHandle != 0) {
        DAQmxStopTask(g_cdaq.slot3TaskHandle);
        DAQmxClearTask(g_cdaq.slot3TaskHandle);
        g_cdaq.slot3TaskHandle = 0;
        LogMessage("Cleaned up cDAQ slot 3 task");
    }
    
    g_cdaq.initialized = 0;
    LogMessage("cDAQ module cleaned up");
}

int CDAQ_ReadTC(int slot, int tc_number, double *temperature) {
    if (!g_cdaq.initialized) {
        LogError("cDAQ module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    if (!temperature) {
        return ERR_NULL_POINTER;
    }
    
    if (slot != 2 && slot != 3) {
        LogError("Invalid slot %d (only slots 2 and 3 supported)", slot);
        return ERR_INVALID_PARAMETER;
    }
    
    if (tc_number < 0 || tc_number >= CDAQ_CHANNELS_PER_SLOT) {
        LogError("Thermocouple number %d out of range (0-%d)", 
                tc_number, CDAQ_CHANNELS_PER_SLOT - 1);
        return ERR_INVALID_PARAMETER;
    }
    
    // Select the appropriate task handle
    TaskHandle taskHandle = (slot == 2) ? g_cdaq.slot2TaskHandle : g_cdaq.slot3TaskHandle;
    
    // Read all channels and return the requested one
    float64 data[CDAQ_CHANNELS_PER_SLOT];
    int32 result = DAQmxReadAnalogF64(taskHandle, 1, CDAQ_READ_TIMEOUT,
                                     DAQmx_Val_GroupByChannel, data, CDAQ_CHANNELS_PER_SLOT, 
                                     NULL, NULL);
    if (result != 0) {
        LogError("Failed to read thermocouple data from slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    *temperature = data[tc_number];
    return SUCCESS;
}

int CDAQ_ReadTCArray(int slot, double *temperatures, int *num_read) {
    if (!g_cdaq.initialized) {
        LogError("cDAQ module not initialized");
        return ERR_NOT_INITIALIZED;
    }
    
    if (!temperatures || !num_read) {
        return ERR_NULL_POINTER;
    }
    
    if (slot != 2 && slot != 3) {
        LogError("Invalid slot %d (only slots 2 and 3 supported)", slot);
        return ERR_INVALID_PARAMETER;
    }
    
    // Select the appropriate task handle
    TaskHandle taskHandle = (slot == 2) ? g_cdaq.slot2TaskHandle : g_cdaq.slot3TaskHandle;
    
    // Read all channels
    float64 data[CDAQ_CHANNELS_PER_SLOT];
    int32 result = DAQmxReadAnalogF64(taskHandle, 1, CDAQ_READ_TIMEOUT,
                                     DAQmx_Val_GroupByChannel, data, CDAQ_CHANNELS_PER_SLOT, 
                                     NULL, NULL);
    if (result != 0) {
        LogError("Failed to read thermocouple array from slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    // Copy data to output array
    for (int i = 0; i < CDAQ_CHANNELS_PER_SLOT; i++) {
        temperatures[i] = data[i];
    }
    
    *num_read = CDAQ_CHANNELS_PER_SLOT;
    return SUCCESS;
}

/******************************************************************************
 * Internal Function Implementation
 ******************************************************************************/

static int CDAQ_CreateSlotTask(int slot, TaskHandle *taskHandle) {
    // Create task name
    char taskName[64];
    snprintf(taskName, sizeof(taskName), "TC_Slot_%d", slot);
    
    // Create DAQmx task
    int32 result = DAQmxCreateTask(taskName, taskHandle);
    if (result != 0) {
        LogError("Failed to create cDAQ task for slot %d: %d", slot, result);
        return ERR_OPERATION_FAILED;
    }
    
    // Add thermocouple channels (0-15)
    for (int i = 0; i < CDAQ_CHANNELS_PER_SLOT; i++) {
        char channelName[64];
        snprintf(channelName, sizeof(channelName), "cDAQ1Mod%d/ai%d", slot, i);
        
        result = DAQmxCreateAIThrmcplChan(*taskHandle, channelName, "", 
                                         CDAQ_TC_MIN_TEMP, CDAQ_TC_MAX_TEMP, 
                                         DAQmx_Val_DegC, DAQmx_Val_K_Type_TC, 
                                         DAQmx_Val_BuiltIn, CDAQ_CJC_TEMP, NULL);
        if (result != 0) {
            LogError("Failed to create thermocouple channel %s: %d", channelName, result);
            DAQmxClearTask(*taskHandle);
            *taskHandle = 0;
            return ERR_OPERATION_FAILED;
        }
    }
    
    // Start the task
    result = DAQmxStartTask(*taskHandle);
    if (result != 0) {
        LogError("Failed to start cDAQ task for slot %d: %d", slot, result);
        DAQmxClearTask(*taskHandle);
        *taskHandle = 0;
        return ERR_OPERATION_FAILED;
    }
    
    return SUCCESS;
}