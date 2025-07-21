/******************************************************************************
 * DTB4848 Temperature Controller Library
 * Implementation file for LabWindows/CVI
 * 
 * Configured for K-type thermocouple with PID control
 ******************************************************************************/

#include "dtb4848_dll.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static int debugEnabled = 0;

static const char* errorStrings[] = {
    "Success",
    "Communication error",
    "Checksum error", 
    "Timeout error",
    "Invalid parameter",
    "Device busy",
    "Not connected",
    "Invalid response",
    "Not supported"
};

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static void PrintDebug(const char *format, ...) {
    if (!debugEnabled) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    LogDebugEx(LOG_DEVICE_DTB, "%s", buffer);
}

static unsigned char CalculateLRC(const unsigned char *data, int length) {
    unsigned char lrc = 0;
    for (int i = 0; i < length; i++) {
        lrc += data[i];
    }
    return (unsigned char)(-(char)lrc);
}

static int HexCharToInt(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int HexStringToBytes(const char *hex, unsigned char *bytes, int maxBytes) {
    int len = strlen(hex);
    if (len % 2 != 0) return -1;
    
    int byteCount = len / 2;
    if (byteCount > maxBytes) return -1;
    
    for (int i = 0; i < byteCount; i++) {
        int high = HexCharToInt(hex[i * 2]);
        int low = HexCharToInt(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return -1;
        bytes[i] = (unsigned char)((high << 4) | low);
    }
    
    return byteCount;
}

static void BytesToHexString(const unsigned char *bytes, int length, char *hex) {
    for (int i = 0; i < length; i++) {
        sprintf(&hex[i * 2], "%02X", bytes[i]);
    }
    hex[length * 2] = '\0';
}

static int SendModbusASCII(DTB_Handle *handle, unsigned char functionCode, 
                          unsigned short address, unsigned short data,
                          unsigned char *response, int maxResponseLen) {
    if (!handle || !handle->isConnected) {
        return DTB_ERROR_NOT_CONNECTED;
    }
    
    // Build binary message
    unsigned char binMsg[6];
    binMsg[0] = (unsigned char)handle->slaveAddress;
    binMsg[1] = functionCode;
    binMsg[2] = (unsigned char)((address >> 8) & 0xFF);
    binMsg[3] = (unsigned char)(address & 0xFF);
    binMsg[4] = (unsigned char)((data >> 8) & 0xFF);
    binMsg[5] = (unsigned char)(data & 0xFF);
    
    // Calculate LRC
    unsigned char lrc = CalculateLRC(binMsg, 6);
    
    // Convert to ASCII
    char asciiFrame[32];
    char hexData[16];
    BytesToHexString(binMsg, 6, hexData);
    sprintf(asciiFrame, ":%s%02X\r\n", hexData, lrc);
    
    PrintDebug("TX: %s", asciiFrame);
    
    // Clear input buffer
    FlushInQ(handle->comPort);
    
    // Send command
    int frameLen = strlen(asciiFrame);
    if (ComWrt(handle->comPort, asciiFrame, frameLen) != frameLen) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to write to COM port");
        return DTB_ERROR_COMM;
    }
    
    // Wait for response
    Delay(0.1);  // 100ms for device processing
    
    // Read response
    char rxBuffer[128] = {0};
    int totalRead = 0;
    double startTime = Timer();
    
    // Look for start character
    while (totalRead == 0) {
        int available = GetInQLen(handle->comPort);
        if (available > 0) {
            char c;
            if (ComRd(handle->comPort, &c, 1) == 1) {
                if (c == MODBUS_ASCII_START) {
                    rxBuffer[totalRead++] = c;
                    break;
                }
            }
        }
        
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_DTB, "Timeout waiting for response");
            return DTB_ERROR_TIMEOUT;
        }
    }
    
    // Read until CR/LF
    while (totalRead < sizeof(rxBuffer) - 1) {
        int available = GetInQLen(handle->comPort);
        if (available > 0) {
            char c;
            if (ComRd(handle->comPort, &c, 1) == 1) {
                rxBuffer[totalRead++] = c;
                if (totalRead >= 2 && rxBuffer[totalRead-2] == MODBUS_ASCII_CR && 
                    rxBuffer[totalRead-1] == MODBUS_ASCII_LF) {
                    break;
                }
            }
        }
        
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_DTB, "Timeout reading response");
            return DTB_ERROR_TIMEOUT;
        }
    }
    
    rxBuffer[totalRead] = '\0';
    PrintDebug("RX: %s", rxBuffer);
    
    // Parse ASCII response
    if (totalRead < 11 || rxBuffer[0] != MODBUS_ASCII_START) {
        LogErrorEx(LOG_DEVICE_DTB, "Invalid response format");
        return DTB_ERROR_RESPONSE;
    }
    
    // Extract hex data (skip ':', take until CR)
    int hexLen = 0;
    for (int i = 1; i < totalRead && rxBuffer[i] != MODBUS_ASCII_CR; i++) {
        hexData[hexLen++] = rxBuffer[i];
    }
    hexData[hexLen] = '\0';
    
    // Convert hex to binary
    unsigned char binResponse[64];
    int binLen = HexStringToBytes(hexData, binResponse, sizeof(binResponse));
    if (binLen < 4) {
        LogErrorEx(LOG_DEVICE_DTB, "Response too short");
        return DTB_ERROR_RESPONSE;
    }
    
    // Verify LRC
    unsigned char calcLrc = CalculateLRC(binResponse, binLen - 1);
    if (calcLrc != binResponse[binLen - 1]) {
        LogErrorEx(LOG_DEVICE_DTB, "LRC mismatch: calc=0x%02X, recv=0x%02X", 
                   calcLrc, binResponse[binLen - 1]);
        return DTB_ERROR_CHECKSUM;
    }
    
    // Check slave address
    if (binResponse[0] != handle->slaveAddress) {
        LogErrorEx(LOG_DEVICE_DTB, "Wrong slave address: expected %d, got %d",
                   handle->slaveAddress, binResponse[0]);
        return DTB_ERROR_RESPONSE;
    }
    
    // Check for exception
    if (binResponse[1] & 0x80) {
        LogErrorEx(LOG_DEVICE_DTB, "Modbus exception: code 0x%02X", binResponse[2]);
        return DTB_ERROR_RESPONSE;
    }
    
    // Check function code
    if (binResponse[1] != functionCode) {
        LogErrorEx(LOG_DEVICE_DTB, "Wrong function code: expected 0x%02X, got 0x%02X",
                   functionCode, binResponse[1]);
        return DTB_ERROR_RESPONSE;
    }
    
    // Copy response data
    if (response && maxResponseLen > 0) {
        int copyLen = binLen - 1;  // Exclude LRC
        if (copyLen > maxResponseLen) copyLen = maxResponseLen;
        memcpy(response, binResponse, copyLen);
    }
    
    Delay(0.05);  // 50ms recovery time
    
    return DTB_SUCCESS;
}

