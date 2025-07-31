/******************************************************************************
 * teensy_dll.c
 * 
 * Teensy Microcontroller Control Library
 * Implementation file for LabWindows/CVI
 * 
 * Protocol: Send "D<2-digit-pin><H/L>\n", Receive "<pin><0/1>\n"
 * Note: Teensy response does NOT zero-pad single-digit pins
 ******************************************************************************/

#include "teensy_dll.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/******************************************************************************
 * Static Variables
 ******************************************************************************/

static const char* libraryVersion = "1.0.0";

static const char* errorStrings[] = {
    "Success",
    "Communication error",
    "Timeout error",
    "Invalid pin number",
    "Not connected",
    "Invalid response format",
    "Verification failed",
    "Invalid parameter"
};

/******************************************************************************
 * Internal Helper Functions
 ******************************************************************************/

static void PrintDebug(const char *format, ...) {
    if (!g_debugMode) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    LogDebugEx(LOG_DEVICE_TNY, "%s", buffer);
}

static int ValidatePin(const TNY_Handle *handle, int pin) {
    if (!handle) return TNY_ERROR_INVALID_PARAM;
    
    if (pin < handle->minPin || pin > handle->maxPin) {
        LogErrorEx(LOG_DEVICE_TNY, "Pin %d out of valid range (%d-%d)", 
                   pin, handle->minPin, handle->maxPin);
        return TNY_ERROR_INVALID_PIN;
    }
    
    return TNY_SUCCESS;
}

/******************************************************************************
 * Connection Functions
 ******************************************************************************/

int TNY_Initialize(TNY_Handle *handle, int comPort, int baudRate) {
    if (!handle) return TNY_ERROR_INVALID_PARAM;
    
    // Initialize handle structure
    memset(handle, 0, sizeof(TNY_Handle));
    handle->comPort = comPort;
    handle->baudRate = (baudRate > 0) ? baudRate : TNY_DEFAULT_BAUD_RATE;
    handle->timeoutMs = TNY_DEFAULT_TIMEOUT_MS;
    handle->state = DEVICE_STATE_CONNECTING;
    handle->minPin = TNY_MIN_PIN;
    handle->maxPin = TNY_MAX_PIN;
    
    LogMessageEx(LOG_DEVICE_TNY, "Initializing Teensy on COM%d at %d baud", 
                 comPort, handle->baudRate);
    
    // Open COM port
    int result = OpenComConfig(comPort, "", handle->baudRate, 0, 8, 1, 512, 512);
    if (result < 0) {
        LogErrorEx(LOG_DEVICE_TNY, "Failed to open COM%d: error %d", comPort, result);
        handle->state = DEVICE_STATE_ERROR;
        return TNY_ERROR_COMM;
    }
    
    // Set timeout
    SetComTime(comPort, handle->timeoutMs / 1000.0);
    
    // Clear any pending data
    FlushInQ(comPort);
    FlushOutQ(comPort);
    
    handle->isConnected = 1;
    handle->state = DEVICE_STATE_CONNECTED;
    
    // Test connection
    result = TNY_TestConnection(handle);
    if (result == TNY_SUCCESS) {
        handle->state = DEVICE_STATE_READY;
        LogMessageEx(LOG_DEVICE_TNY, "Successfully connected to Teensy on COM%d", comPort);
    } else {
        LogWarningEx(LOG_DEVICE_TNY, "Connected to COM%d but test failed - check Teensy firmware", 
                     comPort);
    }
    
    return TNY_SUCCESS;
}

int TNY_Close(TNY_Handle *handle) {
    if (!handle) return TNY_ERROR_INVALID_PARAM;
    
    if (!handle->isConnected) {
        return TNY_ERROR_NOT_CONNECTED;
    }
    
    LogMessageEx(LOG_DEVICE_TNY, "Closing connection on COM%d", handle->comPort);
    
    // Close COM port
    CloseCom(handle->comPort);
    
    handle->isConnected = 0;
    handle->state = DEVICE_STATE_DISCONNECTED;
    
    return TNY_SUCCESS;
}

