/******************************************************************************
 * PSB 10000 Series Power Supply Control Library - Simplified Version
 * Implementation file for LabWindows/CVI
 * 
 * Configured for 60V/60A derated version
 ******************************************************************************/

#include "psb10000_dll.h"
#include "logging.h"

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

static void PrintHexDump(const char *label, unsigned char *data, int length) {
    if (!debugEnabled) return;
    
    char hexBuffer[LARGE_BUFFER_SIZE];
    char *ptr = hexBuffer;
    int remaining = sizeof(hexBuffer) - 1;
    
    int written = snprintf(ptr, remaining, "%s (%d bytes): ", label, length);
    if (written > 0 && written < remaining) {
        ptr += written;
        remaining -= written;
    }
    
    for (int i = 0; i < length && remaining > 3; i++) {
        written = snprintf(ptr, remaining, "%02X ", data[i]);
        if (written > 0 && written < remaining) {
            ptr += written;
            remaining -= written;
        }
    }
    
    LogDebugEx(LOG_DEVICE_PSB,"%s", hexBuffer);
}

static int ConvertToDeviceUnits(double realValue, double nominalValue) {
    double percentage = (realValue / nominalValue) * 100.0;
    percentage = CLAMP(percentage, 0.0, 102.0);
    return (int)((percentage / 102.0) * 53477);
}

static double ConvertFromDeviceUnits(int deviceValue, double nominalValue) {
    double percentage = ((double)deviceValue / 53477.0) * 102.0;
    return (percentage / 100.0) * nominalValue;
}

