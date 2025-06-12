/******************************************************************************
 * PSB 10000 Series Power Supply Control Library
 * Implementation file for LabWindows/CVI with Auto-Discovery
 * 
 * This library provides functions to control PSB 10000 series power supplies
 * via Modbus RTU communication protocol, including automatic port scanning.
 * 
 * Author: Generated for LabWindows/CVI
 * Date: 2025
 ******************************************************************************/

#include "psb10000.h"
#include <string.h>

/******************************************************************************
 * Static Variables and Internal Functions
 ******************************************************************************/

static const char* errorStrings[] = {
    "Success",
    "Communication error",
    "CRC error", 
    "Timeout error",
    "Invalid parameter",
    "Device busy"
};

// Internal helper to send Modbus command and receive response
static int PSB_SendModbusCommand(PSB_Handle *handle, unsigned char *txBuffer, int txLength, 
                                unsigned char *rxBuffer, int expectedRxLength);

/******************************************************************************
 * Auto-Discovery Functions
 ******************************************************************************/

int PSB_ScanPort(int comPort, PSB_DiscoveryResult *result) {
    if (!result) return PSB_ERROR_INVALID_PARAM;
    
    // Clear result structure
    memset(result, 0, sizeof(PSB_DiscoveryResult));
    
    // Test different baud rates
    int baudRates[] = {9600, 19200, 38400, 57600, 115200};
    int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
    
    for (int i = 0; i < numRates; i++) {
        // Disable CVI error popups temporarily
        SetBreakOnLibraryErrors(0);
        
        // Try to open port at this baud rate
        int portResult = OpenComConfig(comPort, "", baudRates[i], 0, 8, 1, 512, 512);
        
        // Re-enable CVI error popups
        SetBreakOnLibraryErrors(1);
        
        if (portResult < 0) {
            // Port doesn't exist or can't be opened - this is normal
            continue; // Try next baud rate or return error
        }
        
        SetComTime(comPort, 1.0);  // 1 second timeout
        
        // Test 1: Read device class (register 0) to verify it's a PSB
        unsigned char cmd1[8] = {1, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
        unsigned char response1[10];
        
        // Calculate CRC
        unsigned short crc1 = PSB_CalculateCRC(cmd1, 6);
        cmd1[6] = crc1 & 0xFF;
        cmd1[7] = (crc1 >> 8) & 0xFF;
        
        FlushInQ(comPort);
        
        if (ComWrt(comPort, (char*)cmd1, 8) == 8) {
            Delay(0.1);
            int len1 = ComRd(comPort, (char*)response1, 10);
            
            if (len1 == 7 && response1[0] == 1 && response1[1] == 0x03 && response1[2] == 0x02) {
                // Valid response! Now read device info
                
                // Read device type (registers 1-20)
                unsigned char cmd2[8] = {1, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00};
                unsigned char response2[50];
                
                unsigned short crc2 = PSB_CalculateCRC(cmd2, 6);
                cmd2[6] = crc2 & 0xFF;
                cmd2[7] = (crc2 >> 8) & 0xFF;
                
                FlushInQ(comPort);
                
                if (ComWrt(comPort, (char*)cmd2, 8) == 8) {
                    Delay(0.1);
                    int len2 = ComRd(comPort, (char*)response2, 50);
                    
                    if (len2 == 45 && response2[0] == 1 && response2[1] == 0x03 && response2[2] == 0x28) {
                        // Extract device type
                        strncpy(result->deviceType, (char*)&response2[3], 40);
                        result->deviceType[40] = '\0';
                        
                        // Read serial number (registers 151-170)
                        unsigned char cmd3[8] = {1, 0x03, 0x00, 0x97, 0x00, 0x14, 0x00, 0x00};
                        unsigned char response3[50];
                        
                        unsigned short crc3 = PSB_CalculateCRC(cmd3, 6);
                        cmd3[6] = crc3 & 0xFF;
                        cmd3[7] = (crc3 >> 8) & 0xFF;
                        
                        FlushInQ(comPort);
                        
                        if (ComWrt(comPort, (char*)cmd3, 8) == 8) {
                            Delay(0.1);
                            int len3 = ComRd(comPort, (char*)response3, 50);
                            
                            if (len3 == 45 && response3[0] == 1 && response3[1] == 0x03 && response3[2] == 0x28) {
                                // Extract serial number
                                strncpy(result->serialNumber, (char*)&response3[3], 40);
                                result->serialNumber[40] = '\0';
                                
                                // Store connection info
                                result->comPort = comPort;
                                result->slaveAddress = 1;
                                result->baudRate = baudRates[i];
                                
                                CloseCom(comPort);
                                return PSB_SUCCESS;
                            }
                        }
                    }
                }
            }
        }
        
        CloseCom(comPort);
    }
    
    // If we get here, no PSB was found on any baud rate
    return PSB_ERROR_COMM;  // No PSB found on this port
}

int PSB_AutoDiscover(PSB_DiscoveryResult *devices, int maxDevices, int *foundCount) {
    if (!devices || !foundCount || maxDevices <= 0) return PSB_ERROR_INVALID_PARAM;
    
    *foundCount = 0;
    
    // Scan COM ports 1-20
    for (int port = 1; port <= 20 && *foundCount < maxDevices; port++) {
        PSB_DiscoveryResult result;
        
        if (PSB_ScanPort(port, &result) == PSB_SUCCESS) {
            // Copy result to output array
            devices[*foundCount] = result;
            (*foundCount)++;
        }
        
        // Small delay between port scans
        Delay(0.05);
    }
    
    return (*foundCount > 0) ? PSB_SUCCESS : PSB_ERROR_COMM;
}

/******************************************************************************
 * Initialization and Communication Functions
 ******************************************************************************/

int PSB_Initialize(PSB_Handle *handle, int comPort, int slaveAddress) {
    return PSB_InitializeSpecific(handle, comPort, slaveAddress, 9600);
}

int PSB_InitializeSpecific(PSB_Handle *handle, int comPort, int slaveAddress, int baudRate) {
    if (!handle) return PSB_ERROR_INVALID_PARAM;
    
    // Initialize structure
    memset(handle, 0, sizeof(PSB_Handle));
    handle->comPort = comPort;
    handle->slaveAddress = slaveAddress;
    handle->timeoutMs = DEFAULT_TIMEOUT_MS;
    
    // Configure serial port for Modbus RTU over USB
    if (OpenComConfig(comPort, "", baudRate, 0, 8, 1, 512, 512) < 0) {
        return PSB_ERROR_COMM;
    }
    
    // Set timeouts
    SetComTime(comPort, 1.0);  // 1 second timeout
    
    handle->isConnected = 1;
    return PSB_SUCCESS;
}

int PSB_Close(PSB_Handle *handle) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    CloseCom(handle->comPort);
    handle->isConnected = 0;
    return PSB_SUCCESS;
}