int TNY_TestConnection(TNY_Handle *handle) {
    if (!handle || !handle->isConnected) return TNY_ERROR_NOT_CONNECTED;
    
    PrintDebug("Testing connection by setting pin 0 low");
    
    // Set pin 13 low as a test (the LED pin)
    return TNY_SetPin(handle, 13, TNY_PIN_STATE_LOW);
}

/******************************************************************************
 * Pin Control Functions
 ******************************************************************************/

int TNY_SetPin(TNY_Handle *handle, int pin, int state) {
    if (!handle || !handle->isConnected) return TNY_ERROR_NOT_CONNECTED;
    
    // Validate pin
    int result = ValidatePin(handle, pin);
    if (result != TNY_SUCCESS) return result;
    
    // Validate state
    if (state != TNY_PIN_STATE_LOW && state != TNY_PIN_STATE_HIGH) {
        LogErrorEx(LOG_DEVICE_TNY, "Invalid pin state: %d", state);
        return TNY_ERROR_INVALID_PARAM;
    }
    
    // Format command: D<pin><H/L>
    char command[16];
    sprintf(command, "%c%02d%c", TNY_CMD_PREFIX, pin, 
            state ? TNY_PIN_HIGH : TNY_PIN_LOW);
    
    // Send command and get response
    char response[16] = {0};
    result = TNY_SendCommand(handle, command, response, sizeof(response));
    if (result != TNY_SUCCESS) {
        return result;
    }
    
    // Clean response - remove any whitespace/control characters
    int cleanLen = 0;
    char cleanResponse[16] = {0};
    for (int i = 0; i < strlen(response) && cleanLen < sizeof(cleanResponse)-1; i++) {
        if (response[i] >= '0' && response[i] <= '9') {
            cleanResponse[cleanLen++] = response[i];
        }
    }
    cleanResponse[cleanLen] = '\0';
    
    PrintDebug("Raw response: '%s', Clean response: '%s' (len=%d)", 
               response, cleanResponse, cleanLen);
    
    // Verify response format: <pin><0/1>
    // Pin can be 1 or 2 digits
    if (cleanLen < 2 || cleanLen > 3) {
        LogErrorEx(LOG_DEVICE_TNY, "Invalid response length: expected 2-3, got %d", 
                   cleanLen);
        return TNY_ERROR_INVALID_RESP;
    }
    
    // Extract pin number and state from clean response
    int respPin;
    int respState;
    
    if (cleanLen == 2) {
        // Single digit pin (0-9)
        respPin = cleanResponse[0] - '0';
        respState = (cleanResponse[1] == '1') ? 1 : 0;  // Check for '1' not 'H'
        PrintDebug("Parsed single-digit response: pin=%d, state=%d", respPin, respState);
    } else {
        // Two digit pin (10-16)
        respPin = (cleanResponse[0] - '0') * 10 + (cleanResponse[1] - '0');
        respState = (cleanResponse[2] == '1') ? 1 : 0;  // Check for '1' not 'H'
        PrintDebug("Parsed double-digit response: pin=%d, state=%d", respPin, respState);
    }
    
    // Verify pin number matches
    if (respPin != pin) {
        LogErrorEx(LOG_DEVICE_TNY, "Pin mismatch: sent %d, received %d (raw response: '%s')", 
                   pin, respPin, response);
        return TNY_ERROR_VERIFY_FAILED;
    }
    
    // Verify state matches what we set
    if (respState != state) {
        LogErrorEx(LOG_DEVICE_TNY, "State mismatch: set %d, received %d", 
                   state, respState);
        return TNY_ERROR_VERIFY_FAILED;
    }
    
    PrintDebug("Successfully set pin %d to %s", pin, state ? "HIGH" : "LOW");
    
    return TNY_SUCCESS;
}

int TNY_SetMultiplePins(TNY_Handle *handle, const int *pins, const int *states, int count) {
    if (!handle || !pins || !states || count <= 0) {
        return TNY_ERROR_INVALID_PARAM;
    }
    
    LogMessageEx(LOG_DEVICE_TNY, "Setting %d pins", count);
    
    int errors = 0;
    for (int i = 0; i < count; i++) {
        int result = TNY_SetPin(handle, pins[i], states[i]);
        if (result != TNY_SUCCESS) {
            LogWarningEx(LOG_DEVICE_TNY, "Failed to set pin %d: %s", 
                        pins[i], TNY_GetErrorString(result));
            errors++;
        }
    }
    
    if (errors > 0) {
        LogWarningEx(LOG_DEVICE_TNY, "Completed with %d errors out of %d pins", 
                     errors, count);
        return TNY_ERROR_COMM;
    }
    
    return TNY_SUCCESS;
}

