/******************************************************************************
 * biologic_eclab.h
 *
 * High-level EC-Lab technique interface
 *
 * This module provides a BioLogic-compatible API that uses EC-Lab OLE COM
 * automation as the backend instead of direct DLL calls. Techniques are
 * configured using pre-created .mps template files from the EC-Lab GUI.
 *
 * Key differences from direct DLL mode:
 * - All technique parameters are defined in .mps files
 * - Parameter arguments are ignored (settings come from file)
 * - Data is retrieved from .mpr files after measurement
 * - Polling-based status monitoring (no hardware callbacks)
 *
 * Usage:
 * 1. Create .mps template files in EC-Lab GUI for each technique
 * 2. Call BIO_ECLAB_Init() with paths to templates and data directory
 * 3. Call technique functions (parameters are ignored)
 * 4. Results are returned as BIO_TechniqueData for compatibility
 ******************************************************************************/

#ifndef BIOLOGIC_ECLAB_H
#define BIOLOGIC_ECLAB_H

#include "biologic_dll.h"
#include "biologic_queue.h"
#include "eclab_olecom.h"

/******************************************************************************
 * Configuration
 ******************************************************************************/

// Configuration for EC-Lab mode
typedef struct {
    char settingsDir[MAX_PATH];      // Directory containing .mps template files
    char dataDir[MAX_PATH];          // Directory for output .mpr files
    int deviceNumber;                // EC-Lab device number (0-based)
    int channelNumber;               // EC-Lab channel number (0-based)

    // Template filenames (within settingsDir)
    char ocvTemplate[MAX_PATH];      // OCV .mps template
    char peisTemplate[MAX_PATH];     // PEIS .mps template
    char geisTemplate[MAX_PATH];     // GEIS .mps template

    // Connection handle (managed internally)
    ECLabConnection *conn;

    // Device ID (for compatibility with existing API)
    int deviceID;
} ECLAB_Config;

/******************************************************************************
 * Initialization and Shutdown
 ******************************************************************************/

/**
 * Initialize EC-Lab backend
 *
 * Establishes OLE COM connection to EC-Lab and connects to the device.
 * EC-Lab must be running and registered as an OLE COM server.
 *
 * @param config Configuration structure with paths and device info
 * @return SUCCESS or error code
 */
int BIO_ECLAB_Init(const ECLAB_Config *config);

/**
 * Shutdown EC-Lab backend
 *
 * Disconnects from device and releases OLE COM resources.
 */
void BIO_ECLAB_Shutdown(void);

/**
 * Check if EC-Lab backend is initialized
 *
 * @return true if initialized, false otherwise
 */
bool BIO_ECLAB_IsInitialized(void);

/**
 * Get current configuration
 *
 * @param config Pointer to receive configuration copy
 * @return SUCCESS or error code
 */
int BIO_ECLAB_GetConfig(ECLAB_Config *config);

/******************************************************************************
 * Technique Functions
 *
 * These functions mirror the existing BioLogic API but use EC-Lab backend.
 *
 * IMPORTANT: Technique parameters are IGNORED. All settings come from the
 * .mps template files. Parameters are kept for API compatibility only.
 *
 * Workflow:
 * 1. Load appropriate .mps template file
 * 2. Start measurement (data written to .mpr file)
 * 3. Monitor status via polling
 * 4. When complete, read .mpr file and convert to BIO_TechniqueData
 * 5. Return data to caller
 ******************************************************************************/

/**
 * Run OCV measurement using EC-Lab
 *
 * Uses the OCV template specified in BIO_ECLAB_Init().
 * All technique parameters except timeout are ignored.
 *
 * @param mpsFilePath    Path to OCV .mps file (NULL = use template from config)
 * @param outputMprPath  Path for output .mpr file (NULL = auto-generate)
 * @param result         Pointer to receive technique data (must be freed)
 * @param timeout_ms     Maximum time to wait for completion
 * @param progressCallback Optional progress callback
 * @param userData       User data for callback
 * @param cancelled      Pointer to cancellation flag (optional)
 * @return SUCCESS or error code
 */
int BIO_ECLAB_RunOCV(const char *mpsFilePath,
                     const char *outputMprPath,
                     BIO_TechniqueData **result,
                     int timeout_ms,
                     BioTechniqueProgressCallback progressCallback,
                     void *userData,
                     volatile int *cancelled);

