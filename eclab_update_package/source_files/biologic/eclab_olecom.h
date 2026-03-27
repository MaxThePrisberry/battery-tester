/******************************************************************************
 * eclab_olecom.h
 *
 * Low-level OLE COM wrapper for EC-Lab automation
 *
 * This module provides a C interface to EC-Lab's OLE COM automation API.
 * It uses EC-Lab's CUSTOM COM INTERFACE (IEClabExe), NOT IDispatch!
 *
 * IMPORTANT: EC-Lab does not support IDispatch automation. You must use
 * the custom IEClabExe interface with direct vtable calls.
 *
 * EC-Lab must be installed and registered as an OLE COM server before use:
 *   C:\> cd "C:\Program Files (x86)\EC-Lab"
 *   C:\> ECLab.exe /regserver
 *
 * Reference: Bio-Logic EC-Lab OLE COM User Manual
 ******************************************************************************/

#ifndef ECLAB_OLECOM_H
#define ECLAB_OLECOM_H

#include <windows.h>
#include <stdbool.h>
#include <oaidl.h>
#include <oleauto.h>
#include "common.h"
#include "eclab_olecom_interface.h"  // Custom IEClabExe interface

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Status codes from EC-Lab MeasureStatus (index 0)
#define ECLAB_STATUS_STOP       0
#define ECLAB_STATUS_RUN        1
#define ECLAB_STATUS_PAUSE      2
#define ECLAB_STATUS_SYNC       3
#define ECLAB_STATUS_STOP_REC1  4
#define ECLAB_STATUS_STOP_REC2  5
#define ECLAB_STATUS_PAUSE_REC  6

// Technique codes (from manual annex 4.1)
#define ECLAB_TECH_OCV          11   // Open Circuit Voltage
#define ECLAB_TECH_PEIS         29   // Potentio EIS
#define ECLAB_TECH_GEIS         30   // Galvano EIS

// Variable codes for MeasureValueByCode (from manual annex 4.4)
#define ECLAB_VAR_TIME          4    // time/s
#define ECLAB_VAR_EWE           6    // Ewe/V
#define ECLAB_VAR_I             8    // I/mA
#define ECLAB_VAR_FREQ          32   // freq/Hz
#define ECLAB_VAR_Z_MAG         36   // |Z|/Ohm
#define ECLAB_VAR_Z_REAL        37   // Re(Z)/Ohm
#define ECLAB_VAR_Z_IMAG        38   // -Im(Z)/Ohm

// Safety limit codes (status array index 30)
#define ECLAB_SAFETY_OK         0
#define ECLAB_SAFETY_EMAX       1
#define ECLAB_SAFETY_EMIN       2
#define ECLAB_SAFETY_I          3
#define ECLAB_SAFETY_Q          4

// Connection status (status array index 31)
#define ECLAB_CONN_OK           0
#define ECLAB_CONN_DISCONNECTED 1

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// EC-Lab connection handle
typedef struct {
    IEClabExe *pInterface;       // Custom IEClabExe interface pointer
    int deviceNumber;            // EC-Lab device index (0-based)
    int channelNumber;           // EC-Lab channel index (0-based)
    bool isConnected;            // Connection state
    char workingDir[MAX_PATH];   // Directory for data files
    CLSID clsid;                 // COM class ID for EC-Lab
} ECLabConnection;

