/******************************************************************************
 * PSB 10000 Series Power Supply Control Library - Simplified Version
 * Header file for LabWindows/CVI
 * 
 * This simplified library provides essential functions to control PSB 10000
 * series power supplies via Modbus RTU communication protocol.
 * 
 * Configured for 60V/60A derated version
 ******************************************************************************/

#ifndef PSB10000_H
#define PSB10000_H

#include <ansi_c.h>
#include <rs232.h>
#include <utility.h>

/******************************************************************************
 * Constants and Definitions
 ******************************************************************************/

// Device specifications for 60V/60A derated version
#define PSB_NOMINAL_VOLTAGE     60.0    // V
#define PSB_NOMINAL_CURRENT     60.0    // A
#define PSB_NOMINAL_POWER       1200.0  // W (derated due to the outlet)

// Error codes
#define PSB_SUCCESS                 0
#define PSB_ERROR_COMM             -1
#define PSB_ERROR_CRC              -2
#define PSB_ERROR_TIMEOUT          -3
#define PSB_ERROR_INVALID_PARAM    -4
#define PSB_ERROR_BUSY             -5
#define PSB_ERROR_NOT_CONNECTED    -6
#define PSB_ERROR_RESPONSE         -7

// Modbus function codes
#define MODBUS_READ_HOLDING_REGISTERS       0x03
#define MODBUS_WRITE_SINGLE_COIL            0x05
#define MODBUS_WRITE_SINGLE_REGISTER        0x06

// Modbus constants
#define MODBUS_CRC_INIT             0xFFFF
#define DEFAULT_TIMEOUT_MS          1000
#define DEFAULT_SLAVE_ADDRESS       1

// PSB register addresses
#define REG_DEVICE_CLASS            0       // 0x0000
#define REG_DEVICE_TYPE             1       // 0x0001
#define REG_SERIAL_NUMBER           151     // 0x0097
#define REG_REMOTE_MODE             402     // 0x0192
#define REG_DC_OUTPUT               405     // 0x0195
#define REG_SET_VOLTAGE             500     // 0x01F4
#define REG_SET_CURRENT             501     // 0x01F5
#define REG_SET_POWER_SOURCE        502     // 0x01F6
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

// Device state bit masks (Register 505)
#define STATE_CONTROL_LOCATION_MASK 0x0000001F  // Bits 0-4
#define STATE_OUTPUT_ENABLED        0x00000080  // Bit 7
#define STATE_REGULATION_MODE_MASK  0x00000600  // Bits 9-10
#define STATE_REMOTE_MODE           0x00000800  // Bit 11
#define STATE_ALARMS_ACTIVE         0x00008000  // Bit 15

// Control locations
#define CONTROL_FREE                0x00
#define CONTROL_LOCAL               0x01
#define CONTROL_USB                 0x03
#define CONTROL_ANALOG              0x04

/******************************************************************************
 * Data Structures
 ******************************************************************************/

// PSB Handle structure
typedef struct {
    int comPort;
    int slaveAddress;
    int timeoutMs;
    int isConnected;
    char serialNumber[50];
} PSB_Handle;

// Device status structure
typedef struct {
    double voltage;         // Actual voltage in V
    double current;         // Actual current in A
    double power;           // Actual power in W
    int outputEnabled;      // 1 = output on, 0 = output off
    int remoteMode;         // 1 = remote mode, 0 = local mode
    int regulationMode;     // 0=CV, 1=CR, 2=CC, 3=CP
    int controlLocation;    // Control location (0=free, 1=local, etc.)
    int alarmsActive;       // 1 = alarms active, 0 = no alarms
    unsigned long rawState; // Raw 32-bit state value for debugging
} PSB_Status;

// Discovery result structure
typedef struct {
    char deviceType[50];
    char serialNumber[50];
    int comPort;
    int slaveAddress;
    int baudRate;
} PSB_DiscoveryResult;

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/

// Auto-Discovery Functions
int PSB_ScanPort(int comPort, PSB_DiscoveryResult *result);
int PSB_AutoDiscover(const char *targetSerial, PSB_Handle *handle);

// Connection Functions
int PSB_InitializeSpecific(PSB_Handle *handle, int comPort, int slaveAddress, int baudRate);
int PSB_Close(PSB_Handle *handle);

// Basic Control Functions
int PSB_SetRemoteMode(PSB_Handle *handle, int enable);
int PSB_SetOutputEnable(PSB_Handle *handle, int enable);

// Voltage Control Functions
int PSB_SetVoltage(PSB_Handle *handle, double voltage);
int PSB_SetVoltageLimits(PSB_Handle *handle, double minVoltage, double maxVoltage);

// Current Control Functions
int PSB_SetCurrent(PSB_Handle *handle, double current);
int PSB_SetCurrentLimits(PSB_Handle *handle, double minCurrent, double maxCurrent);

// Power Control Functions
int PSB_SetPower(PSB_Handle *handle, double power);
int PSB_SetPowerLimit(PSB_Handle *handle, double maxPower);

// Status Functions
int PSB_GetStatus(PSB_Handle *handle, PSB_Status *status);
int PSB_GetActualValues(PSB_Handle *handle, double *voltage, double *current, double *power);

// Utility Functions
const char* PSB_GetErrorString(int errorCode);
unsigned short PSB_CalculateCRC(unsigned char *data, int length);

// Debug Functions
void PSB_EnableDebugOutput(int enable);
void PSB_PrintStatus(PSB_Status *status);

#endif // PSB10000_H