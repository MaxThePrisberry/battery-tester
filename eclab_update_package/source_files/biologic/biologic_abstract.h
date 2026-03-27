/******************************************************************************
 * biologic_abstract.h
 *
 * Unified BioLogic abstraction layer
 *
 * This module provides a single interface that can dispatch to either:
 * - Direct DLL mode (biologic_dll + biologic_queue)
 * - EC-Lab OLE COM mode (biologic_eclab)
 *
 * The abstraction layer allows experiments and high-level code to remain
 * unchanged regardless of which backend is used. Selection is made at
 * initialization time via configuration.
 *
 * Usage:
 * 1. Configure BIO_Config with desired mode and settings
 * 2. Call BIO_InitializeAbstract()
 * 3. Use BIO_Abstract_* functions (same API as before)
 * 4. Call BIO_ShutdownAbstract() when done
 *
 * Transparent backend switching - experiments don't need to know which mode
 * is being used.
 ******************************************************************************/

#ifndef BIOLOGIC_ABSTRACT_H
#define BIOLOGIC_ABSTRACT_H

#include "biologic_dll.h"
#include "biologic_queue.h"
#include "biologic_eclab.h"

/******************************************************************************
 * Configuration
 ******************************************************************************/

// Control mode selection
typedef enum {
    BIO_MODE_DIRECT_DLL = 0,      // Direct hardware control via ECLib.dll
    BIO_MODE_ECLAB_OLECOM = 1     // Software control via EC-Lab OLE COM
} BIO_ControlMode;

// Unified configuration structure
typedef struct {
    BIO_ControlMode mode;

    // Direct DLL mode settings
    struct {
        char deviceAddress[64];    // e.g., "USB0"
        uint8_t timeout;           // Connection timeout (seconds)
        int deviceID;              // Device ID after connection
        BioQueueManager *queueMgr; // Queue manager handle
    } dll;

    // EC-Lab OLE COM mode settings
    struct {
        char settingsDir[MAX_PATH];  // Directory with .mps templates
        char dataDir[MAX_PATH];      // Directory for output .mpr files
        int deviceNumber;            // EC-Lab device index
        int channelNumber;           // EC-Lab channel index

        // Template filenames
        char ocvTemplate[MAX_PATH];
        char peisTemplate[MAX_PATH];
        char geisTemplate[MAX_PATH];
    } eclab;

} BIO_Config;

/******************************************************************************
 * Initialization and Shutdown
 ******************************************************************************/

/**
 * Initialize BioLogic abstraction layer
 *
 * Initializes the selected backend based on config->mode.
 *
 * @param config Configuration specifying mode and backend settings
 * @return SUCCESS or error code
 */
int BIO_InitializeAbstract(const BIO_Config *config);

/**
 * Shutdown BioLogic abstraction layer
 *
 * Shuts down the active backend and releases resources.
 */
void BIO_ShutdownAbstract(void);

/**
 * Check if abstraction layer is initialized
 *
 * @return true if initialized, false otherwise
 */
bool BIO_IsAbstractInitialized(void);

/**
 * Get current control mode
 *
 * @return Current mode or -1 if not initialized
 */
BIO_ControlMode BIO_GetCurrentMode(void);

/**
 * Get current configuration
 *
 * @param config Pointer to receive configuration copy
 * @return SUCCESS or error code
 */
int BIO_GetAbstractConfig(BIO_Config *config);

/******************************************************************************
 * Unified Technique Functions
 *
 * These functions provide a single API that works with both backends.
 *
 * For Direct DLL mode:
 * - All parameters are used as specified
 * - Direct hardware control
 * - Maximum performance
 *
 * For EC-Lab OLE COM mode:
 * - Technique parameters are IGNORED (come from .mps file)
 * - Software-mediated control
 * - GUI validation and monitoring available
 *
 * The return value and result structure are identical regardless of mode.
 ******************************************************************************/