// Complete status from MeasureStatus (32 values from manual section 3.2.9)
typedef struct {
    // Basic status
    int status;                  // [0] 0=Stop, 1=Run, 2=Pause, 3=Sync, etc.
    int oxRed;                   // [1] 0=Oxidation, 1=Reduction
    int ocv;                     // [2] 0=OCV, 1=Other
    int eis;                     // [3] 0=EIS, 1=No EIS

    // Technique info
    int techniqueNumber;         // [4] Index 0-19
    int techniqueCode;           // [5] See annex 4.1 (11=OCV, 29=PEIS, 30=GEIS)
    int sequenceNumber;          // [6] Current sequence

    // Loop control
    int currentLoopIteration;    // [7] Current loop iteration
    int currentSequenceInLoop;   // [8] Current sequence within loop
    int loopExperimentIteration; // [9] Loop experiment iteration
    int cycleNumber;             // [10] For CV, EIS, VASP, CASP

    // Counters
    int counter1;                // [11] For CV, ECN, SPFC, PR
    int counter2;                // [12] For CV, ECN, SPFC, PR
    int counter3;                // [13] For CV, ECN, SPFC, PR

    // Data
    int bufferSize;              // [14] Buffer size
    double time;                 // [15] Elapsed time (seconds)
    double ewe;                  // [16] Working electrode voltage (V)
    double ece;                  // [17] Counter electrode voltage (V)
    double eoc;                  // [18] Open circuit voltage (V)
    double current;              // [19] Current (A)
    double charge;               // [20] Q-Q0 charge (A.h)
    double aux1;                 // [21] Auxiliary input 1
    double aux2;                 // [22] Auxiliary input 2
    double iRange;               // [23] Current range (A)
    double rCompensation;        // [24] R Compensation (Ohm)

    // EIS specific
    double frequency;            // [25] Frequency (Hz)
    double zMagnitude;           // [26] |Z| impedance magnitude (Ohm)

    // Point tracking
    int currentPointIndex;       // [27] Current point index
    int totalPointIndex;         // [28] Total point index

    // System status
    double temperature;          // [29] Temperature (°C)
    int safetyLimit;             // [30] 0=OK, 1=Emax, 2=Emin, 3=I, 4=Q-Q0, 7-10=Stack
    int connection;              // [31] 0=OK, 1=Disconnected
} ECLAB_Status;

// Data value structure for MeasureValueByCode results
typedef struct {
    double value;                // The requested value
    int varCode;                 // Variable code that was queried
    int dataIndex;               // Data point index
} ECLAB_DataValue;

// DC measurement data point (from MeasureDcValue)
typedef struct {
    double time;                 // Time (s)
    double voltage;              // Voltage (V)
    double current;              // Current (A)
} ECLAB_DcPoint;

// EIS measurement data point (from MeasureEisValue)
typedef struct {
    double time;                 // Time (s)
    double frequency;            // Frequency (Hz)
    double zReal;                // Real impedance (Ohm)
    double zImag;                // Imaginary impedance (Ohm)
} ECLAB_EisPoint;

/******************************************************************************
 * Error Codes
 *
 * These are in addition to standard Windows HRESULT codes and BioLogic
 * device error codes. They use the BIOLOGIC error base for compatibility.
 ******************************************************************************/

#define ECLAB_ERR_COM_INIT_FAILED         (ERR_BASE_BIOLOGIC - 100)
#define ECLAB_ERR_COM_CREATE_FAILED       (ERR_BASE_BIOLOGIC - 101)
#define ECLAB_ERR_COM_INVOKE_FAILED       (ERR_BASE_BIOLOGIC - 102)
#define ECLAB_ERR_COM_RELEASE_FAILED      (ERR_BASE_BIOLOGIC - 103)
#define ECLAB_ERR_BSTR_CONVERSION         (ERR_BASE_BIOLOGIC - 104)
#define ECLAB_ERR_VARIANT_TYPE            (ERR_BASE_BIOLOGIC - 105)
#define ECLAB_ERR_INVALID_CONNECTION      (ERR_BASE_BIOLOGIC - 106)
#define ECLAB_ERR_NOT_CONNECTED           (ERR_BASE_BIOLOGIC - 107)
#define ECLAB_ERR_ALREADY_CONNECTED       (ERR_BASE_BIOLOGIC - 108)
#define ECLAB_ERR_DEVICE_NOT_FOUND        (ERR_BASE_BIOLOGIC - 109)
#define ECLAB_ERR_CHANNEL_NOT_FOUND       (ERR_BASE_BIOLOGIC - 110)
#define ECLAB_ERR_FILE_NOT_FOUND          (ERR_BASE_BIOLOGIC - 111)
#define ECLAB_ERR_INVALID_MPS_FILE        (ERR_BASE_BIOLOGIC - 112)
#define ECLAB_ERR_INVALID_MPR_FILE        (ERR_BASE_BIOLOGIC - 113)
#define ECLAB_ERR_RUN_FAILED              (ERR_BASE_BIOLOGIC - 114)
#define ECLAB_ERR_STATUS_ARRAY_SIZE       (ERR_BASE_BIOLOGIC - 115)
#define ECLAB_ERR_DATA_NOT_AVAILABLE      (ERR_BASE_BIOLOGIC - 116)
#define ECLAB_ERR_REGISTRATION_FAILED     (ERR_BASE_BIOLOGIC - 117)

