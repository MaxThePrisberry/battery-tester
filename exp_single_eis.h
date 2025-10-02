/******************************************************************************
 * exp_single_eis.h
 * 
 * Single EIS Measurement Module
 * Performs a single EIS measurement (OCV + GEIS) at current battery state
 ******************************************************************************/

#ifndef EXP_SINGLE_EIS_H
#define EXP_SINGLE_EIS_H

#include "common.h"
#include "psb10000_dll.h"
#include "psb10000_queue.h"
#include "biologic_dll.h"
#include "biologic_queue.h"
#include "teensy_dll.h"
#include "teensy_queue.h"
#include "dtb4848_dll.h"
#include "dtb4848_queue.h"
#include "cdaq_utils.h"

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// EIS Retry and Timeout
#define SINGLE_EIS_MAX_RETRY          2       // Retry failed measurements twice
#define SINGLE_EIS_RETRY_DELAY        5.0     // Seconds to wait between retries

// File System Structure
#define SINGLE_EIS_DATA_DIR           "data"
#define SINGLE_EIS_RESULTS_FILE       "eis_measurement.txt"
#define SINGLE_EIS_SETTINGS_FILE      "measurement_settings.ini"

/******************************************************************************
 * Type Definitions
 ******************************************************************************/

// Measurement state
typedef enum {
    SINGLE_EIS_STATE_IDLE = 0,
    SINGLE_EIS_STATE_PREPARING,
    SINGLE_EIS_STATE_MEASURING_OCV,
    SINGLE_EIS_STATE_MEASURING_GEIS,
    SINGLE_EIS_STATE_COMPLETED,
    SINGLE_EIS_STATE_ERROR,
    SINGLE_EIS_STATE_CANCELLED
} SingleEISState;

// Temperature data point
typedef struct {
    double timestamp;
    double dtbTemperatures[DTB_NUM_DEVICES];
    double dtbAverageTemperature;
    int dtbDeviceCount;
    double tc0Temperature;
    double tc1Temperature;
    char status[128];
} SingleEISTemperatureData;

// EIS measurement result
typedef struct {
    double ocvVoltage;                    // Open circuit voltage (V)
    double timestamp;                     // Measurement timestamp
    SingleEISTemperatureData tempData;    // Temperature readings
    BIO_TechniqueData *ocvData;          // Raw OCV data
    BIO_TechniqueData *geisData;         // Raw GEIS data
    // Processed impedance data
    double *frequencies;                  // Array of frequencies (Hz)
    double *zReal;                       // Real impedance values (Ohm)
    double *zImag;                       // Imaginary impedance values (Ohm)
    int numPoints;                       // Number of impedance points
    int retryCount;                      // Number of retries
    char filename[MAX_PATH_LENGTH];      // Saved data filename
} SingleEISMeasurement;

// Experiment context
typedef struct {
    SingleEISState state;
    
    // Cancellation handling
    volatile int cancelRequested;
    volatile int emergencyStop;
    
    // Timing
    double measurementStartTime;
    
    // Measurement result
    SingleEISMeasurement measurement;
    
    // File system
    char dataDirectory[MAX_PATH_LENGTH];
    
    // UI handles
    int mainPanelHandle;
    int tabPanelHandle;
    int buttonControl;
    int statusControl;
    int graphBiologicHandle;
    
    // Device handles
    int biologicID;
    
} SingleEISContext;

/******************************************************************************
 * Public Function Prototypes
 ******************************************************************************/

/**
 * Main callback for starting/stopping single EIS measurement
 */
int CVICALLBACK StartSingleEISCallback(int panel, int control, int event,
                                      void *callbackData, int eventData1, 
                                      int eventData2);

/**
 * Check if measurement is running
 * @return 1 if running, 0 if not
 */
int SingleEIS_IsRunning(void);

/**
 * Abort a running measurement
 * @return SUCCESS or error code
 */
int SingleEIS_Abort(void);

/**
 * Emergency stop
 * @return SUCCESS or error code
 */
int SingleEIS_EmergencyStop(void);

/**
 * Cleanup module
 */
void SingleEIS_Cleanup(void);

#endif // EXP_SINGLE_EIS_H