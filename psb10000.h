/******************************************************************************
 * PSB 10000 Series Power Supply Control Library
 * Header file for LabWindows/CVI with Auto-Discovery
 * 
 * This library provides functions to control PSB 10000 series power supplies
 * via Modbus RTU communication protocol, including automatic port scanning.
 * 
 * Author: Generated for LabWindows/CVI
 * Date: 2025
 ******************************************************************************/

#ifndef PSB10000_H
#define PSB10000_H

#include <ansi_c.h>
#include <rs232.h>
#include <utility.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// Error codes
#define PSB_SUCCESS                 0
#define PSB_ERROR_COMM             -1
#define PSB_ERROR_CRC              -2
#define PSB_ERROR_TIMEOUT          -3
#define PSB_ERROR_INVALID_PARAM    -4
#define PSB_ERROR_BUSY             -5

// Modbus function codes
#define MODBUS_READ_COILS                   0x01
#define MODBUS_READ_DISCRETE_INPUTS         0x02
#define MODBUS_READ_HOLDING_REGISTERS       0x03
#define MODBUS_READ_INPUT_REGISTERS         0x04
#define MODBUS_WRITE_SINGLE_COIL            0x05
#define MODBUS_WRITE_SINGLE_REGISTER        0x06
#define MODBUS_WRITE_MULTIPLE_COILS         0x0F
#define MODBUS_WRITE_MULTIPLE_REGISTERS     0x10

// Modbus constants
#define MODBUS_CRC_INIT             0xFFFF
#define DEFAULT_TIMEOUT_MS          1000

// PSB 10000 specific register addresses (from manual)
#define REG_REMOTE_MODE             402     // 0x0192
#define REG_DC_OUTPUT               405     // 0x0195
#define REG_OPERATION_MODE          409     // 0x0199
#define REG_SET_VOLTAGE             500     // 0x01F4
#define REG_SET_CURRENT             501     // 0x01F5
#define REG_SET_POWER_SOURCE        502     // 0x01F6
#define REG_SET_POWER_SINK          498     // 0x01F2
#define REG_DEVICE_STATE            505     // 0x01F9
#define REG_ACTUAL_VOLTAGE          507     // 0x01FB
#define REG_ACTUAL_CURRENT          508     // 0x01FC
#define REG_ACTUAL_POWER            509     // 0x01FD
#define REG_VOLTAGE_MAX             9000    // 0x2328
#define REG_VOLTAGE_MIN             9001    // 0x2329
#define REG_CURRENT_MAX             9002    // 0x232A
#define REG_CURRENT_MIN             9003    // 0x232B
#define REG_POWER_MAX               9004    // 0x232C

// Coil values
#define COIL_OFF                    0x0000
#define COIL_ON                     0xFF00

// Operation modes
#define MODE_UIP                    0x0000  // Voltage priority
#define MODE_UIR                    0xFF00  // Current priority

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// PSB Handle structure
typedef struct {
    int comPort;
    int slaveAddress;
    int timeoutMs;
    int isConnected;
} PSB_Handle;

// Device status structure
typedef struct {
    double voltage;         // Actual voltage in V
    double current;         // Actual current in A
    double power;           // Actual power in W
    int outputEnabled;      // 1 = output on, 0 = output off
    int remoteMode;         // 1 = remote mode, 0 = local mode
    int operationMode;      // Current regulation mode
} PSB_Status;

// Device limits structure
typedef struct {
    double maxVoltage;      // Maximum voltage limit in V
    double minVoltage;      // Minimum voltage limit in V
    double maxCurrent;      // Maximum current limit in A
    double minCurrent;      // Minimum current limit in A
    double maxPower;        // Maximum power limit in W
} PSB_Limits;

// Discovery result structure
typedef struct {
    char deviceType[50];    // Device model string (e.g., "PSB 10080-1000")
    char serialNumber[50];  // Device serial number
    int comPort;            // COM port where device was found
    int slaveAddress;       // Modbus slave address
    int baudRate;           // Communication baud rate
} PSB_DiscoveryResult;

/******************************************************************************
 * Auto-Discovery Functions
 ******************************************************************************/

/**
 * Scan a specific COM port for PSB devices
 * @param comPort COM port number to scan (1-255)
 * @param result Pointer to structure to store discovery results
 * @return PSB_SUCCESS if device found, error code otherwise
 */
int PSB_ScanPort(int comPort, PSB_DiscoveryResult *result);

/**
 * Auto-discover all PSB devices on available COM ports
 * @param devices Array to store discovered devices
 * @param maxDevices Maximum number of devices to find
 * @param foundCount Pointer to store actual number of devices found
 * @return PSB_SUCCESS if any devices found, error code otherwise
 */
int PSB_AutoDiscover(PSB_DiscoveryResult *devices, int maxDevices, int *foundCount);

/******************************************************************************
 * Initialization and Communication Functions
 ******************************************************************************/

/**
 * Initialize connection to PSB device (default 9600 baud)
 * @param handle Pointer to PSB handle structure
 * @param comPort COM port number (1-255)
 * @param slaveAddress Modbus slave address (1-247)
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_Initialize(PSB_Handle *handle, int comPort, int slaveAddress);

/**
 * Initialize connection to PSB device with specific baud rate
 * @param handle Pointer to PSB handle structure
 * @param comPort COM port number (1-255)
 * @param slaveAddress Modbus slave address (1-247)
 * @param baudRate Communication baud rate
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_InitializeSpecific(PSB_Handle *handle, int comPort, int slaveAddress, int baudRate);

/**
 * Close connection to PSB device
 * @param handle Pointer to PSB handle structure
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_Close(PSB_Handle *handle);

/**
 * Set communication timeout
 * @param handle Pointer to PSB handle structure
 * @param timeoutMs Timeout in milliseconds
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetTimeout(PSB_Handle *handle, int timeoutMs);

/******************************************************************************
 * Basic Control Functions
 ******************************************************************************/