/**
 * Run PEIS measurement using EC-Lab
 *
 * Uses the PEIS template specified in BIO_ECLAB_Init().
 * All technique parameters except timeout are ignored.
 *
 * @param mpsFilePath    Path to PEIS .mps file (NULL = use template from config)
 * @param outputMprPath  Path for output .mpr file (NULL = auto-generate)
 * @param result         Pointer to receive technique data (must be freed)
 * @param timeout_ms     Maximum time to wait for completion
 * @param progressCallback Optional progress callback
 * @param userData       User data for callback
 * @param cancelled      Pointer to cancellation flag (optional)
 * @return SUCCESS or error code
 */
int BIO_ECLAB_RunPEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled);

/**
 * Run GEIS measurement using EC-Lab
 *
 * Uses the GEIS template specified in BIO_ECLAB_Init().
 * All technique parameters except timeout are ignored.
 *
 * @param mpsFilePath    Path to GEIS .mps file (NULL = use template from config)
 * @param outputMprPath  Path for output .mpr file (NULL = auto-generate)
 * @param result         Pointer to receive technique data (must be freed)
 * @param timeout_ms     Maximum time to wait for completion
 * @param progressCallback Optional progress callback
 * @param userData       User data for callback
 * @param cancelled      Pointer to cancellation flag (optional)
 * @return SUCCESS or error code
 */
int BIO_ECLAB_RunGEIS(const char *mpsFilePath,
                      const char *outputMprPath,
                      BIO_TechniqueData **result,
                      int timeout_ms,
                      BioTechniqueProgressCallback progressCallback,
                      void *userData,
                      volatile int *cancelled);

/******************************************************************************
 * Data Conversion Functions
 ******************************************************************************/

/**
 * Convert .mpr file to BIO_TechniqueData structure
 *
 * Reads data from an EC-Lab .mpr file and converts it to the standard
 * BIO_TechniqueData format for compatibility with existing code.
 *
 * @param mprPath Full path to .mpr file
 * @param type    Technique type (OCV, PEIS, GEIS, etc.)
 * @param data    Pointer to receive allocated data (must be freed with BIO_FreeTechniqueData)
 * @return SUCCESS or error code
 */
int BIO_ECLAB_ConvertMprToTechniqueData(const char *mprPath,
                                        BioTechniqueType type,
                                        BIO_TechniqueData **data);

/**
 * Generate unique .mpr filename
 *
 * Creates a unique filename in the data directory based on technique type
 * and timestamp.
 *
 * @param type      Technique type
 * @param filename  Buffer to receive filename (MAX_PATH size)
 * @return SUCCESS or error code
 */
int BIO_ECLAB_GenerateMprFilename(BioTechniqueType type, char *filename);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get EC-Lab connection handle
 *
 * Returns the internal ECLabConnection for advanced operations.
 * Handle is managed by BIO_ECLAB module - do not free.
 *
 * @return Connection handle or NULL if not initialized
 */
ECLabConnection* BIO_ECLAB_GetConnection(void);

/**
 * Get device ID
 *
 * Returns the device ID for compatibility with existing code.
 *
 * @return Device ID or -1 if not initialized
 */
int BIO_ECLAB_GetDeviceID(void);

/**
 * Connect to EC-Lab device
 *
 * Connects to the configured device. Can be used to reconnect after
 * a disconnection.
 *
 * @return SUCCESS or error code
 */
int BIO_ECLAB_Connect(void);

/**
 * Disconnect from EC-Lab device
 *
 * Disconnects from the device. Useful for clearing device fault states
 * after hardware events like relay switching.
 *
 * @return SUCCESS or error code
 */
int BIO_ECLAB_Disconnect(void);

/**
 * Force reconnect to EC-Lab device
 *
 * Forces a reconnection by resetting internal state and reconnecting.
 * Use this when EC-Lab has detected a hardware disconnection (e.g., relay
 * switching) and marked the device as "not connected", but the device is
 * now physically reconnected. This bypasses the normal disconnect/connect
 * sequence which may fail in this state.
 *
 * @return SUCCESS or error code
 */
int BIO_ECLAB_ForceReconnect(void);

/**
 * Test EC-Lab connection
 *
 * Verifies that EC-Lab is still running and device is connected.
 *
 * @return SUCCESS if connected, error code otherwise
 */
int BIO_ECLAB_TestConnection(void);

/**
 * Stop running measurement
 *
 * Stops the currently running measurement on the configured channel.
 *
 * @return SUCCESS or error code
 */
int BIO_ECLAB_StopMeasurement(void);

#endif // BIOLOGIC_ECLAB_H
