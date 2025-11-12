/******************************************************************************
 * biologic_abstract.c
 *
 * Implementation of unified BioLogic abstraction layer
 *
 * This file implements mode dispatching logic that routes calls to the
 * appropriate backend (direct DLL or EC-Lab OLE COM) based on configuration.
 ******************************************************************************/

#include "biologic_abstract.h"
#include "logging.h"

/******************************************************************************
 * Module State
 ******************************************************************************/

static BIO_Config g_abstractConfig = {0};
static bool g_abstractInitialized = false;

/******************************************************************************
 * Initialization and Shutdown
 ******************************************************************************/

int BIO_InitializeAbstract(const BIO_Config *config) {
    if (!config) return ERR_NULL_POINTER;
    if (g_abstractInitialized) return ERR_ALREADY_INITIALIZED;

    LogMessageEx(LOG_DEVICE_BIO, "Initializing BioLogic abstraction layer");
    LogMessageEx(LOG_DEVICE_BIO, "  Mode: %s", BIO_GetModeName(config->mode));

    // Copy configuration
    memcpy(&g_abstractConfig, config, sizeof(BIO_Config));

    int result = SUCCESS;

    // Initialize appropriate backend
    switch (config->mode) {
        case BIO_MODE_DIRECT_DLL:
            LogMessageEx(LOG_DEVICE_BIO, "Initializing Direct DLL mode");
            LogMessageEx(LOG_DEVICE_BIO, "  Device address: %s", config->dll.deviceAddress);

            // Initialize queue manager
            g_abstractConfig.dll.queueMgr = BIO_QueueInit(config->dll.deviceAddress);
            if (!g_abstractConfig.dll.queueMgr) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize queue manager");
                return ERR_NOT_INITIALIZED;
            }

            // Connect to device
            TDeviceInfos_t deviceInfo;
            result = BIO_ConnectQueued(config->dll.deviceAddress,
                                       config->dll.timeout,
                                       &g_abstractConfig.dll.deviceID,
                                       &deviceInfo,
                                       DEVICE_PRIORITY_HIGH);

            if (result != SUCCESS) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to connect to device: %s",
                          BIO_GetErrorString(result));
                BIO_QueueShutdown(g_abstractConfig.dll.queueMgr);
                g_abstractConfig.dll.queueMgr = NULL;
                return result;
            }

            LogMessageEx(LOG_DEVICE_BIO, "Connected to device ID: %d",
                        g_abstractConfig.dll.deviceID);
            break;

        case BIO_MODE_ECLAB_OLECOM:
            LogMessageEx(LOG_DEVICE_BIO, "Initializing EC-Lab OLE COM mode");
            LogMessageEx(LOG_DEVICE_BIO, "  Settings dir: %s", config->eclab.settingsDir);
            LogMessageEx(LOG_DEVICE_BIO, "  Data dir: %s", config->eclab.dataDir);

            // Build EC-Lab configuration
            ECLAB_Config eclabCfg = {0};
            strncpy(eclabCfg.settingsDir, config->eclab.settingsDir, MAX_PATH - 1);
            strncpy(eclabCfg.dataDir, config->eclab.dataDir, MAX_PATH - 1);
            eclabCfg.deviceNumber = config->eclab.deviceNumber;
            eclabCfg.channelNumber = config->eclab.channelNumber;
            strncpy(eclabCfg.ocvTemplate, config->eclab.ocvTemplate, MAX_PATH - 1);
            strncpy(eclabCfg.peisTemplate, config->eclab.peisTemplate, MAX_PATH - 1);
            strncpy(eclabCfg.geisTemplate, config->eclab.geisTemplate, MAX_PATH - 1);

            result = BIO_ECLAB_Init(&eclabCfg);
            if (result != SUCCESS) {
                LogErrorEx(LOG_DEVICE_BIO, "Failed to initialize EC-Lab: %s",
                          ECLAB_GetErrorString(result));
                return result;
            }
            break;

        default:
            LogErrorEx(LOG_DEVICE_BIO, "Invalid control mode: %d", config->mode);
            return ERR_INVALID_PARAMETER;
    }

    g_abstractInitialized = true;

    LogMessageEx(LOG_DEVICE_BIO, "BioLogic abstraction layer initialized successfully");
    return SUCCESS;
}