/**
 * Run OCV measurement (unified)
 *
 * @param channel          Channel number (0-based)
 * @param duration_s       Duration in seconds (ignored in EC-Lab mode)
 * @param sample_interval_s Sample interval (ignored in EC-Lab mode)
 * @param record_every_dE   Record threshold voltage (ignored in EC-Lab mode)
 * @param record_every_dT   Record threshold time (ignored in EC-Lab mode)
 * @param e_range          Voltage range (ignored in EC-Lab mode)
 * @param result           Pointer to receive technique data
 * @param timeout_ms       Maximum time to wait
 * @param progressCallback Optional progress callback
 * @param userData         User data for callback
 * @param cancelled        Cancellation flag (optional)
 * @return SUCCESS or error code
 */
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
                       volatile int *cancelled);

/**
 * Run PEIS measurement (unified)
 *
 * @param channel               Channel number
 * @param vs_initial            Voltage step vs initial (ignored in EC-Lab mode)
 * @param initial_voltage_step  Initial voltage (ignored in EC-Lab mode)
 * @param duration_step         Step duration (ignored in EC-Lab mode)
 * @param record_every_dT       Record threshold (ignored in EC-Lab mode)
 * @param record_every_dI       Record threshold (ignored in EC-Lab mode)
 * @param initial_freq          Initial frequency (ignored in EC-Lab mode)
 * @param final_freq            Final frequency (ignored in EC-Lab mode)
 * @param sweep_linear          Linear vs log sweep (ignored in EC-Lab mode)
 * @param amplitude_voltage     Sine amplitude (ignored in EC-Lab mode)
 * @param frequency_number      Number of frequencies (ignored in EC-Lab mode)
 * @param average_n_times       Averaging (ignored in EC-Lab mode)
 * @param correction            Non-stationary correction (ignored in EC-Lab mode)
 * @param wait_for_steady       Wait periods (ignored in EC-Lab mode)
 * @param result                Pointer to receive technique data
 * @param timeout_ms            Maximum time to wait
 * @param progressCallback      Optional progress callback
 * @param userData              User data for callback
 * @param cancelled             Cancellation flag (optional)
 * @return SUCCESS or error code
 */
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
                        volatile int *cancelled);

/**
 * Run GEIS measurement (unified)
 *
 * @param channel               Channel number
 * @param vs_initial            Current step vs initial (ignored in EC-Lab mode)
 * @param initial_current_step  Initial current (ignored in EC-Lab mode)
 * @param duration_step         Step duration (ignored in EC-Lab mode)
 * @param record_every_dT       Record threshold (ignored in EC-Lab mode)
 * @param record_every_dE       Record threshold (ignored in EC-Lab mode)
 * @param initial_freq          Initial frequency (ignored in EC-Lab mode)
 * @param final_freq            Final frequency (ignored in EC-Lab mode)
 * @param sweep_linear          Linear vs log sweep (ignored in EC-Lab mode)
 * @param amplitude_current     Sine amplitude (ignored in EC-Lab mode)
 * @param frequency_number      Number of frequencies (ignored in EC-Lab mode)
 * @param average_n_times       Averaging (ignored in EC-Lab mode)
 * @param correction            Non-stationary correction (ignored in EC-Lab mode)
 * @param wait_for_steady       Wait periods (ignored in EC-Lab mode)
 * @param i_range               Current range (ignored in EC-Lab mode)
 * @param result                Pointer to receive technique data
 * @param timeout_ms            Maximum time to wait
 * @param progressCallback      Optional progress callback
 * @param userData              User data for callback
 * @param cancelled             Cancellation flag (optional)
 * @return SUCCESS or error code
 */
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
                        volatile int *cancelled);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get device ID
 *
 * Returns device ID appropriate for current mode.
 *
 * @return Device ID or -1 if not initialized
 */
int BIO_Abstract_GetDeviceID(void);

/**
 * Connect to BioLogic device
 *
 * Connects to the device. Can be used to reconnect after a disconnection.
 * Works in both Direct DLL and EC-Lab modes.
 *
 * @return SUCCESS or error code
 */
int BIO_Abstract_Connect(void);

/**
 * Test connection
 *
 * Verifies connection to device in current mode.
 *
 * @return SUCCESS if connected, error code otherwise
 */
int BIO_Abstract_TestConnection(void);

/**
 * Get mode name as string
 *
 * @param mode Control mode
 * @return Human-readable mode name
 */
const char* BIO_GetModeName(BIO_ControlMode mode);

#endif // BIOLOGIC_ABSTRACT_H