/******************************************************************************
 * Auto-Discovery Functions
 ******************************************************************************/

int DTB_ScanPort(int comPort, DTB_DiscoveryResult *result) {
    if (!result) return DTB_ERROR_INVALID_PARAM;
    
    memset(result, 0, sizeof(DTB_DiscoveryResult));
    
    // Try default communication parameters first
    int baudRates[] = {9600, 19200, 38400, 57600, 115200};
    int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
    
    for (int i = 0; i < numRates; i++) {
        PrintDebug("Trying COM%d at %d baud...", comPort, baudRates[i]);
        
        SetBreakOnLibraryErrors(0);
        int portResult = OpenComConfig(comPort, "", baudRates[i], 0, 8, 1, 512, 512);
        SetBreakOnLibraryErrors(1);
        
        if (portResult < 0) {
            continue;
        }
        
        SetComTime(comPort, 1.0);
        
        // Create temporary handle for testing
        DTB_Handle tempHandle;
        tempHandle.comPort = comPort;
        tempHandle.slaveAddress = DEFAULT_SLAVE_ADDRESS;
        tempHandle.baudRate = baudRates[i];
        tempHandle.timeoutMs = DEFAULT_TIMEOUT_MS;
        tempHandle.isConnected = 1;
        
        // Try to read software version
        unsigned short version;
        if (DTB_ReadRegister(&tempHandle, REG_SOFTWARE_VERSION, &version) == DTB_SUCCESS) {
            // Success! Fill in discovery result
            result->comPort = comPort;
            result->slaveAddress = DEFAULT_SLAVE_ADDRESS;
            result->baudRate = baudRates[i];
            result->firmwareVersion = version / 100.0;
            sprintf(result->modelType, "DTB4848 V%.2f", result->firmwareVersion);
            
            CloseCom(comPort);
            
            LogMessageEx(LOG_DEVICE_DTB, "Found DTB4848 on COM%d: %s", 
                        comPort, result->modelType);
            
            return DTB_SUCCESS;
        }
        
        CloseCom(comPort);
    }
    
    return DTB_ERROR_COMM;
}