/**
 * Enable/disable remote mode
 * @param handle Pointer to PSB handle structure
 * @param enable 1 = enable remote mode, 0 = local mode
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetRemoteMode(PSB_Handle *handle, int enable);

/**
 * Enable/disable DC output
 * @param handle Pointer to PSB handle structure
 * @param enable 1 = enable output, 0 = disable output
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetOutputEnable(PSB_Handle *handle, int enable);

/**
 * Set operation mode
 * @param handle Pointer to PSB handle structure
 * @param mode 0 = UIP (voltage priority), 1 = UIR (current priority)
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetOperationMode(PSB_Handle *handle, int mode);

/******************************************************************************
 * Voltage and Current Control Functions
 ******************************************************************************/

/**
 * Set output voltage
 * @param handle Pointer to PSB handle structure
 * @param voltage Voltage in volts
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetVoltage(PSB_Handle *handle, double voltage);

/**
 * Set output current
 * @param handle Pointer to PSB handle structure
 * @param current Current in amperes
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetCurrent(PSB_Handle *handle, double current);

/**
 * Get actual voltage, current, and power values
 * @param handle Pointer to PSB handle structure
 * @param status Pointer to status structure to fill
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_GetActualValues(PSB_Handle *handle, PSB_Status *status);

/******************************************************************************
 * Limit Configuration Functions
 ******************************************************************************/

/**
 * Set voltage limits
 * @param handle Pointer to PSB handle structure
 * @param minVoltage Minimum voltage in volts
 * @param maxVoltage Maximum voltage in volts
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetVoltageLimits(PSB_Handle *handle, double minVoltage, double maxVoltage);

/**
 * Set current limits
 * @param handle Pointer to PSB handle structure
 * @param minCurrent Minimum current in amperes
 * @param maxCurrent Maximum current in amperes
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetCurrentLimits(PSB_Handle *handle, double minCurrent, double maxCurrent);

/**
 * Set power limit
 * @param handle Pointer to PSB handle structure
 * @param maxPower Maximum power in watts
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_SetPowerLimit(PSB_Handle *handle, double maxPower);

/**
 * Get current limits
 * @param handle Pointer to PSB handle structure
 * @param limits Pointer to limits structure to fill
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_GetLimits(PSB_Handle *handle, PSB_Limits *limits);

/******************************************************************************
 * Status and Monitoring Functions
 ******************************************************************************/

/**
 * Get device status including actual values and states
 * @param handle Pointer to PSB handle structure
 * @param status Pointer to status structure to fill
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_GetDeviceStatus(PSB_Handle *handle, PSB_Status *status);

/**
 * Check if output is enabled
 * @param handle Pointer to PSB handle structure
 * @param enabled Pointer to store result (1 = enabled, 0 = disabled)
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_IsOutputEnabled(PSB_Handle *handle, int *enabled);

/**
 * Check if remote mode is active
 * @param handle Pointer to PSB handle structure
 * @param active Pointer to store result (1 = remote, 0 = local)
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_IsRemoteModeActive(PSB_Handle *handle, int *active);

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

/**
 * Get error string for error code
 * @param errorCode Error code returned by PSB functions
 * @return Pointer to error description string
 */
const char* PSB_GetErrorString(int errorCode);

/**
 * Convert real value to device units
 * @param realValue Real world value
 * @param nominalValue Nominal (rated) value for the parameter
 * @return Device units (0-53477 representing 0-102%)
 */
int PSB_ConvertToDeviceUnits(double realValue, double nominalValue);

/**
 * Convert device units to real value
 * @param deviceValue Device units (0-53477)
 * @param nominalValue Nominal (rated) value for the parameter
 * @return Real world value
 */
double PSB_ConvertFromDeviceUnits(int deviceValue, double nominalValue);

/******************************************************************************
 * Low-level Modbus Functions
 ******************************************************************************/

/**
 * Read holding registers
 * @param handle Pointer to PSB handle structure
 * @param startAddr Starting register address
 * @param numRegs Number of registers to read
 * @param data Pointer to array to store register values
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_ReadHoldingRegisters(PSB_Handle *handle, int startAddr, int numRegs, unsigned short *data);

/**
 * Write single coil
 * @param handle Pointer to PSB handle structure
 * @param address Coil address
 * @param value Coil value (COIL_ON or COIL_OFF)
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_WriteSingleCoil(PSB_Handle *handle, int address, int value);

/**
 * Write single register
 * @param handle Pointer to PSB handle structure
 * @param address Register address
 * @param value Register value
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_WriteSingleRegister(PSB_Handle *handle, int address, int value);

/**
 * Write multiple registers
 * @param handle Pointer to PSB handle structure
 * @param startAddr Starting register address
 * @param numRegs Number of registers to write
 * @param data Pointer to array of register values
 * @return PSB_SUCCESS on success, error code on failure
 */
int PSB_WriteMultipleRegisters(PSB_Handle *handle, int startAddr, int numRegs, unsigned short *data);

/**
 * Calculate Modbus CRC
 * @param data Pointer to data buffer
 * @param length Number of bytes in buffer
 * @return CRC value
 */
unsigned short PSB_CalculateCRC(unsigned char *data, int length);

#endif // PSB10000_H