void BIO_ShutdownAbstract(void) {
    if (!g_abstractInitialized) return;

    LogMessageEx(LOG_DEVICE_BIO, "Shutting down BioLogic abstraction layer");

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            if (g_abstractConfig.dll.deviceID > 0) {
                BIO_DisconnectQueued(g_abstractConfig.dll.deviceID, DEVICE_PRIORITY_HIGH);
            }
            if (g_abstractConfig.dll.queueMgr) {
                BIO_QueueShutdown(g_abstractConfig.dll.queueMgr);
                g_abstractConfig.dll.queueMgr = NULL;
            }
            break;

        case BIO_MODE_ECLAB_OLECOM:
            BIO_ECLAB_Shutdown();
            break;
    }

    memset(&g_abstractConfig, 0, sizeof(g_abstractConfig));
    g_abstractInitialized = false;

    LogMessageEx(LOG_DEVICE_BIO, "BioLogic abstraction layer shutdown complete");
}

bool BIO_IsAbstractInitialized(void) {
    return g_abstractInitialized;
}

BIO_ControlMode BIO_GetCurrentMode(void) {
    return g_abstractInitialized ? g_abstractConfig.mode : (BIO_ControlMode)-1;
}

int BIO_GetAbstractConfig(BIO_Config *config) {
    if (!config) return ERR_NULL_POINTER;
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    memcpy(config, &g_abstractConfig, sizeof(BIO_Config));
    return SUCCESS;
}

/******************************************************************************
 * Unified Technique Functions
 ******************************************************************************/

int BIO_Abstract_RunOCV(uint8_t channel,
                       double duration_s,
                       double sample_interval_s,
                       double record_every_dE,
                       double record_every_dT,
                       int e_range,
                       BIO_TechniqueData **result,
                       int timeout_ms,
                       BioTechniqueProgressCallback progressCallback,
                       void *userData,
                       volatile int *cancelled) {
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    LogDebugEx(LOG_DEVICE_BIO, "Running OCV via %s",
              BIO_GetModeName(g_abstractConfig.mode));

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            return BIO_RunOCVQueued(
                g_abstractConfig.dll.deviceID,
                channel,
                duration_s,
                sample_interval_s,
                record_every_dE,
                record_every_dT,
                e_range,
                true,  // processData
                result,
                timeout_ms,
                DEVICE_PRIORITY_NORMAL,
                progressCallback,
                userData,
                cancelled
            );

        case BIO_MODE_ECLAB_OLECOM:
            // In EC-Lab mode, parameters are ignored (come from .mps file)
            return BIO_ECLAB_RunOCV(
                NULL,  // Use template from config
                NULL,  // Auto-generate output filename
                result,
                timeout_ms,
                progressCallback,
                userData,
                cancelled
            );

        default:
            return ERR_INVALID_STATE;
    }
}

int BIO_Abstract_RunPEIS(uint8_t channel,
                        bool vs_initial,
                        double initial_voltage_step,
                        double duration_step,
                        double record_every_dT,
                        double record_every_dI,
                        double initial_freq,
                        double final_freq,
                        bool sweep_linear,
                        double amplitude_voltage,
                        int frequency_number,
                        int average_n_times,
                        bool correction,
                        double wait_for_steady,
                        BIO_TechniqueData **result,
                        int timeout_ms,
                        BioTechniqueProgressCallback progressCallback,
                        void *userData,
                        volatile int *cancelled) {
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    LogDebugEx(LOG_DEVICE_BIO, "Running PEIS via %s",
              BIO_GetModeName(g_abstractConfig.mode));

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            return BIO_RunPEISQueued(
                g_abstractConfig.dll.deviceID,
                channel,
                vs_initial,
                initial_voltage_step,
                duration_step,
                record_every_dT,
                record_every_dI,
                initial_freq,
                final_freq,
                sweep_linear,
                amplitude_voltage,
                frequency_number,
                average_n_times,
                correction,
                wait_for_steady,
                true,  // processData
                result,
                timeout_ms,
                DEVICE_PRIORITY_NORMAL,
                progressCallback,
                userData,
                cancelled
            );

        case BIO_MODE_ECLAB_OLECOM:
            return BIO_ECLAB_RunPEIS(
                NULL,  // Use template from config
                NULL,  // Auto-generate output filename
                result,
                timeout_ms,
                progressCallback,
                userData,
                cancelled
            );

        default:
            return ERR_INVALID_STATE;
    }
}