static int SendModbusCommand(PSB_Handle *handle, unsigned char *txBuffer, int txLength, 
                            unsigned char *rxBuffer, int expectedRxLength) {
    if (!handle || !handle->isConnected) {
        LogErrorEx(LOG_DEVICE_PSB,"SendModbusCommand called with invalid handle or not connected");
        return PSB_ERROR_NOT_CONNECTED;
    }
    
    // Store the function code we're sending for later verification
    unsigned char sentFunctionCode = txBuffer[1];
    
    PrintHexDump("TX", txBuffer, txLength);
    
    // Clear input buffer
    FlushInQ(handle->comPort);
    
    // Send command
    if (ComWrt(handle->comPort, (char*)txBuffer, txLength) != txLength) {
        LogErrorEx(LOG_DEVICE_PSB,"Failed to write all bytes to COM port");
        return PSB_ERROR_COMM;
    }
    
    // Wait for response - INCREASED from 50ms to 150ms
    Delay(0.15);  // 150ms delay for device processing
    
    // Read response with timeout
    int totalBytesRead = 0;
    double startTime = Timer();
    
    // First, try to read at least 5 bytes to check if it's an exception response
    int minBytesToRead = 5;
    int actualExpectedBytes = expectedRxLength;
    
    while (totalBytesRead < minBytesToRead) {
        int bytesAvailable = GetInQLen(handle->comPort);
        if (bytesAvailable > 0) {
            int bytesToRead = MIN(bytesAvailable, minBytesToRead - totalBytesRead);
            int bytesRead = ComRd(handle->comPort, (char*)&rxBuffer[totalBytesRead], bytesToRead);
            if (bytesRead > 0) {
                totalBytesRead += bytesRead;
            }
        }
        
        // Check timeout
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_PSB,"Timeout - read %d of %d bytes", totalBytesRead, minBytesToRead);
            if (totalBytesRead > 0) {
                PrintHexDump("Partial RX", rxBuffer, totalBytesRead);
            }
            return PSB_ERROR_TIMEOUT;
        }
        
        if (totalBytesRead < minBytesToRead) {
            Delay(0.05);
        }
    }
    
    // Check if this is an exception response
    if (totalBytesRead >= 2 && (rxBuffer[1] & 0x80)) {
        // This is an exception response, which is only 5 bytes total
        actualExpectedBytes = 5;
        LogDebugEx(LOG_DEVICE_PSB,"Detected Modbus exception response");
    }
    
    // Continue reading remaining bytes if needed
    while (totalBytesRead < actualExpectedBytes) {
        int bytesAvailable = GetInQLen(handle->comPort);
        if (bytesAvailable > 0) {
            int bytesToRead = MIN(bytesAvailable, actualExpectedBytes - totalBytesRead);
            int bytesRead = ComRd(handle->comPort, (char*)&rxBuffer[totalBytesRead], bytesToRead);
            if (bytesRead > 0) {
                totalBytesRead += bytesRead;
            }
        }
        
        // Check timeout
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_PSB,"Timeout - read %d of %d bytes", totalBytesRead, actualExpectedBytes);
            if (totalBytesRead > 0) {
                PrintHexDump("Partial RX", rxBuffer, totalBytesRead);
            }
            return PSB_ERROR_TIMEOUT;
        }
        
        if (totalBytesRead < actualExpectedBytes) {
            Delay(0.05);
        }
    }
    
    PrintHexDump("RX", rxBuffer, totalBytesRead);
    
    // Verify response length
    if (totalBytesRead != actualExpectedBytes) {
        LogErrorEx(LOG_DEVICE_PSB,"Wrong response length - got %d, expected %d", 
                 totalBytesRead, actualExpectedBytes);
        return PSB_ERROR_RESPONSE;
    }
    
    // Verify slave address
    if (rxBuffer[0] != handle->slaveAddress) {
        LogErrorEx(LOG_DEVICE_PSB,"Wrong slave address in response - got 0x%02X, expected 0x%02X", 
                 rxBuffer[0], handle->slaveAddress);
        return PSB_ERROR_RESPONSE;
    }
    
    // Check for Modbus exception
    if (rxBuffer[1] & 0x80) {
        unsigned char exceptionCode = rxBuffer[2];
        const char *exceptionMsg = "Unknown exception";
        
        switch(exceptionCode) {
            case 0x01: exceptionMsg = "Illegal function"; break;
            case 0x02: exceptionMsg = "Illegal data address"; break;
            case 0x03: exceptionMsg = "Illegal data value"; break;
            case 0x04: exceptionMsg = "Slave device failure"; break;
            case 0x05: exceptionMsg = "Acknowledge"; break;
            case 0x06: exceptionMsg = "Slave device busy"; break;
            case 0x07: exceptionMsg = "Negative acknowledge"; break;
            case 0x08: exceptionMsg = "Memory parity error"; break;
        }
        
        LogErrorEx(LOG_DEVICE_PSB,"Modbus exception code: 0x%02X - %s", exceptionCode, exceptionMsg);
        return PSB_ERROR_RESPONSE;
    }
    
    // CRITICAL: Verify function code matches what we sent
    if (rxBuffer[1] != sentFunctionCode) {
        LogErrorEx(LOG_DEVICE_PSB,"Function code mismatch - sent 0x%02X, received 0x%02X", 
                 sentFunctionCode, rxBuffer[1]);
        
        // Special diagnostic for common error
        if (sentFunctionCode == MODBUS_READ_HOLDING_REGISTERS && rxBuffer[1] == MODBUS_WRITE_SINGLE_REGISTER) {
            LogErrorEx(LOG_DEVICE_PSB,"Device responded with WRITE REGISTER (0x06) to READ REGISTERS (0x03) request!");
        } else if (sentFunctionCode == MODBUS_WRITE_SINGLE_COIL && rxBuffer[1] == MODBUS_READ_HOLDING_REGISTERS) {
            LogErrorEx(LOG_DEVICE_PSB,"Device responded with READ REGISTERS (0x03) to WRITE COIL (0x05) request!");
        }
        
        return PSB_ERROR_RESPONSE;
    }
    
    // Additional validation based on function code
    if (sentFunctionCode == MODBUS_READ_HOLDING_REGISTERS) {
        // For read responses, byte 2 should be the byte count
        unsigned char expectedByteCount = (unsigned char)(actualExpectedBytes - 5); // Total - Address - Function - ByteCount - CRC(2)
        if (rxBuffer[2] != expectedByteCount) {
            LogErrorEx(LOG_DEVICE_PSB,"Read response byte count mismatch - got %d, expected %d", 
                     rxBuffer[2], expectedByteCount);
            return PSB_ERROR_RESPONSE;
        }
    }
    
    // Verify CRC
    unsigned short receivedCRC = (unsigned short)((rxBuffer[totalBytesRead-1] << 8) | rxBuffer[totalBytesRead-2]);
    unsigned short calculatedCRC = PSB_CalculateCRC(rxBuffer, totalBytesRead - 2);
    
    if (receivedCRC != calculatedCRC) {
        LogErrorEx(LOG_DEVICE_PSB,"CRC mismatch - received 0x%04X, calculated 0x%04X", 
                 receivedCRC, calculatedCRC);
        return PSB_ERROR_CRC;
    }
    
    Delay(0.05);  // 50ms recovery time
    
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
    int numRates = ARRAY_SIZE(baudRates);
    
    for (int i = 0; i < numRates; i++) {
        LogDebugEx(LOG_DEVICE_PSB,"Trying COM%d at %d baud...", comPort, baudRates[i]);
        
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
                                
                                LogDebugEx(LOG_DEVICE_PSB,"Found %s, SN: %s", 
                                        result->deviceType, result->serialNumber);
                                
                                return PSB_SUCCESS;
                            }
                        }
                    }
                }
            } else if (len == 7 && response[0] == DEFAULT_SLAVE_ADDRESS && response[1] == 0x06) {
                LogWarningEx(LOG_DEVICE_PSB,"Device responded with WRITE response (0x06) to READ request during scan!");
            }
        }
        
        CloseCom(comPort);
    }
    
    return PSB_ERROR_COMM;
}