int DTB_AutoDiscover(DTB_Handle *handle) {
    if (!handle) return DTB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_DTB, "=== AUTO-DISCOVERING DTB4848 ===");
    
    SetBreakOnLibraryErrors(0);
    
    // Scan common COM ports
    int portsToScan[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int numPorts = sizeof(portsToScan) / sizeof(portsToScan[0]);
    
    for (int i = 0; i < numPorts; i++) {
        int port = portsToScan[i];
        DTB_DiscoveryResult result;
        
        LogMessageEx(LOG_DEVICE_DTB, "Scanning COM%d...", port);
        
        if (DTB_ScanPort(port, &result) == DTB_SUCCESS) {
            LogMessageEx(LOG_DEVICE_DTB, "  Found DTB4848!");
            LogMessageEx(LOG_DEVICE_DTB, "  Model: %s", result.modelType);
            
            SetBreakOnLibraryErrors(1);
            
            // Initialize with discovered parameters
            if (DTB_Initialize(handle, result.comPort, result.slaveAddress, 
                              result.baudRate) == DTB_SUCCESS) {
                strcpy(handle->modelNumber, result.modelType);
                LogMessageEx(LOG_DEVICE_DTB, "? Successfully connected to DTB4848 on COM%d", port);
                return DTB_SUCCESS;
            } else {
                LogErrorEx(LOG_DEVICE_DTB, "? Found device but failed to connect");
                return DTB_ERROR_COMM;
            }
        }
        
        Delay(0.05);
    }
    
    SetBreakOnLibraryErrors(1);
    
    LogErrorEx(LOG_DEVICE_DTB, "? DTB4848 not found on any port");
    return DTB_ERROR_COMM;
}

/******************************************************************************
 * Connection Functions
 ******************************************************************************/

int DTB_Initialize(DTB_Handle *handle, int comPort, int slaveAddress, int baudRate) {
    if (!handle) return DTB_ERROR_INVALID_PARAM;
    
    memset(handle, 0, sizeof(DTB_Handle));
    handle->comPort = comPort;
    handle->slaveAddress = slaveAddress;
    handle->baudRate = baudRate;
    handle->timeoutMs = DEFAULT_TIMEOUT_MS;
    handle->state = DEVICE_STATE_CONNECTING;
    
    LogMessageEx(LOG_DEVICE_DTB, "Initializing on COM%d, slave %d, %d baud", 
                 comPort, slaveAddress, baudRate);
    
    if (OpenComConfig(comPort, "", baudRate, 0, 8, 1, 512, 512) < 0) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to open COM%d", comPort);
        handle->state = DEVICE_STATE_ERROR;
        return DTB_ERROR_COMM;
    }
    
    SetComTime(comPort, handle->timeoutMs / 1000.0);
    
    handle->isConnected = 1;
    handle->state = DEVICE_STATE_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Successfully initialized");
    return DTB_SUCCESS;
}

int DTB_Close(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Closing connection on COM%d", handle->comPort);
    
    // Stop any active operations
    DTB_SetRunStop(handle, 0);
    
    CloseCom(handle->comPort);
    handle->isConnected = 0;
    handle->state = DEVICE_STATE_DISCONNECTED;
    
    return DTB_SUCCESS;
}

int DTB_TestConnection(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    // Try to read software version
    unsigned short version;
    return DTB_ReadRegister(handle, REG_SOFTWARE_VERSION, &version);
}

/******************************************************************************
 * Configuration Functions
 ******************************************************************************/

