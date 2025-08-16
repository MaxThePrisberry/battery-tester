/******************************************************************************
 * cdaq_utils.h
 * 
 * cDAQ Utilities Module Header
 * Handles NI cDAQ slots 2 and 3 for thermocouple monitoring
 ******************************************************************************/
#ifndef CDAQ_UTILS_H
#define CDAQ_UTILS_H

#include "common.h"
#include <NIDAQmx.h>

/******************************************************************************
 * Configuration Constants
 ******************************************************************************/
#define CDAQ_CHANNELS_PER_SLOT 16
#define CDAQ_TC_MIN_TEMP 0.0
#define CDAQ_TC_MAX_TEMP 400.0
#define CDAQ_CJC_TEMP 25.0
#define CDAQ_READ_TIMEOUT 10.0

/******************************************************************************
 * Public Function Declarations
 ******************************************************************************/

/**
 * Initialize the cDAQ module
 * Creates DAQmx tasks for slots 2 and 3
 * @return SUCCESS or error code
 */
int CDAQ_Initialize(void);

/**
 * Clean up and release all cDAQ resources
 */
void CDAQ_Cleanup(void);

/**
 * Read a specific thermocouple channel
 * @param slot - cDAQ module slot number (2 or 3 only)
 * @param tc_number - Thermocouple channel number (0-15)
 * @param temperature - Pointer to receive temperature value in degrees C
 * @return SUCCESS or error code
 */
int CDAQ_ReadTC(int slot, int tc_number, double *temperature);

/**
 * Read all thermocouple channels for a slot
 * @param slot - cDAQ module slot number (2 or 3 only)
 * @param temperatures - Array to receive temperature values (must be size 16)
 * @param num_read - Pointer to receive number of channels read
 * @return SUCCESS or error code
 */
int CDAQ_ReadTCArray(int slot, double *temperatures, int *num_read);

#endif // CDAQ_UTILS_H