int PSB_SetTimeout(PSB_Handle *handle, int timeoutMs) {
    if (!handle) return PSB_ERROR_INVALID_PARAM;
    
    handle->timeoutMs = timeoutMs;
    SetComTime(handle->comPort, timeoutMs / 1000.0);
    return PSB_SUCCESS;
}

/******************************************************************************
 * Basic Control Functions
 ******************************************************************************/

int PSB_SetRemoteMode(PSB_Handle *handle, int enable) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    int value = enable ? COIL_ON : COIL_OFF;
    return PSB_WriteSingleCoil(handle, REG_REMOTE_MODE, value);
}

int PSB_SetOutputEnable(PSB_Handle *handle, int enable) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    int value = enable ? COIL_ON : COIL_OFF;
    return PSB_WriteSingleCoil(handle, REG_DC_OUTPUT, value);
}

int PSB_SetOperationMode(PSB_Handle *handle, int mode) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    // mode: 0 = UIP (voltage priority), 1 = UIR (current priority)
    int value = (mode == 0) ? MODE_UIP : MODE_UIR;
    return PSB_WriteSingleCoil(handle, REG_OPERATION_MODE, value);
}

/******************************************************************************
 * Voltage and Current Control Functions
 ******************************************************************************/

int PSB_SetVoltage(PSB_Handle *handle, double voltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    if (voltage < 0) return PSB_ERROR_INVALID_PARAM;
    
    // Convert voltage to device units (0-102% range, 0xD0E5 = 102%)
    // Assuming nominal voltage from device specifications
    int deviceValue = PSB_ConvertToDeviceUnits(voltage, 80.0);  // Assuming 80V nominal
    
    return PSB_WriteSingleRegister(handle, REG_SET_VOLTAGE, deviceValue);
}