int PSB_AutoDiscover(const char *targetSerial, PSB_Handle *handle) {
    if (!handle || !targetSerial) return PSB_ERROR_INVALID_PARAM;
    
    LogMessageEx(LOG_DEVICE_PSB,"=== AUTO-DISCOVERING PSB 10000 ===");
    LogMessageEx(LOG_DEVICE_PSB,"Target serial: %s", targetSerial);
    
    SetBreakOnLibraryErrors(0);
    
    int portsToScan[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int numPorts = ARRAY_SIZE(portsToScan);
    
    for (int i = 0; i < numPorts; i++) {
        int port = portsToScan[i];
        PSB_DiscoveryResult result;
        
        LogMessageEx(LOG_DEVICE_PSB,"Scanning COM%d...", port);
        
        int scanResult = PSB_ScanPort(port, &result);
        
        if (scanResult == PSB_SUCCESS) {
            LogMessageEx(LOG_DEVICE_PSB,"  Found PSB!");
            LogMessageEx(LOG_DEVICE_PSB,"  Model: %s", result.deviceType);
            LogMessageEx(LOG_DEVICE_PSB,"  Serial: %s", result.serialNumber);
            
            if (strncmp(result.serialNumber, targetSerial, strlen(targetSerial)) == 0) {
                LogMessageEx(LOG_DEVICE_PSB,"  ? TARGET DEVICE FOUND!");
                
                SetBreakOnLibraryErrors(1);
                
                if (PSB_InitializeSpecific(handle, port, result.slaveAddress, result.baudRate) == PSB_SUCCESS) {
                    SAFE_STRCPY(handle->serialNumber, result.serialNumber, sizeof(handle->serialNumber));
                    LogMessageEx(LOG_DEVICE_PSB,"? Successfully connected to PSB %s on COM%d", targetSerial, port);
                    return PSB_SUCCESS;
                } else {
                    LogErrorEx(LOG_DEVICE_PSB,"? Found target but failed to connect");
                    return PSB_ERROR_COMM;
                }
            } else {
                LogMessageEx(LOG_DEVICE_PSB,"  ? Different device, continuing...");
            }
        } else {
            LogDebugEx(LOG_DEVICE_PSB,"  no PSB");
        }
        
        Delay(0.05);
    }
    
    SetBreakOnLibraryErrors(1);
    
    LogErrorEx(LOG_DEVICE_PSB,"? PSB with serial %s not found", targetSerial);
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
    handle->state = DEVICE_STATE_CONNECTING;
    
    LogMessageEx(LOG_DEVICE_PSB,"Initializing on COM%d, slave %d, %d baud", comPort, slaveAddress, baudRate);
    
    if (OpenComConfig(comPort, "", baudRate, 0, 8, 1, 512, 512) < 0) {
        LogErrorEx(LOG_DEVICE_PSB,"Failed to open COM%d", comPort);
        handle->state = DEVICE_STATE_ERROR;
        return PSB_ERROR_COMM;
    }
    
    SetComTime(comPort, handle->timeoutMs / 1000.0);
    
    handle->isConnected = 1;
    handle->state = DEVICE_STATE_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_PSB,"Successfully initialized");
    return PSB_SUCCESS;
}