int DTB_FactoryReset(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Performing factory reset...");
    
    // Write magic value to first reset register
    int result = DTB_WriteRegister(handle, REG_FACTORY_RESET_1, FACTORY_RESET_VALUE);
    if (result != DTB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to write first reset register");
        return result;
    }
    
    // Write magic value to second reset register
    result = DTB_WriteRegister(handle, REG_FACTORY_RESET_2, FACTORY_RESET_VALUE);
    if (result != DTB_SUCCESS) {
        LogErrorEx(LOG_DEVICE_DTB, "Failed to write second reset register");
        return result;
    }
    
    LogMessageEx(LOG_DEVICE_DTB, "Factory reset command sent - power cycle required");
    
    // Note: User must power cycle the device after factory reset
    return DTB_SUCCESS;
}

int DTB_Configure(DTB_Handle *handle, const DTB_Configuration *config) {
    if (!handle || !handle->isConnected || !config) return DTB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_DTB, "Configuring DTB4848...");
    
    int result;
    
    // Set sensor type
    result = DTB_SetSensorType(handle, config->sensorType);
    if (result != DTB_SUCCESS) return result;
    
    // Set temperature limits
    result = DTB_SetTemperatureLimits(handle, config->upperTempLimit, config->lowerTempLimit);
    if (result != DTB_SUCCESS) return result;
    
    // Set control method
    result = DTB_SetControlMethod(handle, config->controlMethod);
    if (result != DTB_SUCCESS) return result;
    
    // Set PID mode
    result = DTB_SetPIDMode(handle, config->pidMode);
    if (result != DTB_SUCCESS) return result;
    
    // Configure alarm if not disabled
    if (config->alarmType != ALARM_DISABLED) {
        result = DTB_WriteRegister(handle, REG_ALARM1_TYPE, (unsigned short)config->alarmType);
        if (result != DTB_SUCCESS) return result;
        
        result = DTB_SetAlarmLimits(handle, config->alarmUpperLimit, config->alarmLowerLimit);
        if (result != DTB_SUCCESS) return result;
    }
    
    LogMessageEx(LOG_DEVICE_DTB, "Configuration complete");
    return DTB_SUCCESS;
}

int DTB_ConfigureForPID(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    // Simplified configuration for K-type thermocouple with PID control
    DTB_Configuration config = {
        .sensorType = SENSOR_TYPE_K,
        .controlMethod = CONTROL_METHOD_PID,
        .pidMode = PID_MODE_AUTO,
        .upperTempLimit = K_TYPE_MAX_TEMP,
        .lowerTempLimit = K_TYPE_MIN_TEMP,
        .alarmType = ALARM_DISABLED,
        .alarmUpperLimit = 0.0,
        .alarmLowerLimit = 0.0
    };
    
    return DTB_Configure(handle, &config);
}

/******************************************************************************
 * Basic Control Functions
 ******************************************************************************/

int DTB_SetRunStop(DTB_Handle *handle, int run) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting Run/Stop: %s", run ? "RUN" : "STOP");
    
    return DTB_WriteBit(handle, BIT_RUN_STOP, run ? 1 : 0);
}

int DTB_SetSetPoint(DTB_Handle *handle, double temperature) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    // Validate temperature range for K-type
    if (temperature < K_TYPE_MIN_TEMP || temperature > K_TYPE_MAX_TEMP) {
        LogErrorEx(LOG_DEVICE_DTB, "Temperature %.1f°C out of range (%.1f to %.1f)",
                   temperature, K_TYPE_MIN_TEMP, K_TYPE_MAX_TEMP);
        return DTB_ERROR_INVALID_PARAM;
    }
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting setpoint: %.1f°C", temperature);
    
    // Temperature is stored as value * 10 (one decimal place)
    short tempValue = (short)(temperature * 10);
    
    return DTB_WriteRegister(handle, REG_SET_POINT, (unsigned short)tempValue);
}

int DTB_StartAutoTuning(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Starting auto-tuning...");
    
    return DTB_WriteBit(handle, BIT_AUTO_TUNING, 1);
}

int DTB_StopAutoTuning(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_DTB, "Stopping auto-tuning...");
    
    return DTB_WriteBit(handle, BIT_AUTO_TUNING, 0);
}

/******************************************************************************
 * Read Functions
 ******************************************************************************/