/******************************************************************************
 * Initialization and Cleanup Functions
 ******************************************************************************/

/**
 * Initialize EC-Lab OLE COM connection
 *
 * Creates an instance of the EC-Lab COM object and initializes the connection
 * structure. EC-Lab must already be registered as an OLE COM server.
 *
 * @param conn          Pointer to receive allocated connection handle
 * @param workingDir    Directory for temporary and data files (can be NULL for default)
 * @return SUCCESS or error code
 */
int ECLAB_Initialize(ECLabConnection **conn, const char *workingDir);

/**
 * Shutdown EC-Lab OLE COM connection
 *
 * Releases all COM resources and frees the connection structure.
 *
 * @param conn Connection handle to shutdown
 * @return SUCCESS or error code
 */
int ECLAB_Shutdown(ECLabConnection *conn);

/**
 * Register EC-Lab as OLE COM server
 *
 * Executes EC-Lab with /regserver parameter to register it for COM automation.
 * Requires administrator privileges. This is typically a one-time setup step.
 *
 * @param eclabPath Full path to ECLab.exe
 * @return SUCCESS or error code
 */
int ECLAB_RegisterServer(const char *eclabPath);

/******************************************************************************
 * Device Management Functions
 ******************************************************************************/

/**
 * Connect to EC-Lab device
 *
 * Establishes connection to a specific BioLogic device through EC-Lab.
 *
 * @param conn          Connection handle
 * @param deviceNumber  EC-Lab device index (0-based)
 * @return SUCCESS or error code
 */
int ECLAB_ConnectDevice(ECLabConnection *conn, int deviceNumber);

/**
 * Disconnect from EC-Lab device
 *
 * Closes connection to the device.
 *
 * @param conn Connection handle
 * @return SUCCESS or error code
 */
int ECLAB_DisconnectDevice(ECLabConnection *conn);

/**
 * Force reconnect to EC-Lab device
 *
 * Forces a reconnection to the device by resetting the internal connection
 * state and calling ConnectDevice. This is used when EC-Lab has detected a
 * hardware disconnection (e.g., relay switching) and marked the device as
 * "not connected" internally, but the device is now physically reconnected.
 *
 * This bypasses the normal disconnect/connect sequence which may fail when
 * EC-Lab already considers the device disconnected but the internal flag
 * still shows connected.
 *
 * @param conn          Connection handle
 * @param deviceNumber  EC-Lab device index (0-based)
 * @return SUCCESS or error code
 */
int ECLAB_ForceReconnect(ECLabConnection *conn, int deviceNumber);

/**
 * Test connection to device
 *
 * Verifies that the device is still connected and responding.
 *
 * @param conn Connection handle
 * @return SUCCESS if connected, error code otherwise
 */
int ECLAB_TestConnection(ECLabConnection *conn);

/******************************************************************************
 * Experiment Control Functions
 *
 * These functions work with PRE-CONFIGURED .mps settings files created
 * in the EC-Lab GUI. Settings files define all technique parameters.
 ******************************************************************************/

/**
 * Load settings from .mps file
 *
 * Loads a pre-configured .mps settings file created in EC-Lab GUI.
 * All technique parameters are defined in this file.
 *
 * @param conn        Connection handle
 * @param device      Device number (same as deviceNumber in connect)
 * @param channel     Channel number (0-based)
 * @param mpsFilePath Full path to .mps settings file
 * @return SUCCESS or error code
 */
int ECLAB_LoadSettings(ECLabConnection *conn, int device, int channel,
                      const char *mpsFilePath);