int PSB_SetCurrent(PSB_Handle *handle, double current) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    if (current < 0) return PSB_ERROR_INVALID_PARAM;
    
    // Convert current to device units (0-102% range, 0xD0E5 = 102%)
    // Assuming nominal current from device specifications
    int deviceValue = PSB_ConvertToDeviceUnits(current, 1000.0);  // Assuming 1000A nominal
    
    return PSB_WriteSingleRegister(handle, REG_SET_CURRENT, deviceValue);
}

int PSB_GetActualValues(PSB_Handle *handle, PSB_Status *status) {
    if (!handle || !handle->isConnected || !status) return PSB_ERROR_INVALID_PARAM;
    
    unsigned short registers[3];
    int result = PSB_ReadHoldingRegisters(handle, REG_ACTUAL_VOLTAGE, 3, registers);
    
    if (result == PSB_SUCCESS) {
        // Convert from device units to real values
        status->voltage = PSB_ConvertFromDeviceUnits(registers[0], 80.0);   // Assuming 80V nominal
        status->current = PSB_ConvertFromDeviceUnits(registers[1], 1000.0); // Assuming 1000A nominal  
        status->power = PSB_ConvertFromDeviceUnits(registers[2], 30000.0);  // Assuming 30kW nominal
    }
    
    return result;
}

/******************************************************************************
 * Limit Configuration Functions
 ******************************************************************************/

int PSB_SetVoltageLimits(PSB_Handle *handle, double minVoltage, double maxVoltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    if (minVoltage < 0 || maxVoltage < minVoltage) return PSB_ERROR_INVALID_PARAM;
    
    int minValue = PSB_ConvertToDeviceUnits(minVoltage, 80.0);
    int maxValue = PSB_ConvertToDeviceUnits(maxVoltage, 80.0);
    
    int result = PSB_WriteSingleRegister(handle, REG_VOLTAGE_MIN, minValue);
    if (result == PSB_SUCCESS) {
        result = PSB_WriteSingleRegister(handle, REG_VOLTAGE_MAX, maxValue);
    }
    
    return result;
}

int PSB_SetCurrentLimits(PSB_Handle *handle, double minCurrent, double maxCurrent) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    if (minCurrent < 0 || maxCurrent < minCurrent) return PSB_ERROR_INVALID_PARAM;
    
    int minValue = PSB_ConvertToDeviceUnits(minCurrent, 1000.0);
    int maxValue = PSB_ConvertToDeviceUnits(maxCurrent, 1000.0);
    
    int result = PSB_WriteSingleRegister(handle, REG_CURRENT_MIN, minValue);
    if (result == PSB_SUCCESS) {
        result = PSB_WriteSingleRegister(handle, REG_CURRENT_MAX, maxValue);
    }
    
    return result;
}

int PSB_SetPowerLimit(PSB_Handle *handle, double maxPower) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    if (maxPower < 0) return PSB_ERROR_INVALID_PARAM;
    
    int value = PSB_ConvertToDeviceUnits(maxPower, 30000.0);
    return PSB_WriteSingleRegister(handle, REG_POWER_MAX, value);
}

int PSB_GetLimits(PSB_Handle *handle, PSB_Limits *limits) {
    if (!handle || !handle->isConnected || !limits) return PSB_ERROR_INVALID_PARAM;
    
    unsigned short registers[5];
    int result = PSB_ReadHoldingRegisters(handle, REG_VOLTAGE_MAX, 5, registers);
    
    if (result == PSB_SUCCESS) {
        limits->maxVoltage = PSB_ConvertFromDeviceUnits(registers[0], 80.0);
        limits->minVoltage = PSB_ConvertFromDeviceUnits(registers[1], 80.0);
        limits->maxCurrent = PSB_ConvertFromDeviceUnits(registers[2], 1000.0);
        limits->minCurrent = PSB_ConvertFromDeviceUnits(registers[3], 1000.0);
        limits->maxPower = PSB_ConvertFromDeviceUnits(registers[4], 30000.0);
    }
    
    return result;
}