int DTB_GetStatus(DTB_Handle *handle, DTB_Status *status) {
    if (!handle || !handle->isConnected || !status) return DTB_ERROR_INVALID_PARAM;
    
    memset(status, 0, sizeof(DTB_Status));
    
    int result;
    unsigned short value;
    int bitValue;
    
    // Read process value
    result = DTB_ReadRegister(handle, REG_PROCESS_VALUE, &value);
    if (result == DTB_SUCCESS) {
        status->processValue = (short)value / 10.0;
    }
    
    // Read setpoint
    result = DTB_ReadRegister(handle, REG_SET_POINT, &value);
    if (result == DTB_SUCCESS) {
        status->setPoint = (short)value / 10.0;
    }
    
    // Read run/stop status
    result = DTB_ReadBit(handle, BIT_RUN_STOP, &bitValue);
    if (result == DTB_SUCCESS) {
        status->outputEnabled = bitValue;
    }
    
    // Read output states
    result = DTB_ReadBit(handle, BIT_OUTPUT1_STATUS, &bitValue);
    if (result == DTB_SUCCESS) {
        status->output1State = bitValue;
    }
    
    result = DTB_ReadBit(handle, BIT_OUTPUT2_STATUS, &bitValue);
    if (result == DTB_SUCCESS) {
        status->output2State = bitValue;
    }
    
    // Read alarm state
    result = DTB_ReadBit(handle, BIT_ALARM1_STATUS, &bitValue);
    if (result == DTB_SUCCESS) {
        status->alarmState = bitValue;
    }
    
    // Read auto-tuning status
    result = DTB_ReadBit(handle, BIT_AT_STATUS, &bitValue);
    if (result == DTB_SUCCESS) {
        status->autoTuning = bitValue;
    }
    
    // Read control method
    result = DTB_ReadRegister(handle, REG_CONTROL_METHOD, &value);
    if (result == DTB_SUCCESS) {
        status->controlMethod = value;
    }
    
    // Read PID mode
    result = DTB_ReadRegister(handle, REG_PID_SELECTION, &value);
    if (result == DTB_SUCCESS) {
        status->pidMode = value;
    }
    
    return DTB_SUCCESS;
}

int DTB_GetProcessValue(DTB_Handle *handle, double *temperature) {
    if (!handle || !handle->isConnected || !temperature) return DTB_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = DTB_ReadRegister(handle, REG_PROCESS_VALUE, &value);
    
    if (result == DTB_SUCCESS) {
        // Handle special error values
        switch (value) {
            case 0x8002:
                LogWarningEx(LOG_DEVICE_DTB, "Temperature not yet available (initializing)");
                *temperature = 0.0;
                return DTB_ERROR_BUSY;
            case 0x8003:
                LogErrorEx(LOG_DEVICE_DTB, "Temperature sensor not connected");
                *temperature = 0.0;
                return DTB_ERROR_RESPONSE;
            case 0x8004:
                LogErrorEx(LOG_DEVICE_DTB, "Temperature sensor input error");
                *temperature = 0.0;
                return DTB_ERROR_RESPONSE;
            default:
                *temperature = (short)value / 10.0;
                break;
        }
    }
    
    return result;
}

int DTB_GetSetPoint(DTB_Handle *handle, double *setPoint) {
    if (!handle || !handle->isConnected || !setPoint) return DTB_ERROR_INVALID_PARAM;
    
    unsigned short value;
    int result = DTB_ReadRegister(handle, REG_SET_POINT, &value);
    
    if (result == DTB_SUCCESS) {
        *setPoint = (short)value / 10.0;
    }
    
    return result;
}

