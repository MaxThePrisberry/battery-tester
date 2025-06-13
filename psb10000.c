/******************************************************************************
 * PSB 10000 Series Power Supply Control Library - Simplified Version
 * Implementation file for LabWindows/CVI
 * 
 * Configured for 60V/60A derated version
 ******************************************************************************/

#include "psb10000.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static int debugEnabled = 0;

static const char* errorStrings[] = {
    "Success",
    "Communication error",
    "CRC error", 
    "Timeout error",
    "Invalid parameter",
    "Device busy",
    "Not connected",
    "Invalid response"
};

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static void DebugPrint(const char *format, ...) {
    if (debugEnabled) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

static void PrintHexDump(const char *label, unsigned char *data, int length) {
    if (!debugEnabled) return;
    
    printf("%s (%d bytes): ", label, length);
    for (int i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

static int ConvertToDeviceUnits(double realValue, double nominalValue) {
    double percentage = (realValue / nominalValue) * 100.0;
    if (percentage > 102.0) percentage = 102.0;
    if (percentage < 0.0) percentage = 0.0;
    return (int)((percentage / 102.0) * 53477);
}

static double ConvertFromDeviceUnits(int deviceValue, double nominalValue) {
    double percentage = ((double)deviceValue / 53477.0) * 102.0;
    return (percentage / 100.0) * nominalValue;
}

static int SendModbusCommand(PSB_Handle *handle, unsigned char *txBuffer, int txLength, 
                            unsigned char *rxBuffer, int expectedRxLength) {
    if (!handle || !handle->isConnected) {
        return PSB_ERROR_NOT_CONNECTED;
    }
    
    // Store the function code we're sending for later verification
    unsigned char sentFunctionCode = txBuffer[1];
    
    PrintHexDump("TX", txBuffer, txLength);
    
    // Clear input buffer
    FlushInQ(handle->comPort);
    
    // Send command
    if (ComWrt(handle->comPort, (char*)txBuffer, txLength) != txLength) {
        DebugPrint("ERROR: Failed to write all bytes\n");
        return PSB_ERROR_COMM;
    }
    
    // Wait for response
    Delay(0.05);  // 50ms delay for device processing
    
    // Read response with timeout
    int totalBytesRead = 0;
    double startTime = Timer();
    
    // First, try to read at least 5 bytes to check if it's an exception response
    int minBytesToRead = 5;
    int actualExpectedBytes = expectedRxLength;
    
    while (totalBytesRead < minBytesToRead) {
        int bytesAvailable = GetInQLen(handle->comPort);
        if (bytesAvailable > 0) {
            int bytesToRead = (bytesAvailable < (minBytesToRead - totalBytesRead)) ? 
                             bytesAvailable : (minBytesToRead - totalBytesRead);
            int bytesRead = ComRd(handle->comPort, (char*)&rxBuffer[totalBytesRead], bytesToRead);
            if (bytesRead > 0) {
                totalBytesRead += bytesRead;
            }
        }
        
        // Check timeout
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            DebugPrint("ERROR: Timeout - read %d of %d bytes\n", totalBytesRead, minBytesToRead);
            if (totalBytesRead > 0) {
                PrintHexDump("Partial RX", rxBuffer, totalBytesRead);
            }
            return PSB_ERROR_TIMEOUT;
        }
        
        if (totalBytesRead < minBytesToRead) {
            Delay(0.01);  // Small delay between read attempts
        }
    }
    
    // Check if this is an exception response
    if (totalBytesRead >= 2 && (rxBuffer[1] & 0x80)) {
        // This is an exception response, which is only 5 bytes total
        actualExpectedBytes = 5;
        DebugPrint("Detected Modbus exception response\n");
    }
    
    // Continue reading remaining bytes if needed
    while (totalBytesRead < actualExpectedBytes) {
        int bytesAvailable = GetInQLen(handle->comPort);
        if (bytesAvailable > 0) {
            int bytesToRead = (bytesAvailable < (actualExpectedBytes - totalBytesRead)) ? 
                             bytesAvailable : (actualExpectedBytes - totalBytesRead);
            int bytesRead = ComRd(handle->comPort, (char*)&rxBuffer[totalBytesRead], bytesToRead);
            if (bytesRead > 0) {
                totalBytesRead += bytesRead;
            }
        }
        
        // Check timeout
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            DebugPrint("ERROR: Timeout - read %d of %d bytes\n", totalBytesRead, actualExpectedBytes);
            if (totalBytesRead > 0) {
                PrintHexDump("Partial RX", rxBuffer, totalBytesRead);
            }
            return PSB_ERROR_TIMEOUT;
        }
        
        if (totalBytesRead < actualExpectedBytes) {
            Delay(0.01);  // Small delay between read attempts
        }
    }
    
    PrintHexDump("RX", rxBuffer, totalBytesRead);
    
    // Verify response length
    if (totalBytesRead != actualExpectedBytes) {
        DebugPrint("ERROR: Wrong response length - got %d, expected %d\n", 
                   totalBytesRead, actualExpectedBytes);
        return PSB_ERROR_RESPONSE;
    }
    
    // Verify slave address
    if (rxBuffer[0] != handle->slaveAddress) {
        DebugPrint("ERROR: Wrong slave address in response - got 0x%02X, expected 0x%02X\n", 
                   rxBuffer[0], handle->slaveAddress);
        return PSB_ERROR_RESPONSE;
    }
    
    // Check for Modbus exception
    if (rxBuffer[1] & 0x80) {
        unsigned char exceptionCode = rxBuffer[2];
        DebugPrint("ERROR: Modbus exception code: 0x%02X - ", exceptionCode);
        switch(exceptionCode) {
            case 0x01: DebugPrint("Illegal function\n"); break;
            case 0x02: DebugPrint("Illegal data address\n"); break;
            case 0x03: DebugPrint("Illegal data value\n"); break;
            case 0x04: DebugPrint("Slave device failure\n"); break;
            case 0x05: DebugPrint("Acknowledge\n"); break;
            case 0x06: DebugPrint("Slave device busy\n"); break;
            case 0x07: DebugPrint("Negative acknowledge\n"); break;
            case 0x08: DebugPrint("Memory parity error\n"); break;
            default: DebugPrint("Unknown exception\n"); break;
        }
        return PSB_ERROR_RESPONSE;
    }
    
    // CRITICAL: Verify function code matches what we sent
    if (rxBuffer[1] != sentFunctionCode) {
        DebugPrint("ERROR: Function code mismatch - sent 0x%02X, received 0x%02X\n", 
                   sentFunctionCode, rxBuffer[1]);
        
        // Special diagnostic for common error
        if (sentFunctionCode == MODBUS_READ_HOLDING_REGISTERS && rxBuffer[1] == MODBUS_WRITE_SINGLE_REGISTER) {
            DebugPrint("ERROR: Device responded with WRITE REGISTER (0x06) to READ REGISTERS (0x03) request!\n");
        } else if (sentFunctionCode == MODBUS_WRITE_SINGLE_COIL && rxBuffer[1] == MODBUS_READ_HOLDING_REGISTERS) {
            DebugPrint("ERROR: Device responded with READ REGISTERS (0x03) to WRITE COIL (0x05) request!\n");
        }
        
        return PSB_ERROR_RESPONSE;
    }
    
    // Additional validation based on function code
    if (sentFunctionCode == MODBUS_READ_HOLDING_REGISTERS) {
        // For read responses, byte 2 should be the byte count
        unsigned char expectedByteCount = (unsigned char)(actualExpectedBytes - 5); // Total - Address - Function - ByteCount - CRC(2)
        if (rxBuffer[2] != expectedByteCount) {
            DebugPrint("ERROR: Read response byte count mismatch - got %d, expected %d\n", 
                       rxBuffer[2], expectedByteCount);
            return PSB_ERROR_RESPONSE;
        }
    }
    
    // Verify CRC
    unsigned short receivedCRC = (unsigned short)((rxBuffer[totalBytesRead-1] << 8) | rxBuffer[totalBytesRead-2]);
    unsigned short calculatedCRC = PSB_CalculateCRC(rxBuffer, totalBytesRead - 2);
    
    if (receivedCRC != calculatedCRC) {
        DebugPrint("ERROR: CRC mismatch - received 0x%04X, calculated 0x%04X\n", 
                   receivedCRC, calculatedCRC);
        return PSB_ERROR_CRC;
    }
    
    return PSB_SUCCESS;
}

/******************************************************************************
 * CRC Calculation
 ******************************************************************************/

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

/******************************************************************************
 * Auto-Discovery Functions
 ******************************************************************************/

int PSB_ScanPort(int comPort, PSB_DiscoveryResult *result) {
    if (!result) return PSB_ERROR_INVALID_PARAM;
    
    memset(result, 0, sizeof(PSB_DiscoveryResult));
    
    int baudRates[] = {9600, 19200, 38400, 57600, 115200};
    int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
    
    for (int i = 0; i < numRates; i++) {
        DebugPrint("Trying COM%d at %d baud...\n", comPort, baudRates[i]);
        
        SetBreakOnLibraryErrors(0);
        int portResult = OpenComConfig(comPort, "", baudRates[i], 0, 8, 1, 512, 512);
        SetBreakOnLibraryErrors(1);
        
        if (portResult < 0) {
            continue;
        }
        
        SetComTime(comPort, 1.0);
        
        // Test: Read device class (register 0)
        unsigned char cmd[8] = {DEFAULT_SLAVE_ADDRESS, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
        unsigned char response[10];
        
        unsigned short crc = PSB_CalculateCRC(cmd, 6);
        cmd[6] = (unsigned char)(crc & 0xFF);
        cmd[7] = (unsigned char)((crc >> 8) & 0xFF);
        
        FlushInQ(comPort);
        
        if (ComWrt(comPort, (char*)cmd, 8) == 8) {
            Delay(0.1);
            int len = ComRd(comPort, (char*)response, 7);
            
            // Verify it's a read response (function code 0x03)
            if (len == 7 && response[0] == DEFAULT_SLAVE_ADDRESS && response[1] == 0x03) {
                // Valid response - read device info
                
                // Read device type (registers 1-20)
                unsigned char cmd2[8] = {DEFAULT_SLAVE_ADDRESS, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00};
                unsigned char response2[50];
                
                unsigned short crc2 = PSB_CalculateCRC(cmd2, 6);
                cmd2[6] = (unsigned char)(crc2 & 0xFF);
                cmd2[7] = (unsigned char)((crc2 >> 8) & 0xFF);
                
                FlushInQ(comPort);
                
                if (ComWrt(comPort, (char*)cmd2, 8) == 8) {
                    Delay(0.1);
                    int len2 = ComRd(comPort, (char*)response2, 45);
                    
                    if (len2 == 45 && response2[0] == DEFAULT_SLAVE_ADDRESS && response2[1] == 0x03) {
                        strncpy(result->deviceType, (char*)&response2[3], 40);
                        result->deviceType[40] = '\0';
                        
                        // Read serial number (registers 151-170)
                        unsigned char cmd3[8] = {DEFAULT_SLAVE_ADDRESS, 0x03, 0x00, 0x97, 0x00, 0x14, 0x00, 0x00};
                        unsigned char response3[50];
                        
                        unsigned short crc3 = PSB_CalculateCRC(cmd3, 6);
                        cmd3[6] = (unsigned char)(crc3 & 0xFF);
                        cmd3[7] = (unsigned char)((crc3 >> 8) & 0xFF);
                        
                        FlushInQ(comPort);
                        
                        if (ComWrt(comPort, (char*)cmd3, 8) == 8) {
                            Delay(0.1);
                            int len3 = ComRd(comPort, (char*)response3, 45);
                            
                            if (len3 == 45 && response3[0] == DEFAULT_SLAVE_ADDRESS && response3[1] == 0x03) {
                                strncpy(result->serialNumber, (char*)&response3[3], 40);
                                result->serialNumber[40] = '\0';
                                
                                result->comPort = comPort;
                                result->slaveAddress = DEFAULT_SLAVE_ADDRESS;
                                result->baudRate = baudRates[i];
                                
                                CloseCom(comPort);
                                
                                DebugPrint("Found PSB: %s, SN: %s\n", 
                                          result->deviceType, result->serialNumber);
                                
                                return PSB_SUCCESS;
                            }
                        }
                    }
                }
            } else if (len == 7 && response[0] == DEFAULT_SLAVE_ADDRESS && response[1] == 0x06) {
                DebugPrint("WARNING: Device responded with WRITE response (0x06) to READ request during scan!\n");
            }
        }
        
        CloseCom(comPort);
    }
    
    return PSB_ERROR_COMM;
}

int PSB_AutoDiscover(const char *targetSerial, PSB_Handle *handle) {
    if (!handle || !targetSerial) return PSB_ERROR_INVALID_PARAM;
    
    printf("\n=== AUTO-DISCOVERING PSB 10000 ===\n");
    printf("Target serial: %s\n", targetSerial);
    
    SetBreakOnLibraryErrors(0);
    
    int portsToScan[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int numPorts = sizeof(portsToScan) / sizeof(portsToScan[0]);
    
    for (int i = 0; i < numPorts; i++) {
        int port = portsToScan[i];
        PSB_DiscoveryResult result;
        
        printf("Scanning COM%d...", port);
        fflush(stdout);
        
        int scanResult = PSB_ScanPort(port, &result);
        
        if (scanResult == PSB_SUCCESS) {
            printf(" Found PSB!\n");
            printf("  Model: %s\n", result.deviceType);
            printf("  Serial: %s\n", result.serialNumber);
            
            if (strncmp(result.serialNumber, targetSerial, strlen(targetSerial)) == 0) {
                printf("  ? TARGET DEVICE FOUND!\n\n");
                
                SetBreakOnLibraryErrors(1);
                
                if (PSB_InitializeSpecific(handle, port, result.slaveAddress, result.baudRate) == PSB_SUCCESS) {
                    strncpy(handle->serialNumber, result.serialNumber, sizeof(handle->serialNumber)-1);
                    printf("? Successfully connected to PSB %s on COM%d\n", targetSerial, port);
                    return PSB_SUCCESS;
                } else {
                    printf("? Found target but failed to connect\n");
                    return PSB_ERROR_COMM;
                }
            } else {
                printf("  ? Different device, continuing...\n");
            }
        } else {
            printf(" no PSB\n");
        }
        
        Delay(0.05);
    }
    
    SetBreakOnLibraryErrors(1);
    
    printf("\n? PSB with serial %s not found\n", targetSerial);
    return PSB_ERROR_COMM;
}

/******************************************************************************
 * Connection Functions
 ******************************************************************************/

int PSB_InitializeSpecific(PSB_Handle *handle, int comPort, int slaveAddress, int baudRate) {
    if (!handle) return PSB_ERROR_INVALID_PARAM;
    
    memset(handle, 0, sizeof(PSB_Handle));
    handle->comPort = comPort;
    handle->slaveAddress = slaveAddress;
    handle->timeoutMs = DEFAULT_TIMEOUT_MS;
    
    if (OpenComConfig(comPort, "", baudRate, 0, 8, 1, 512, 512) < 0) {
        return PSB_ERROR_COMM;
    }
    
    SetComTime(comPort, handle->timeoutMs / 1000.0);
    
    handle->isConnected = 1;
    return PSB_SUCCESS;
}

int PSB_Close(PSB_Handle *handle) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    
    CloseCom(handle->comPort);
    handle->isConnected = 0;
    return PSB_SUCCESS;
}

/******************************************************************************
 * Basic Control Functions
 ******************************************************************************/

int PSB_SetRemoteMode(PSB_Handle *handle, int enable) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_COIL;
    txBuffer[2] = (unsigned char)((REG_REMOTE_MODE >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_REMOTE_MODE & 0xFF);
    txBuffer[4] = enable ? 0xFF : 0x00;
    txBuffer[5] = enable ? 0x00 : 0x00;
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting remote mode: %s\n", enable ? "ON" : "OFF");
    
    // Expected response for write coil: Same as request = 8 bytes
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetOutputEnable(PSB_Handle *handle, int enable) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_COIL;
    txBuffer[2] = (unsigned char)((REG_DC_OUTPUT >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_DC_OUTPUT & 0xFF);
    txBuffer[4] = enable ? 0xFF : 0x00;
    txBuffer[5] = enable ? 0x00 : 0x00;
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting output: %s\n", enable ? "ON" : "OFF");
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Voltage Control Functions
 ******************************************************************************/

int PSB_SetVoltage(PSB_Handle *handle, double voltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (voltage < 0 || voltage > PSB_NOMINAL_VOLTAGE * 1.02) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int deviceValue = ConvertToDeviceUnits(voltage, PSB_NOMINAL_VOLTAGE);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_SET_VOLTAGE >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_SET_VOLTAGE & 0xFF);
    txBuffer[4] = (unsigned char)((deviceValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(deviceValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting voltage: %.2fV (0x%04X)\n", voltage, deviceValue);
    
    // Expected response for write register: Same as request = 8 bytes
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetVoltageLimits(PSB_Handle *handle, double minVoltage, double maxVoltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (minVoltage < 0 || maxVoltage > PSB_NOMINAL_VOLTAGE * 1.02) return PSB_ERROR_INVALID_PARAM;
    if (minVoltage > maxVoltage) return PSB_ERROR_INVALID_PARAM;
    
    // Set minimum voltage
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int minValue = ConvertToDeviceUnits(minVoltage, PSB_NOMINAL_VOLTAGE);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_VOLTAGE_MIN >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_VOLTAGE_MIN & 0xFF);
    txBuffer[4] = (unsigned char)((minValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(minValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting min voltage: %.2fV\n", minVoltage);
    
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
    if (result != PSB_SUCCESS) return result;
    
    // Set maximum voltage
    int maxValue = ConvertToDeviceUnits(maxVoltage, PSB_NOMINAL_VOLTAGE);
    
    txBuffer[2] = (unsigned char)((REG_VOLTAGE_MAX >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_VOLTAGE_MAX & 0xFF);
    txBuffer[4] = (unsigned char)((maxValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(maxValue & 0xFF);
    
    crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("Setting max voltage: %.2fV\n", maxVoltage);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Current Control Functions
 ******************************************************************************/

int PSB_SetCurrent(PSB_Handle *handle, double current) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (current < 0 || current > PSB_NOMINAL_CURRENT * 1.02) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int deviceValue = ConvertToDeviceUnits(current, PSB_NOMINAL_CURRENT);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_SET_CURRENT >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_SET_CURRENT & 0xFF);
    txBuffer[4] = (unsigned char)((deviceValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(deviceValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting current: %.2fA (0x%04X)\n", current, deviceValue);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetCurrentLimits(PSB_Handle *handle, double minCurrent, double maxCurrent) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (minCurrent < 0 || maxCurrent > PSB_NOMINAL_CURRENT * 1.02) return PSB_ERROR_INVALID_PARAM;
    if (minCurrent > maxCurrent) return PSB_ERROR_INVALID_PARAM;
    
    // Set minimum current
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int minValue = ConvertToDeviceUnits(minCurrent, PSB_NOMINAL_CURRENT);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_CURRENT_MIN >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_CURRENT_MIN & 0xFF);
    txBuffer[4] = (unsigned char)((minValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(minValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting min current: %.2fA\n", minCurrent);
    
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
    if (result != PSB_SUCCESS) return result;
    
    // Set maximum current
    int maxValue = ConvertToDeviceUnits(maxCurrent, PSB_NOMINAL_CURRENT);
    
    txBuffer[2] = (unsigned char)((REG_CURRENT_MAX >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_CURRENT_MAX & 0xFF);
    txBuffer[4] = (unsigned char)((maxValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(maxValue & 0xFF);
    
    crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("Setting max current: %.2fA\n", maxCurrent);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Power Control Functions
 ******************************************************************************/

int PSB_SetPower(PSB_Handle *handle, double power) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (power < 0 || power > PSB_NOMINAL_POWER * 1.02) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int deviceValue = ConvertToDeviceUnits(power, PSB_NOMINAL_POWER);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_SET_POWER_SOURCE >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_SET_POWER_SOURCE & 0xFF);
    txBuffer[4] = (unsigned char)((deviceValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(deviceValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting power: %.2fW (0x%04X)\n", power, deviceValue);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetPowerLimit(PSB_Handle *handle, double maxPower) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (maxPower < 0 || maxPower > PSB_NOMINAL_POWER * 1.02) return PSB_ERROR_INVALID_PARAM;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[8];
    
    int maxValue = ConvertToDeviceUnits(maxPower, PSB_NOMINAL_POWER);
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_WRITE_SINGLE_REGISTER;
    txBuffer[2] = (unsigned char)((REG_POWER_MAX >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_POWER_MAX & 0xFF);
    txBuffer[4] = (unsigned char)((maxValue >> 8) & 0xFF);
    txBuffer[5] = (unsigned char)(maxValue & 0xFF);
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\nSetting max power: %.2fW\n", maxPower);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Status Functions
 ******************************************************************************/

int PSB_GetStatus(PSB_Handle *handle, PSB_Status *status) {
    if (!handle || !handle->isConnected || !status) return PSB_ERROR_NOT_CONNECTED;
    
    memset(status, 0, sizeof(PSB_Status));
    
    // Read device state register (505) - 32-bit value across 2 registers
    unsigned char txBuffer[8];
    unsigned char rxBuffer[10];
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_READ_HOLDING_REGISTERS;
    txBuffer[2] = (unsigned char)((REG_DEVICE_STATE >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_DEVICE_STATE & 0xFF);
    txBuffer[4] = 0x00;  // Number of registers high byte
    txBuffer[5] = 0x02;  // Number of registers low byte (2 registers for 32-bit)
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\n=== Reading Device State (Reg 505) ===\n");
    
    // Expected response: Address(1) + Function(1) + ByteCount(1) + Data(4) + CRC(2) = 9 bytes
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 9);
    
    if (result == PSB_SUCCESS) {
        // Verify this is a read response
        if (rxBuffer[1] != MODBUS_READ_HOLDING_REGISTERS) {
            DebugPrint("ERROR: Expected READ response (0x03), got 0x%02X\n", rxBuffer[1]);
            return PSB_ERROR_RESPONSE;
        }
        
        // Extract the 32-bit state value
        // Data is in bytes 3-6 of response
        unsigned short reg505_value = (unsigned short)((rxBuffer[3] << 8) | rxBuffer[4]);  // 0x0000
		unsigned short reg506_value = (unsigned short)((rxBuffer[5] << 8) | rxBuffer[6]);  // 0x0803
		status->rawState = ((unsigned long)reg505_value << 16) | reg506_value;  // 0x00000803
        
        DebugPrint("Raw registers: [505]=0x%04X, [506]=0x%04X\n", reg505_value, reg506_value);
        DebugPrint("Combined 32-bit state: 0x%08lX\n", status->rawState);
        
        // Parse state bits
        status->controlLocation = (int)(status->rawState & STATE_CONTROL_LOCATION_MASK);
        status->outputEnabled = (status->rawState & STATE_OUTPUT_ENABLED) ? 1 : 0;
        status->regulationMode = (int)((status->rawState & STATE_REGULATION_MODE_MASK) >> 9);
        status->remoteMode = (status->rawState & STATE_REMOTE_MODE) ? 1 : 0;
        status->alarmsActive = (status->rawState & STATE_ALARMS_ACTIVE) ? 1 : 0;
        
        DebugPrint("Parsed state:\n");
        DebugPrint("  Control Location: 0x%02X\n", status->controlLocation);
        DebugPrint("  Output Enabled: %s\n", status->outputEnabled ? "YES" : "NO");
        DebugPrint("  Remote Mode: %s\n", status->remoteMode ? "YES" : "NO");
        DebugPrint("  Regulation Mode: %d\n", status->regulationMode);
        DebugPrint("  Alarms Active: %s\n", status->alarmsActive ? "YES" : "NO");
        
        // Read actual values
        return PSB_GetActualValues(handle, &status->voltage, &status->current, &status->power);
    }
    
    return result;
}

int PSB_GetActualValues(PSB_Handle *handle, double *voltage, double *current, double *power) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    
    unsigned char txBuffer[8];
    unsigned char rxBuffer[12];
    
    txBuffer[0] = (unsigned char)handle->slaveAddress;
    txBuffer[1] = MODBUS_READ_HOLDING_REGISTERS;
    txBuffer[2] = (unsigned char)((REG_ACTUAL_VOLTAGE >> 8) & 0xFF);
    txBuffer[3] = (unsigned char)(REG_ACTUAL_VOLTAGE & 0xFF);
    txBuffer[4] = 0x00;
    txBuffer[5] = 0x03;  // Read 3 registers (voltage, current, power)
    
    unsigned short crc = PSB_CalculateCRC(txBuffer, 6);
    txBuffer[6] = (unsigned char)(crc & 0xFF);
    txBuffer[7] = (unsigned char)((crc >> 8) & 0xFF);
    
    DebugPrint("\n=== Reading Actual Values ===\n");
    
    // Expected response: Address(1) + Function(1) + ByteCount(1) + Data(6) + CRC(2) = 11 bytes
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 11);
    
    if (result == PSB_SUCCESS) {
        // Verify this is a read response
        if (rxBuffer[1] != MODBUS_READ_HOLDING_REGISTERS) {
            DebugPrint("ERROR: Expected READ response (0x03), got 0x%02X\n", rxBuffer[1]);
            return PSB_ERROR_RESPONSE;
        }
        
        unsigned short voltageRaw = (unsigned short)((rxBuffer[3] << 8) | rxBuffer[4]);
        unsigned short currentRaw = (unsigned short)((rxBuffer[5] << 8) | rxBuffer[6]);
        unsigned short powerRaw = (unsigned short)((rxBuffer[7] << 8) | rxBuffer[8]);
        
        if (voltage) *voltage = ConvertFromDeviceUnits(voltageRaw, PSB_NOMINAL_VOLTAGE);
        if (current) *current = ConvertFromDeviceUnits(currentRaw, PSB_NOMINAL_CURRENT);
        if (power) *power = ConvertFromDeviceUnits(powerRaw, PSB_NOMINAL_POWER);
        
        DebugPrint("Actual values: V=%.2fV, I=%.2fA, P=%.2fW\n",
                   voltage ? *voltage : 0.0,
                   current ? *current : 0.0,
                   power ? *power : 0.0);
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

void PSB_EnableDebugOutput(int enable) {
    debugEnabled = enable;
}

void PSB_PrintStatus(PSB_Status *status) {
    if (!status) return;
    
    printf("\n=== PSB Status ===\n");
    printf("Voltage: %.2f V\n", status->voltage);
    printf("Current: %.2f A\n", status->current);
    printf("Power: %.2f W\n", status->power);
    printf("Output Enabled: %s\n", status->outputEnabled ? "YES" : "NO");
    printf("Remote Mode: %s\n", status->remoteMode ? "YES" : "NO");
    printf("Control Location: ");
    switch (status->controlLocation) {
        case CONTROL_FREE: printf("FREE\n"); break;
        case CONTROL_LOCAL: printf("LOCAL\n"); break;
        case CONTROL_USB: printf("USB\n"); break;
        case CONTROL_ANALOG: printf("ANALOG\n"); break;
        default: printf("OTHER (0x%02X)\n", status->controlLocation); break;
    }
    printf("Regulation Mode: ");
    switch (status->regulationMode) {
        case 0: printf("CV (Constant Voltage)\n"); break;
        case 1: printf("CR (Constant Resistance)\n"); break;
        case 2: printf("CC (Constant Current)\n"); break;
        case 3: printf("CP (Constant Power)\n"); break;
    }
    printf("Alarms Active: %s\n", status->alarmsActive ? "YES" : "NO");
    printf("Raw State: 0x%08lX\n", status->rawState);
    printf("==================\n");
}