/**
 * Start measurement on channel
 *
 * Starts the experiment defined by the loaded settings file.
 * Data will be saved to the specified output .mpr file.
 *
 * @param conn          Connection handle
 * @param device        Device number
 * @param channel       Channel number
 * @param outputMprPath Full path for output .mpr data file
 * @return SUCCESS or error code
 */
int ECLAB_RunChannel(ECLabConnection *conn, int device, int channel,
                    const char *outputMprPath);

/**
 * Stop measurement on channel
 *
 * Stops a running measurement.
 *
 * @param conn    Connection handle
 * @param device  Device number
 * @param channel Channel number
 * @return SUCCESS or error code
 */
int ECLAB_StopChannel(ECLabConnection *conn, int device, int channel);

/******************************************************************************
 * Status Monitoring Functions
 ******************************************************************************/

/**
 * Get current measurement status
 *
 * Retrieves complete status information from a running measurement.
 * This includes the 32-value status array documented in the EC-Lab manual.
 *
 * @param conn    Connection handle
 * @param device  Device number
 * @param channel Channel number
 * @param status  Pointer to receive status structure
 * @return SUCCESS or error code
 */
int ECLAB_MeasureStatus(ECLabConnection *conn, int device, int channel,
                       ECLAB_Status *status);

/******************************************************************************
 * Data Retrieval Functions
 *
 * These functions read data from completed .mpr files
 ******************************************************************************/

/**
 * Get number of data points in .mpr file
 *
 * @param mprPath   Full path to .mpr file
 * @param numPoints Pointer to receive point count
 * @return SUCCESS or error code
 */
int ECLAB_MeasureNumberOfPoints(const char *mprPath, int *numPoints);

/**
 * Read DC measurement data point
 *
 * Retrieves time, voltage, and current for a specific data point.
 *
 * @param mprPath   Full path to .mpr file
 * @param dataIndex Data point index (0-based)
 * @param time      Pointer to receive time (s)
 * @param voltage   Pointer to receive voltage (V)
 * @param current   Pointer to receive current (A)
 * @return SUCCESS or error code
 */
int ECLAB_MeasureDcValue(const char *mprPath, int dataIndex,
                        double *time, double *voltage, double *current);

/**
 * Read EIS measurement data point
 *
 * Retrieves time, frequency, and impedance for a specific data point.
 *
 * @param mprPath   Full path to .mpr file
 * @param dataIndex Data point index (0-based)
 * @param time      Pointer to receive time (s)
 * @param freq      Pointer to receive frequency (Hz)
 * @param zReal     Pointer to receive real impedance (Ohm)
 * @param zImag     Pointer to receive imaginary impedance (Ohm)
 * @return SUCCESS or error code
 */
int ECLAB_MeasureEisValue(const char *mprPath, int dataIndex,
                         double *time, double *freq, double *zReal, double *zImag);

/**
 * Read specific variable by code
 *
 * Generic function to retrieve any variable from .mpr file using variable codes
 * defined in the EC-Lab manual (see ECLAB_VAR_* constants).
 *
 * @param mprPath   Full path to .mpr file
 * @param varCode   Variable code (see manual annex 4.4)
 * @param dataIndex Data point index (0-based)
 * @param value     Pointer to receive value
 * @return SUCCESS or error code
 */
int ECLAB_MeasureValueByCode(const char *mprPath, int varCode, int dataIndex,
                            double *value);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get error string for EC-Lab error code
 *
 * @param errorCode Error code
 * @return Human-readable error string
 */
const char* ECLAB_GetErrorString(int errorCode);

/**
 * Check if EC-Lab is running
 *
 * @return true if EC-Lab process is running, false otherwise
 */
bool ECLAB_IsRunning(void);

/**
 * Enable or disable EC-Lab message windows
 *
 * Controls whether EC-Lab displays popup message dialogs.
 * Useful for automated operation.
 *
 * @param conn   Connection handle
 * @param enable true to enable popups, false to disable
 * @return SUCCESS or error code
 */
int ECLAB_EnableMessagesWindows(ECLabConnection *conn, bool enable);

#endif // ECLAB_OLECOM_H