int PSB_Close(PSB_Handle *handle) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    
    LogMessageEx(LOG_DEVICE_PSB,"Closing connection on COM%d", handle->comPort);
    
    CloseCom(handle->comPort);
    handle->isConnected = 0;
    handle->state = DEVICE_STATE_DISCONNECTED;
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting remote mode: %s", enable ? "ON" : "OFF");
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting output: %s", enable ? "ON" : "OFF");
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Voltage Control Functions
 ******************************************************************************/

int PSB_SetVoltage(PSB_Handle *handle, double voltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (voltage < 0 || voltage > PSB_NOMINAL_VOLTAGE * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid voltage %.2fV (range: 0-%.2fV)", voltage, PSB_NOMINAL_VOLTAGE * 1.02);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting voltage: %.2fV (0x%04X)", voltage, deviceValue);
    
    // Expected response for write register: Same as request = 8 bytes
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetVoltageLimits(PSB_Handle *handle, double minVoltage, double maxVoltage) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (minVoltage < 0 || maxVoltage > PSB_NOMINAL_VOLTAGE * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid voltage limits (%.2fV-%.2fV)", minVoltage, maxVoltage);
        return PSB_ERROR_INVALID_PARAM;
    }
    if (minVoltage > maxVoltage) {
        LogErrorEx(LOG_DEVICE_PSB,"Min voltage (%.2fV) > Max voltage (%.2fV)", minVoltage, maxVoltage);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting min voltage: %.2fV", minVoltage);
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting max voltage: %.2fV", maxVoltage);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Current Control Functions
 ******************************************************************************/

int PSB_SetCurrent(PSB_Handle *handle, double current) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (current < 0 || current > PSB_NOMINAL_CURRENT * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid current %.2fA (range: 0-%.2fA)", current, PSB_NOMINAL_CURRENT * 1.02);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting current: %.2fA (0x%04X)", current, deviceValue);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetCurrentLimits(PSB_Handle *handle, double minCurrent, double maxCurrent) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (minCurrent < 0 || maxCurrent > PSB_NOMINAL_CURRENT * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid current limits (%.2fA-%.2fA)", minCurrent, maxCurrent);
        return PSB_ERROR_INVALID_PARAM;
    }
    if (minCurrent > maxCurrent) {
        LogErrorEx(LOG_DEVICE_PSB,"Min current (%.2fA) > Max current (%.2fA)", minCurrent, maxCurrent);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting min current: %.2fA", minCurrent);
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting max current: %.2fA", maxCurrent);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

/******************************************************************************
 * Power Control Functions
 ******************************************************************************/

int PSB_SetPower(PSB_Handle *handle, double power) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (power < 0 || power > PSB_NOMINAL_POWER * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid power %.2fW (range: 0-%.2fW)", power, PSB_NOMINAL_POWER * 1.02);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting power: %.2fW (0x%04X)", power, deviceValue);
    
    return SendModbusCommand(handle, txBuffer, 8, rxBuffer, 8);
}

int PSB_SetPowerLimit(PSB_Handle *handle, double maxPower) {
    if (!handle || !handle->isConnected) return PSB_ERROR_NOT_CONNECTED;
    if (maxPower < 0 || maxPower > PSB_NOMINAL_POWER * 1.02) {
        LogErrorEx(LOG_DEVICE_PSB,"Invalid power limit %.2fW (range: 0-%.2fW)", maxPower, PSB_NOMINAL_POWER * 1.02);
        return PSB_ERROR_INVALID_PARAM;
    }
    
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
    
    LogMessageEx(LOG_DEVICE_PSB,"Setting max power: %.2fW", maxPower);
    
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
    
    LogDebugEx(LOG_DEVICE_PSB,"Reading Device State (Reg 505)");
    
    // Expected response: Address(1) + Function(1) + ByteCount(1) + Data(4) + CRC(2) = 9 bytes
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 9);
    
    if (result == PSB_SUCCESS) {
        // Verify this is a read response
        if (rxBuffer[1] != MODBUS_READ_HOLDING_REGISTERS) {
            LogErrorEx(LOG_DEVICE_PSB,"Expected READ response (0x03), got 0x%02X", rxBuffer[1]);
            return PSB_ERROR_RESPONSE;
        }
        
        // Extract the 32-bit state value
        // Data is in bytes 3-6 of response
        unsigned short reg505_value = (unsigned short)((rxBuffer[3] << 8) | rxBuffer[4]);  // 0x0000
        unsigned short reg506_value = (unsigned short)((rxBuffer[5] << 8) | rxBuffer[6]);  // 0x0803
        status->rawState = ((unsigned long)reg505_value << 16) | reg506_value;  // 0x00000803
        
        LogDebugEx(LOG_DEVICE_PSB,"Raw registers: [505]=0x%04X, [506]=0x%04X", reg505_value, reg506_value);
        LogDebugEx(LOG_DEVICE_PSB,"Combined 32-bit state: 0x%08lX", status->rawState);
        
        // Parse state bits
        status->controlLocation = (int)(status->rawState & STATE_CONTROL_LOCATION_MASK);
        status->outputEnabled = (status->rawState & STATE_OUTPUT_ENABLED) ? 1 : 0;
        status->regulationMode = (int)((status->rawState & STATE_REGULATION_MODE_MASK) >> 9);
        status->remoteMode = (status->rawState & STATE_REMOTE_MODE) ? 1 : 0;
        status->alarmsActive = (status->rawState & STATE_ALARMS_ACTIVE) ? 1 : 0;
        
        LogDebugEx(LOG_DEVICE_PSB,"Parsed state:");
        LogDebugEx(LOG_DEVICE_PSB,"  Control Location: 0x%02X", status->controlLocation);
        LogDebugEx(LOG_DEVICE_PSB,"  Output Enabled: %s", status->outputEnabled ? "YES" : "NO");
        LogDebugEx(LOG_DEVICE_PSB,"  Remote Mode: %s", status->remoteMode ? "YES" : "NO");
        LogDebugEx(LOG_DEVICE_PSB,"  Regulation Mode: %d", status->regulationMode);
        LogDebugEx(LOG_DEVICE_PSB,"  Alarms Active: %s", status->alarmsActive ? "YES" : "NO");
        
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
    
    LogDebugEx(LOG_DEVICE_PSB,"Reading Actual Values");
    
    // Expected response: Address(1) + Function(1) + ByteCount(1) + Data(6) + CRC(2) = 11 bytes
    int result = SendModbusCommand(handle, txBuffer, 8, rxBuffer, 11);
    
    if (result == PSB_SUCCESS) {
        // Verify this is a read response
        if (rxBuffer[1] != MODBUS_READ_HOLDING_REGISTERS) {
            LogErrorEx(LOG_DEVICE_PSB,"Expected READ response (0x03), got 0x%02X", rxBuffer[1]);
            return PSB_ERROR_RESPONSE;
        }
        
        unsigned short voltageRaw = (unsigned short)((rxBuffer[3] << 8) | rxBuffer[4]);
        unsigned short currentRaw = (unsigned short)((rxBuffer[5] << 8) | rxBuffer[6]);
        unsigned short powerRaw = (unsigned short)((rxBuffer[7] << 8) | rxBuffer[8]);
        
        if (voltage) *voltage = ConvertFromDeviceUnits(voltageRaw, PSB_NOMINAL_VOLTAGE);
        if (current) *current = ConvertFromDeviceUnits(currentRaw, PSB_NOMINAL_CURRENT);
        if (power) *power = ConvertFromDeviceUnits(powerRaw, PSB_NOMINAL_POWER);
        
        LogDebugEx(LOG_DEVICE_PSB,"Actual values: V=%.2fV, I=%.2fA, P=%.2fW",
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
    // Handle success case
    if (errorCode == PSB_SUCCESS) {
        return errorStrings[0];
    }
    
    // Map PSB error codes to array indices
    int index = 0;
    switch (errorCode) {
        case PSB_ERROR_COMM:         index = 1; break;
        case PSB_ERROR_CRC:          index = 2; break;
        case PSB_ERROR_TIMEOUT:      index = 3; break;
        case PSB_ERROR_INVALID_PARAM: index = 4; break;
        case PSB_ERROR_BUSY:         index = 5; break;
        case PSB_ERROR_NOT_CONNECTED: index = 6; break;
        case PSB_ERROR_RESPONSE:     index = 7; break;
        default:
            return "Unknown PSB error";
    }
    
    if (index < ARRAY_SIZE(errorStrings)) {
        return errorStrings[index];
    }
    return "Unknown error";
}

void PSB_EnableDebugOutput(int enable) {
    debugEnabled = enable;
    if (enable) {
        LogMessageEx(LOG_DEVICE_PSB,"Debug output enabled");
    }
}

void PSB_PrintStatus(PSB_Status *status) {
    if (!status) return;
    
    LogMessageEx(LOG_DEVICE_PSB,"=== PSB Status ===");
    LogMessageEx(LOG_DEVICE_PSB,"Voltage: %.2f V", status->voltage);
    LogMessageEx(LOG_DEVICE_PSB,"Current: %.2f A", status->current);
    LogMessageEx(LOG_DEVICE_PSB,"Power: %.2f W", status->power);
    LogMessageEx(LOG_DEVICE_PSB,"Output Enabled: %s", status->outputEnabled ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_PSB,"Remote Mode: %s", status->remoteMode ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_PSB,"Control Location: ");
    
    switch (status->controlLocation) {
        case CONTROL_FREE: LogMessageEx(LOG_DEVICE_PSB,"  FREE"); break;
        case CONTROL_LOCAL: LogMessageEx(LOG_DEVICE_PSB,"  LOCAL"); break;
        case CONTROL_USB: LogMessageEx(LOG_DEVICE_PSB,"  USB"); break;
        case CONTROL_ANALOG: LogMessageEx(LOG_DEVICE_PSB,"  ANALOG"); break;
        default: LogMessageEx(LOG_DEVICE_PSB,"  OTHER (0x%02X)", status->controlLocation); break;
    }
    
    LogMessageEx(LOG_DEVICE_PSB,"Regulation Mode: ");
    switch (status->regulationMode) {
        case 0: LogMessageEx(LOG_DEVICE_PSB,"  CV (Constant Voltage)"); break;
        case 1: LogMessageEx(LOG_DEVICE_PSB,"  CR (Constant Resistance)"); break;
        case 2: LogMessageEx(LOG_DEVICE_PSB,"  CC (Constant Current)"); break;
        case 3: LogMessageEx(LOG_DEVICE_PSB,"  CP (Constant Power)"); break;
    }
    
    LogMessageEx(LOG_DEVICE_PSB,"Alarms Active: %s", status->alarmsActive ? "YES" : "NO");
    LogMessageEx(LOG_DEVICE_PSB,"Raw State: 0x%08lX", status->rawState);
    LogMessageEx(LOG_DEVICE_PSB,"==================");
}