int BIO_Abstract_RunGEIS(uint8_t channel,
                        bool vs_initial,
                        double initial_current_step,
                        double duration_step,
                        double record_every_dT,
                        double record_every_dE,
                        double initial_freq,
                        double final_freq,
                        bool sweep_linear,
                        double amplitude_current,
                        int frequency_number,
                        int average_n_times,
                        bool correction,
                        double wait_for_steady,
                        int i_range,
                        BIO_TechniqueData **result,
                        int timeout_ms,
                        BioTechniqueProgressCallback progressCallback,
                        void *userData,
                        volatile int *cancelled) {
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    LogDebugEx(LOG_DEVICE_BIO, "Running GEIS via %s",
              BIO_GetModeName(g_abstractConfig.mode));

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            return BIO_RunGEISQueued(
                g_abstractConfig.dll.deviceID,
                channel,
                vs_initial,
                initial_current_step,
                duration_step,
                record_every_dT,
                record_every_dE,
                initial_freq,
                final_freq,
                sweep_linear,
                amplitude_current,
                frequency_number,
                average_n_times,
                correction,
                wait_for_steady,
                i_range,
                true,  // processData
                result,
                timeout_ms,
                DEVICE_PRIORITY_NORMAL,
                progressCallback,
                userData,
                cancelled
            );

        case BIO_MODE_ECLAB_OLECOM:
            return BIO_ECLAB_RunGEIS(
                NULL,  // Use template from config
                NULL,  // Auto-generate output filename
                result,
                timeout_ms,
                progressCallback,
                userData,
                cancelled
            );

        default:
            return ERR_INVALID_STATE;
    }
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

int BIO_Abstract_GetDeviceID(void) {
    if (!g_abstractInitialized) return -1;

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            return g_abstractConfig.dll.deviceID;

        case BIO_MODE_ECLAB_OLECOM:
            return BIO_ECLAB_GetDeviceID();

        default:
            return -1;
    }
}

int BIO_Abstract_Connect(void) {
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            // For Direct DLL mode, reconnect using stored configuration
            {
                TDeviceInfos_t deviceInfo;
                int result = BIO_ConnectQueued(g_abstractConfig.dll.deviceAddress,
                                              g_abstractConfig.dll.timeout,
                                              &g_abstractConfig.dll.deviceID,
                                              &deviceInfo,
                                              DEVICE_PRIORITY_NORMAL);
                return result;
            }

        case BIO_MODE_ECLAB_OLECOM:
            // For EC-Lab mode, use simple reconnect wrapper
            return BIO_ECLAB_Connect();

        default:
            return ERR_INVALID_STATE;
    }
}

int BIO_Abstract_TestConnection(void) {
    if (!g_abstractInitialized) return ERR_NOT_INITIALIZED;

    switch (g_abstractConfig.mode) {
        case BIO_MODE_DIRECT_DLL:
            return BIO_TestConnectionQueued(g_abstractConfig.dll.deviceID,
                                           DEVICE_PRIORITY_NORMAL);

        case BIO_MODE_ECLAB_OLECOM:
            return BIO_ECLAB_TestConnection();

        default:
            return ERR_INVALID_STATE;
    }
}

const char* BIO_GetModeName(BIO_ControlMode mode) {
    switch (mode) {
        case BIO_MODE_DIRECT_DLL: return "Direct DLL";
        case BIO_MODE_ECLAB_OLECOM: return "EC-Lab OLE COM";
        default: return "Unknown";
    }
}