/******************************************************************************
 * Status and Monitoring Functions
 ******************************************************************************/

int PSB_GetDeviceStatus(PSB_Handle *handle, PSB_Status *status) {
    if (!handle || !handle->isConnected || !status) return PSB_ERROR_INVALID_PARAM;
    
    unsigned short deviceState[2];
    int result = PSB_ReadHoldingRegisters(handle, REG_DEVICE_STATE, 2, deviceState);
    
    if (result == PSB_SUCCESS) {
        // Parse device state bits (see register 505 documentation)
        unsigned long state = ((unsigned long)deviceState[1] << 16) | deviceState[0];
        
        // Extract the key status bits according to manual
        status->outputEnabled = (state & 0x80) ? 1 : 0;        // Bit 7: Output state
        status->remoteMode = (state & 0x800) ? 1 : 0;          // Bit 11: Remote mode
        status->operationMode = (state & 0x600) >> 9;          // Bits 9-10: Regulation mode
        
        // Get actual values as well
        PSB_GetActualValues(handle, status);
    }
    
    return result;
}

int PSB_IsOutputEnabled(PSB_Handle *handle, int *enabled) {
    if (!handle || !handle->isConnected || !enabled) return PSB_ERROR_INVALID_PARAM;
    
    PSB_Status status;
    int result = PSB_GetDeviceStatus(handle, &status);
    
    if (result == PSB_SUCCESS) {
        *enabled = status.outputEnabled;
    }
    
    return result;
}