int DTB_GetPIDParams(DTB_Handle *handle, int pidNumber, DTB_PIDParams *params) {
    if (!handle || !handle->isConnected || !params) return DTB_ERROR_INVALID_PARAM;
    if (pidNumber < 0 || pidNumber > 3) return DTB_ERROR_INVALID_PARAM;
    
    // Note: DTB4848 only has global PID parameters, not separate sets
    // This function reads the current active PID parameters
    
    unsigned short value;
    int result;
    
    // Read proportional band
    result = DTB_ReadRegister(handle, REG_PROPORTIONAL_BAND, &value);
    if (result == DTB_SUCCESS) {
        params->proportionalBand = value / 10.0;
    }
    
    // Read integral time
    result = DTB_ReadRegister(handle, REG_INTEGRAL_TIME, &value);
    if (result == DTB_SUCCESS) {
        params->integralTime = value;
    }
    
    // Read derivative time
    result = DTB_ReadRegister(handle, REG_DERIVATIVE_TIME, &value);
    if (result == DTB_SUCCESS) {
        params->derivativeTime = value;
    }
    
    // Read integral default
    result = DTB_ReadRegister(handle, REG_INTEGRAL_DEFAULT, &value);
    if (result == DTB_SUCCESS) {
        params->integralDefault = value / 10.0;
    }
    
    return DTB_SUCCESS;
}

/******************************************************************************
 * Alarm Functions
 ******************************************************************************/

int DTB_GetAlarmStatus(DTB_Handle *handle, int *alarmActive) {
    if (!handle || !handle->isConnected || !alarmActive) return DTB_ERROR_INVALID_PARAM;
    
    return DTB_ReadBit(handle, BIT_ALARM1_STATUS, alarmActive);
}

int DTB_ClearAlarm(DTB_Handle *handle) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    // For DTB4848, alarms auto-clear when condition is resolved
    // This function can be used to acknowledge the alarm was seen
    LogMessageEx(LOG_DEVICE_DTB, "Alarm acknowledged");
    
    return DTB_SUCCESS;
}

int DTB_SetAlarmLimits(DTB_Handle *handle, double upperLimit, double lowerLimit) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    int result;
    
    // Set upper limit
    short upperValue = (short)(upperLimit * 10);
    result = DTB_WriteRegister(handle, REG_ALARM1_UPPER, (unsigned short)upperValue);
    if (result != DTB_SUCCESS) return result;
    
    // Set lower limit
    short lowerValue = (short)(lowerLimit * 10);
    result = DTB_WriteRegister(handle, REG_ALARM1_LOWER, (unsigned short)lowerValue);
    
    return result;
}

/******************************************************************************
 * Advanced Functions
 ******************************************************************************/

int DTB_SetControlMethod(DTB_Handle *handle, int method) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    if (method < 0 || method > 3) return DTB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting control method: %d", method);
    
    return DTB_WriteRegister(handle, REG_CONTROL_METHOD, (unsigned short)method);
}

int DTB_SetPIDMode(DTB_Handle *handle, int mode) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    if (mode < 0 || mode > 4) return DTB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting PID mode: %d%s", mode, 
                 mode == 4 ? " (AUTO)" : "");
    
    return DTB_WriteRegister(handle, REG_PID_SELECTION, (unsigned short)mode);
}

int DTB_SetSensorType(DTB_Handle *handle, int sensorType) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    if (sensorType < 0 || sensorType > 17) return DTB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_DTB, "Setting sensor type: %d", sensorType);
    
    return DTB_WriteRegister(handle, REG_INPUT_SENSOR_TYPE, (unsigned short)sensorType);
}

int DTB_SetTemperatureLimits(DTB_Handle *handle, double upperLimit, double lowerLimit) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    int result;
    
    // Set upper limit
    short upperValue = (short)(upperLimit * 10);
    result = DTB_WriteRegister(handle, REG_UPPER_LIMIT_TEMP, (unsigned short)upperValue);
    if (result != DTB_SUCCESS) return result;
    
    // Set lower limit
    short lowerValue = (short)(lowerLimit * 10);
    result = DTB_WriteRegister(handle, REG_LOWER_LIMIT_TEMP, (unsigned short)lowerValue);
    
    return result;
}

/******************************************************************************
 * Low-level Modbus Functions
 ******************************************************************************/

int DTB_ReadRegister(DTB_Handle *handle, unsigned short address, unsigned short *value) {
    if (!handle || !handle->isConnected || !value) return DTB_ERROR_INVALID_PARAM;
    
    unsigned char response[16];
    int result = SendModbusASCII(handle, MODBUS_READ_REGISTERS, address, 1, 
                                response, sizeof(response));
    
    if (result == DTB_SUCCESS) {
        // Response format: Address(1) + Function(1) + ByteCount(1) + Data(2)
        if (response[2] == 2) {  // Verify byte count
            *value = (unsigned short)((response[3] << 8) | response[4]);
        } else {
            return DTB_ERROR_RESPONSE;
        }
    }
    
    return result;
}

