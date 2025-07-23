/******************************************************************************
 * teensy_dll.h
 * 
 * Teensy Microcontroller Control Library
 * Header file for LabWindows/CVI
 * 
 * This library provides functions to control digital pins on a Teensy
 * microcontroller via serial communication.
 * 
 * Protocol: Send "D<pin><H/L>\n", Receive "<pin><0/1>\n"
 ******************************************************************************/

#ifndef TEENSY_DLL_H
#define TEENSY_DLL_H

#include "common.h"
#include <rs232.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// TNY-specific error codes (using base from common.h)
#define TNY_SUCCESS                 SUCCESS
#define TNY_ERROR_COMM             (ERR_BASE_TNY - 1)
#define TNY_ERROR_TIMEOUT          (ERR_BASE_TNY - 2)
#define TNY_ERROR_INVALID_PIN      (ERR_BASE_TNY - 3)
#define TNY_ERROR_NOT_CONNECTED    (ERR_BASE_TNY - 4)
#define TNY_ERROR_INVALID_RESP     (ERR_BASE_TNY - 5)
#define TNY_ERROR_VERIFY_FAILED    (ERR_BASE_TNY - 6)
#define TNY_ERROR_INVALID_PARAM    (ERR_BASE_TNY - 7)

// Communication constants
#define TNY_DEFAULT_BAUD_RATE      9600
#define TNY_DEFAULT_TIMEOUT_MS     100
#define TNY_RESPONSE_DELAY_MS      10

// Pin constants
#define TNY_MIN_PIN                0
#define TNY_MAX_PIN                16

// Protocol constants
#define TNY_CMD_PREFIX             'D'
#define TNY_PIN_HIGH               'H'
#define TNY_PIN_LOW                'L'
#define TNY_STATE_HIGH             '1'
#define TNY_STATE_LOW              '0'

// Frame sizes
#define TNY_COMMAND_SIZE           5    // D<2pin><H/L>\n
#define TNY_RESPONSE_SIZE          4    // <2pin><0/1>\n

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// Teensy Handle structure
typedef struct {
    int comPort;
    int baudRate;
    int timeoutMs;
    int isConnected;
    DeviceState state;
    int minPin;
    int maxPin;
} TNY_Handle;

// Pin state enumeration
typedef enum {
    TNY_PIN_STATE_LOW = 0,
    TNY_PIN_STATE_HIGH = 1
} TNY_PinState;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Connection Functions
/**
 * Initialize connection to Teensy
 * @param handle - Teensy handle structure to initialize
 * @param comPort - COM port number (1-16)
 * @param baudRate - Baud rate (default: 9600)
 * @return TNY_SUCCESS or error code
 */
int TNY_Initialize(TNY_Handle *handle, int comPort, int baudRate);

/**
 * Close connection to Teensy
 * @param handle - Teensy handle
 * @return TNY_SUCCESS or error code
 */
int TNY_Close(TNY_Handle *handle);

/**
 * Test connection by setting pin 0 low
 * @param handle - Teensy handle
 * @return TNY_SUCCESS or error code
 */
int TNY_TestConnection(TNY_Handle *handle);

// Pin Control Functions
/**
 * Set digital pin state
 * @param handle - Teensy handle
 * @param pin - Pin number (0-16)
 * @param state - Pin state (0=low, 1=high)
 * @return TNY_SUCCESS or error code
 */
int TNY_SetPin(TNY_Handle *handle, int pin, int state);

/**
 * Set multiple pins at once (convenience function)
 * @param handle - Teensy handle
 * @param pins - Array of pin numbers
 * @param states - Array of pin states
 * @param count - Number of pins to set
 * @return TNY_SUCCESS or error code
 */
int TNY_SetMultiplePins(TNY_Handle *handle, const int *pins, const int *states, int count);

// Utility Functions
/**
 * Get error string for error code
 * @param errorCode - Error code
 * @return Error description string
 */
const char* TNY_GetErrorString(int errorCode);

/**
 * Enable/disable debug output
 * @param enable - 1 to enable, 0 to disable
 */
void TNY_EnableDebugOutput(int enable);

/**
 * Get library version string
 * @return Version string
 */
const char* TNY_GetVersion(void);

/**
 * Check if handle is connected
 * @param handle - Teensy handle
 * @return 1 if connected, 0 otherwise
 */
int TNY_IsConnected(const TNY_Handle *handle);

// Low-level Functions (usually not called directly)
/**
 * Send command and receive response
 * @param handle - Teensy handle
 * @param command - Command string (without LF)
 * @param response - Buffer for response (without LF)
 * @param responseSize - Size of response buffer
 * @return TNY_SUCCESS or error code
 */
int TNY_SendCommand(TNY_Handle *handle, const char *command, char *response, int responseSize);

#endif // TEENSY_DLL_H