/******************************************************************************
 * Low-level Communication Function
 ******************************************************************************/

int TNY_SendCommand(TNY_Handle *handle, const char *command, char *response, int responseSize) {
    if (!handle || !handle->isConnected || !command || !response) {
        return TNY_ERROR_INVALID_PARAM;
    }
    
    // Build complete command with LF
    char cmdBuffer[32];
    sprintf(cmdBuffer, "%s\n", command);
    int cmdLen = strlen(cmdBuffer);
    
    PrintDebug("TX: %s", command);
    
    // Clear input buffer before sending
    FlushInQ(handle->comPort);
    
    // Send command
    int bytesWritten = ComWrt(handle->comPort, cmdBuffer, cmdLen);
    if (bytesWritten != cmdLen) {
        LogErrorEx(LOG_DEVICE_TNY, "Failed to write command: wrote %d of %d bytes", 
                   bytesWritten, cmdLen);
        return TNY_ERROR_COMM;
    }
    
    // Wait for response
    Delay(TNY_RESPONSE_DELAY_MS / 1000.0);
    
    // Read response
    char rxBuffer[32] = {0};
    int totalRead = 0;
    double startTime = Timer();
    
    // Read until we get LF or timeout
    while (totalRead < sizeof(rxBuffer) - 1) {
        int available = GetInQLen(handle->comPort);
        if (available > 0) {
            int toRead = MIN(available, sizeof(rxBuffer) - totalRead - 1);
            int bytesRead = ComRd(handle->comPort, &rxBuffer[totalRead], toRead);
            if (bytesRead > 0) {
                totalRead += bytesRead;
                
                // Check for LF
                if (totalRead >= 1 && rxBuffer[totalRead-1] == '\n') {
                    break;
                }
            }
        }
        
        // Check timeout
        if ((Timer() - startTime) > (handle->timeoutMs / 1000.0)) {
            LogErrorEx(LOG_DEVICE_TNY, "Timeout waiting for response (got %d bytes)", 
                       totalRead);
            return TNY_ERROR_TIMEOUT;
        }
        
        Delay(0.001);  // 1ms polling interval
    }
    
    rxBuffer[totalRead] = '\0';
    
    // Remove LF from response
    if (totalRead >= 1 && rxBuffer[totalRead-1] == '\n') {
        rxBuffer[totalRead-1] = '\0';
        totalRead -= 1;
    }
    
    PrintDebug("RX: %s", rxBuffer);
    
    // Copy response (without CR+LF)
    if (totalRead >= responseSize) {
        LogErrorEx(LOG_DEVICE_TNY, "Response too large for buffer");
        return TNY_ERROR_INVALID_RESP;
    }
    
    strcpy(response, rxBuffer);
    
    return TNY_SUCCESS;
}

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

const char* TNY_GetErrorString(int errorCode) {
    if (errorCode == TNY_SUCCESS) {
        return errorStrings[0];
    }
    
    int index = 0;
    switch (errorCode) {
        case TNY_ERROR_COMM:          index = 1; break;
        case TNY_ERROR_TIMEOUT:       index = 2; break;
        case TNY_ERROR_INVALID_PIN:   index = 3; break;
        case TNY_ERROR_NOT_CONNECTED: index = 4; break;
        case TNY_ERROR_INVALID_RESP:  index = 5; break;
        case TNY_ERROR_VERIFY_FAILED: index = 6; break;
        case TNY_ERROR_INVALID_PARAM: index = 7; break;
        default:
            return "Unknown Teensy error";
    }
    
    if (index < sizeof(errorStrings) / sizeof(errorStrings[0])) {
        return errorStrings[index];
    }
    return "Unknown error";
}

const char* TNY_GetVersion(void) {
    return libraryVersion;
}

int TNY_IsConnected(const TNY_Handle *handle) {
    return (handle && handle->isConnected) ? 1 : 0;
}