int DTB_WriteRegister(DTB_Handle *handle, unsigned short address, unsigned short value) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    unsigned char response[16];
    return SendModbusASCII(handle, MODBUS_WRITE_REGISTER, address, value, 
                          response, sizeof(response));
}

int DTB_ReadBit(DTB_Handle *handle, unsigned short address, int *value) {
    if (!handle || !handle->isConnected || !value) return DTB_ERROR_INVALID_PARAM;
    
    unsigned char response[16];
    int result = SendModbusASCII(handle, MODBUS_READ_BITS, address, 1, 
                                response, sizeof(response));
    
    if (result == DTB_SUCCESS) {
        // Response format: Address(1) + Function(1) + ByteCount(1) + Data(1)
        if (response[2] == 1) {  // Verify byte count
            *value = (response[3] & 0x01) ? 1 : 0;
        } else {
            return DTB_ERROR_RESPONSE;
        }
    }
    
    return result;
}

int DTB_WriteBit(DTB_Handle *handle, unsigned short address, int value) {
    if (!handle || !handle->isConnected) return DTB_ERROR_NOT_CONNECTED;
    
    unsigned char response[16];
    unsigned short data = value ? 0xFF00 : 0x0000;
    return SendModbusASCII(handle, MODBUS_WRITE_BIT, address, data, 
                          response, sizeof(response));
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* DTB_GetErrorString(int errorCode) {
    if (errorCode == DTB_SUCCESS) {
        return errorStrings[0];
    }
    
    int index = 0;
    switch (errorCode) {
        case DTB_ERROR_COMM:         index = 1; break;
        case DTB_ERROR_CHECKSUM:     index = 2; break;
        case DTB_ERROR_TIMEOUT:      index = 3; break;
        case DTB_ERROR_INVALID_PARAM: index = 4; break;
        case DTB_ERROR_BUSY:         index = 5; break;
        case DTB_ERROR_NOT_CONNECTED: index = 6; break;
        case DTB_ERROR_RESPONSE:     index = 7; break;
        case DTB_ERROR_NOT_SUPPORTED: index = 8; break;
        default:
            return "Unknown DTB error";
    }
    
    if (index < sizeof(errorStrings) / sizeof(errorStrings[0])) {
        return errorStrings[index];
    }
    return "Unknown error";
}

void DTB_EnableDebugOutput(int enable) {
    debugEnabled = enable;
    if (enable) {
        LogMessageEx(LOG_DEVICE_DTB, "Debug output enabled");
    }
}

void DTB_PrintStatus(const DTB_Status *status) {
    if (!status) return;
    
    LogMessageEx(LOG_DEVICE_DTB, "=== DTB Status ===");
    LogMessageEx(LOG_DEVICE_DTB, "Process Value: %.1f °C", status->processValue);
    LogMessageEx(LOG_DEVICE_DTB, "Set Point: %.1f °C", status->setPoint);
    LogMessageEx(LOG_DEVICE_DTB, "Output: %s", status->outputEnabled ? "RUN" : "STOP");
    LogMessageEx(LOG_DEVICE_DTB, "Output 1: %s", status->output1State ? "ON" : "OFF");
    LogMessageEx(LOG_DEVICE_DTB, "Output 2: %s", status->output2State ? "ON" : "OFF");
    LogMessageEx(LOG_DEVICE_DTB, "Alarm: %s", status->alarmState ? "ACTIVE" : "OFF");
    LogMessageEx(LOG_DEVICE_DTB, "Auto-tuning: %s", status->autoTuning ? "ACTIVE" : "OFF");
    
    const char *controlMethods[] = {"PID", "ON/OFF", "Manual", "PID Program"};
    if (status->controlMethod >= 0 && status->controlMethod <= 3) {
        LogMessageEx(LOG_DEVICE_DTB, "Control Method: %s", controlMethods[status->controlMethod]);
    }
    
    LogMessageEx(LOG_DEVICE_DTB, "PID Mode: %d%s", status->pidMode,
                 status->pidMode == 4 ? " (AUTO)" : "");
    LogMessageEx(LOG_DEVICE_DTB, "==================");
}