int PSB_IsRemoteModeActive(PSB_Handle *handle, int *active) {
    if (!handle || !handle->isConnected || !active) return PSB_ERROR_INVALID_PARAM;
    
    PSB_Status status;
    int result = PSB_GetDeviceStatus(handle, &status);
    
    if (result == PSB_SUCCESS) {
        *active = status.remoteMode;
    }
    
    return result;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* PSB_GetErrorString(int errorCode) {
    int index = -errorCode;
    if (index >= 0 && index < (sizeof(errorStrings) / sizeof(errorStrings[0]))) {
        return errorStrings[index];
    }
    return "Unknown error";
}

int PSB_ConvertToDeviceUnits(double realValue, double nominalValue) {
    // Convert real value to device units (0-102% range)
    // 0xD0E5 (53477) represents 102%
    double percentage = (realValue / nominalValue) * 100.0;
    if (percentage > 102.0) percentage = 102.0;
    if (percentage < 0.0) percentage = 0.0;
    
    return (int)((percentage / 102.0) * 53477);
}

double PSB_ConvertFromDeviceUnits(int deviceValue, double nominalValue) {
    // Convert device units to real value
    double percentage = ((double)deviceValue / 53477.0) * 102.0;
    return (percentage / 100.0) * nominalValue;
}

/******************************************************************************
 * Low-level Modbus Functions
 ******************************************************************************/

int PSB_ReadHoldingRegisters(PSB_Handle *handle, int startAddr, int numRegs, unsigned short *data) {
    if (!handle || !handle->isConnected || !data) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[256];
    
    // Build Modbus RTU frame
    txBuffer[0] = handle->slaveAddress;
    txBuffer[1] = MODBUS_READ_HOLDING_REGISTERS;
    txBuffer[2] = (startAddr >> 8) & 0xFF;
    txBuffer[3] = startAddr & 0xFF;
    txBuffer[4] = (numRegs >> 8) & 0xFF;
    txBuffer[5] = numRegs & 0xFF;
    
    // Calculate CRC
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = crc & 0xFF;
    txBuffer[7] = (crc >> 8) & 0xFF;
    
    int expectedRxLength = 5 + (numRegs * 2);
    int result = PSB_SendModbusCommand(handle, txBuffer, 8, rxBuffer, expectedRxLength);
    
    if (result == PSB_SUCCESS) {
        // Extract data from response
        for (int i = 0; i < numRegs; i++) {
            data[i] = (rxBuffer[3 + i*2] << 8) | rxBuffer[4 + i*2];
        }
    }
    
    return result;
}

int PSB_WriteSingleCoil(PSB_Handle *handle, int address, int value) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    // Build Modbus RTU frame
    txBuffer[0] = handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_COIL;
    txBuffer[2] = (address >> 8) & 0xFF;
    txBuffer[3] = address & 0xFF;
    txBuffer[4] = (value >> 8) & 0xFF;
    txBuffer[5] = value & 0xFF;
    
    // Calculate CRC
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = crc & 0xFF;
    txBuffer[7] = (crc >> 8) & 0xFF;
    
    return PSB_SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_WriteSingleRegister(PSB_Handle *handle, int address, int value) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    // Build Modbus RTU frame
    txBuffer[0] = handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (address >> 8) & 0xFF;
    txBuffer[3] = address & 0xFF;
    txBuffer[4] = (value >> 8) & 0xFF;
    txBuffer[5] = value & 0xFF;
    
    // Calculate CRC
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = crc & 0xFF;
    txBuffer[7] = (crc >> 8) & 0xFF;
    
    return PSB_SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_WriteMultipleRegisters(PSB_Handle *handle, int startAddr, int numRegs, unsigned short *data) {
    if (!handle || !handle->isConnected || !data) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[256];
    unsigned char rxBuffer[8];
    
    int byteCount = numRegs * 2;
    
    // Build Modbus RTU frame
    txBuffer[0] = handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_MULTIPLE_REGISTERS;
    txBuffer[2] = (startAddr >> 8) & 0xFF;
    txBuffer[3] = startAddr & 0xFF;
    txBuffer[4] = (numRegs >> 8) & 0xFF;
    txBuffer[5] = numRegs & 0xFF;
    txBuffer[6] = byteCount;
    
    // Add data
    for (int i = 0; i < numRegs; i++) {
        txBuffer[7 + i*2] = (data[i] >> 8) & 0xFF;
        txBuffer[8 + i*2] = data[i] & 0xFF;
    }
    
    // Calculate CRC
    int frameLength = 7 + byteCount;
    unsigned short crc = PSB_CalculateCRC(txBuffer, frameLength);
    txBuffer[frameLength] = crc & 0xFF;
    txBuffer[frameLength + 1] = (crc >> 8) & 0xFF;
    
    return PSB_SendModbusCommand(handle, txBuffer, frameLength + 2, rxBuffer, 8);
}

static int PSB_SendModbusCommand(PSB_Handle *handle, unsigned char *txBuffer, int txLength, 
                                unsigned char *rxBuffer, int expectedRxLength) {
    if (!handle || !handle->isConnected) return PSB_ERROR_INVALID_PARAM;
    
    // Flush input buffer
    FlushInQ(handle->comPort);
    
    // Send command
    if (ComWrt(handle->comPort, (char*)txBuffer, txLength) != txLength) {
        return PSB_ERROR_COMM;
    }
    
    // Wait for response
    Delay(0.01);  // Small delay for device processing
    
    // Read response
    int bytesRead = ComRdTerm(handle->comPort, (char*)rxBuffer, expectedRxLength, 0x0A);
    if (bytesRead < expectedRxLength) {
        // Try to read remaining bytes
        int remainingBytes = expectedRxLength - bytesRead;
        int additionalBytes = ComRd(handle->comPort, (char*)&rxBuffer[bytesRead], remainingBytes);
        bytesRead += additionalBytes;
    }
    
    if (bytesRead != expectedRxLength) {
        return PSB_ERROR_TIMEOUT;
    }
    
    // Verify CRC
    unsigned short receivedCRC = rxBuffer[bytesRead-1] << 8 | rxBuffer[bytesRead-2];
    unsigned short calculatedCRC = PSB_CalculateCRC(rxBuffer, bytesRead - 2);
    
    if (receivedCRC != calculatedCRC) {
        return PSB_ERROR_CRC;
    }
    
    return PSB_SUCCESS;
}

unsigned short PSB_CalculateCRC(unsigned char *data, int length) {
    unsigned short crc = MODBUS_CRC_INIT;
    
    for (int i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}