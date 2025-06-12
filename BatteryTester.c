/******************************************************************************
 * Simple Battery Tester for PSB 10000 Power Supply
 * LabWindows/CVI Application with Auto-Discovery
 ******************************************************************************/

#include <ansi_c.h>
#include <cvirte.h>
#include <userint.h>
#include <utility.h>
#include "BatteryTester.h"  
#include "psb10000.h"       

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
int CVICALLBACK TestButtonCallback(int panel, int control, int event, void *callbackData,
                                   int eventData1, int eventData2);
void CVICALLBACK UpdateUI(void *callbackData);
int CVICALLBACK UpdateThread(void *functionData);
int CVICALLBACK PSBDiscoveryThread(void *functionData);
void TestBasicCommunication(void);
void UpdateStatus(const char* message);
int AutoDiscoverPSB(void);
int ManualConnectPSB(int comPort);  // Fallback function

/******************************************************************************
 * Global Variables
 ******************************************************************************/
static int panelHandle = 0;
static PSB_Handle psb;
static int connected = 0;
static int discoveryComplete = 0;
static int remoteToggleInitialized = 0;  // Flag to initialize toggle once
static CmtThreadPoolHandle threadPoolHandle = 0;
static CmtThreadFunctionID updateThreadID = 0;
static CmtThreadFunctionID discoveryThreadID = 0;

// Target PSB serial number to find
static const char TARGET_SERIAL[] = "2872380001";

/******************************************************************************
 * Status Update Function (thread-safe)
 ******************************************************************************/
void UpdateStatus(const char* message) {
    if (panelHandle > 0) {
        SetCtrlVal(panelHandle, PANEL_STRING_STATUS, message);
        ProcessSystemEvents(); // Allow UI to update
    }
}

/******************************************************************************
 * PSB Discovery Thread Function
 ******************************************************************************/
int CVICALLBACK PSBDiscoveryThread(void *functionData) {
    UpdateStatus("Initializing PSB discovery...");
    Delay(0.5);
    
    UpdateStatus("Searching for PSB devices...");
    
    // Disable CVI error popups during scanning
    SetBreakOnLibraryErrors(0);
    
    // Scan common COM ports
    int portsToScan[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int numPorts = sizeof(portsToScan) / sizeof(portsToScan[0]);
    
    for (int i = 0; i < numPorts; i++) {
        int port = portsToScan[i];
        
        char statusMsg[100];
        snprintf(statusMsg, sizeof(statusMsg), "Scanning COM%d...", port);
        UpdateStatus(statusMsg);
        
        // Try to find PSB on this port
        PSB_DiscoveryResult result;
        int scanResult = PSB_ScanPort(port, &result);
        
        if (scanResult == PSB_SUCCESS) {
            snprintf(statusMsg, sizeof(statusMsg), "Found PSB on COM%d: %s", port, result.deviceType);
            UpdateStatus(statusMsg);
            
            // Check if this is our target device
            if (strncmp(result.serialNumber, TARGET_SERIAL, strlen(TARGET_SERIAL)) == 0) {
                UpdateStatus("Target PSB found! Connecting...");
                
                // Re-enable CVI error popups
                SetBreakOnLibraryErrors(1);
                
                // Initialize connection to this specific device
                if (PSB_InitializeSpecific(&psb, port, 1, result.baudRate) == PSB_SUCCESS) {
                    snprintf(statusMsg, sizeof(statusMsg), "Connected to PSB %s on COM%d", TARGET_SERIAL, port);
                    UpdateStatus(statusMsg);
                    connected = 1;
                    discoveryComplete = 1;
                    
                    // Start the monitoring thread now that we're connected
                    if (threadPoolHandle > 0) {
                        CmtScheduleThreadPoolFunction(threadPoolHandle, UpdateThread, NULL, &updateThreadID);
                    }
                    
                    return 0; // Success
                } else {
                    UpdateStatus("Found target PSB but connection failed");
                    SetBreakOnLibraryErrors(1);
                    discoveryComplete = 1;
                    return 0;
                }
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "Found different PSB (SN: %.10s...), continuing...", result.serialNumber);
                UpdateStatus(statusMsg);
                Delay(1.0); // Show message briefly
            }
        }
        
        // Small delay between port scans
        Delay(0.1);
    }
    
    // Try fallback to COM3 if auto-discovery failed
    UpdateStatus("Auto-discovery failed. Trying COM3...");
    Delay(0.5);
    
    PSB_DiscoveryResult result;
    int scanResult = PSB_ScanPort(3, &result);
    
    if (scanResult == PSB_SUCCESS) {
        char statusMsg[100];
        snprintf(statusMsg, sizeof(statusMsg), "Found PSB on COM3: %s", result.deviceType);
        UpdateStatus(statusMsg);
        
        if (PSB_InitializeSpecific(&psb, 3, 1, result.baudRate) == PSB_SUCCESS) {
            UpdateStatus("Connected to PSB on COM3 (Manual fallback)");
            connected = 1;
            discoveryComplete = 1;
            
            // Start the monitoring thread
            if (threadPoolHandle > 0) {
                CmtScheduleThreadPoolFunction(threadPoolHandle, UpdateThread, NULL, &updateThreadID);
            }
            
            // Re-enable CVI error popups
            SetBreakOnLibraryErrors(1);
            return 0;
        }
    }
    
    // Re-enable CVI error popups
    SetBreakOnLibraryErrors(1);
    
    UpdateStatus("No PSB found. Check connections and power.");
    discoveryComplete = 1;
    return 0;
}
int AutoDiscoverPSB(void) {
    printf("\n=== AUTO-DISCOVERING PSB 10000 ===\n");
    printf("Searching for PSB with serial number: %s\n", TARGET_SERIAL);
    printf("Scanning common COM ports...\n\n");
    
    // Disable CVI error popups during scanning
    SetBreakOnLibraryErrors(0);
    
    // Scan common COM ports (1-10 should cover most systems)
    // We'll check if each port exists before trying to scan it
    int portsToScan[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    int numPorts = sizeof(portsToScan) / sizeof(portsToScan[0]);
    
    for (int i = 0; i < numPorts; i++) {
        int port = portsToScan[i];
        printf("Checking COM%d...", port);
        
        // Try to find PSB on this port
        PSB_DiscoveryResult result;
        int scanResult = PSB_ScanPort(port, &result);
        
        if (scanResult == PSB_SUCCESS) {
            printf(" FOUND PSB!\n");
            printf("  Model: %s\n", result.deviceType);
            printf("  Serial: %s\n", result.serialNumber);
            printf("  Baud: %d\n", result.baudRate);
            
            // Check if this is our target device
            if (strncmp(result.serialNumber, TARGET_SERIAL, strlen(TARGET_SERIAL)) == 0) {
                printf("  ? TARGET DEVICE FOUND!\n\n");
                
                // Re-enable CVI error popups
                SetBreakOnLibraryErrors(1);
                
                // Initialize connection to this specific device
                if (PSB_InitializeSpecific(&psb, port, 1, result.baudRate) == PSB_SUCCESS) {
                    printf("? Successfully connected to PSB %s on COM%d at %d baud\n", 
                           TARGET_SERIAL, port, result.baudRate);
                    return 1; // Success
                } else {
                    printf("? Found target device but failed to connect\n");
                    return 0;
                }
            } else {
                printf("  ? Different PSB (serial: %s), continuing search...\n", result.serialNumber);
            }
        } else {
            printf(" no PSB device\n");
        }
        
        // Small delay between port scans
        Delay(0.05);
    }
    
    // Re-enable CVI error popups
    SetBreakOnLibraryErrors(1);
    
    printf("\n? PSB with serial number %s not found\n", TARGET_SERIAL);
    printf("Please check:\n");
    printf("1. PSB is powered on\n");
    printf("2. USB cable is connected\n");
    printf("3. PSB appears in Device Manager\n");
    printf("4. Correct serial number: %s\n", TARGET_SERIAL);
    printf("5. Try higher COM port numbers if needed\n");
    
    return 0; // Not found
}

/******************************************************************************
 * Manual Connection Function (Fallback)
 ******************************************************************************/
int ManualConnectPSB(int comPort) {
    printf("\n=== MANUAL CONNECTION TO COM%d ===\n", comPort);
    
    // Disable CVI error popups during manual connection
    SetBreakOnLibraryErrors(0);
    
    PSB_DiscoveryResult result;
    int scanResult = PSB_ScanPort(comPort, &result);
    
    // Re-enable CVI error popups
    SetBreakOnLibraryErrors(1);
    
    if (scanResult == PSB_SUCCESS) {
        printf("Found PSB on COM%d:\n", comPort);
        printf("  Model: %s\n", result.deviceType);
        printf("  Serial: %s\n", result.serialNumber);
        printf("  Baud: %d\n", result.baudRate);
        
        // Connect regardless of serial number for manual mode
        if (PSB_InitializeSpecific(&psb, comPort, 1, result.baudRate) == PSB_SUCCESS) {
            printf("? Successfully connected to PSB on COM%d\n", comPort);
            return 1;
        } else {
            printf("? Found PSB but failed to connect\n");
            return 0;
        }
    } else {
        printf("? No PSB found on COM%d\n", comPort);
        return 0;
    }
}

/******************************************************************************
 * Thread Function for Continuous Updates (only runs after PSB is connected)
 ******************************************************************************/
int CVICALLBACK UpdateThread(void *functionData) {
    PSB_Status status;
    double lastDebugTime = 0;
    int consecutiveErrors = 0;
    const int MAX_CONSECUTIVE_ERRORS = 5;
    
    while (connected) {        
        // Try to read basic status with timeout protection
        int result = PSB_GetDeviceStatus(&psb, &status);
        
        if (result == PSB_SUCCESS) {
            consecutiveErrors = 0;  // Reset error counter
            
            // Update UI in main thread
            PostDeferredCall(UpdateUI, &status);
            
            // Detailed debug output every 3 seconds
            double currentTime = Timer();
            if (currentTime - lastDebugTime >= 3.0) {
                printf("\n=== BASIC STATUS EVERY 3s ===\n");
                printf("Voltage: %.2fV, Current: %.2fA\n", status.voltage, status.current);
                printf("Output Enabled: %s\n", status.outputEnabled ? "YES" : "NO");
                printf("Remote Mode: %s\n", status.remoteMode ? "ACTIVE" : "LOCAL");
                printf("Operation Mode: %d\n", status.operationMode);
                
                // Show UI states
                int toggleState, ledState;
                GetCtrlVal(panelHandle, PANEL_TOGGLE_REMOTE_MODE, &toggleState);
                GetCtrlVal(panelHandle, PANEL_LED_REMOTE_MODE, &ledState);
                printf("UI Toggle: %s, UI LED: %s\n", 
                       toggleState ? "ON" : "OFF", ledState ? "ON" : "OFF");
                
                // Try detailed register 505 analysis (with safety checks)
                printf("\nTrying detailed register 505 read...\n");
                unsigned short deviceState[2] = {0, 0};
                
                // Set a very short timeout for this read
                SetComTime(psb.comPort, 0.5);  // 500ms timeout
                
                int reg505Result = PSB_ReadHoldingRegisters(&psb, REG_DEVICE_STATE, 2, deviceState);
                
                // Restore normal timeout
                SetComTime(psb.comPort, 1.0);  // 1 second timeout
                
                if (reg505Result == PSB_SUCCESS) {
                    unsigned long state = ((unsigned long)deviceState[1] << 16) | deviceState[0];
                    
                    printf("REGISTER 505 SUCCESS:\n");
                    printf("  Raw: [0]=0x%04X, [1]=0x%04X\n", deviceState[0], deviceState[1]);
                    printf("  Combined: 0x%08lX\n", state);
                    printf("  Bit 7 (Output): %s\n", (state & 0x80) ? "ON" : "OFF");
                    printf("  Bit 11 (Remote): %s\n", (state & 0x800) ? "ACTIVE" : "LOCAL");
                    printf("  Bits 0-4 (Control): %lu\n", state & 0x1F);
                    printf("  Bits 9-10 (Reg mode): %lu\n", (state & 0x600) >> 9);
                } else {
                    printf("REGISTER 505 FAILED: Error %d (%s)\n", 
                           reg505Result, PSB_GetErrorString(reg505Result));
                }
                
                printf("=============================\n\n");
                lastDebugTime = currentTime;
            }
        } else {
            consecutiveErrors++;
            printf("ERROR: Status read failed: %d (%s), consecutive errors: %d\n", 
                   result, PSB_GetErrorString(result), consecutiveErrors);
            
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                printf("TOO MANY CONSECUTIVE ERRORS - Stopping update thread\n");
                connected = 0;  // Stop the thread
                break;
            }
        }
        // Wait 500ms before next update
        Delay(0.5);
    }
    
    printf("DEBUG: UpdateThread exiting\n");
    return 0;
}

/******************************************************************************
 * Deferred UI Update Function (runs in main thread)
 ******************************************************************************/
void CVICALLBACK UpdateUI(void *callbackData) {
    PSB_Status *status = (PSB_Status*)callbackData;
    
    if (panelHandle > 0) {
        // Always update voltage and current readings
        SetCtrlVal(panelHandle, PANEL_NUM_VOLTAGE, status->voltage);
        SetCtrlVal(panelHandle, PANEL_NUM_CURRENT, status->current);
        
        // Initialize the toggle once on first successful read
        if (!remoteToggleInitialized) {
            SetCtrlVal(panelHandle, PANEL_TOGGLE_REMOTE_MODE, status->remoteMode);
            remoteToggleInitialized = 1;
            printf("INIT: Setting toggle to %s (remoteMode=%d)\n", 
                   status->remoteMode ? "ON" : "OFF", status->remoteMode);
        }
        
        // Always update LED to reflect actual PSB remote mode status
        SetCtrlVal(panelHandle, PANEL_LED_REMOTE_MODE, status->remoteMode);
        
        // Change LED color based on remote mode status
        if (status->remoteMode) {
            SetCtrlAttribute(panelHandle, PANEL_LED_REMOTE_MODE, ATTR_ON_COLOR, VAL_GREEN);
        } else {
            SetCtrlAttribute(panelHandle, PANEL_LED_REMOTE_MODE, ATTR_ON_COLOR, VAL_RED);
        }
    }
}

/******************************************************************************
 * Main Function
 ******************************************************************************/
int main(int argc, char *argv[]) {
    if (InitCVIRTE(0, argv, 0) == 0) return -1;
    
    // Load UI first
    panelHandle = LoadPanel(0, "BatteryTester.uir", PANEL);
    if (panelHandle < 0) {
        printf("Failed to load UI panel\n");
        return -1;
    }
    
    // Show UI immediately
    DisplayPanel(panelHandle);
    UpdateStatus("Starting PSB Battery Tester...");
    
    // Create thread pool
    if (CmtNewThreadPool(2, &threadPoolHandle) != 0) {
        UpdateStatus("Failed to create thread pool");
        RunUserInterface();
        DiscardPanel(panelHandle);
        return -1;
    }
    
    // Start PSB discovery on background thread
    UpdateStatus("Initializing PSB discovery...");
    CmtScheduleThreadPoolFunction(threadPoolHandle, PSBDiscoveryThread, NULL, &discoveryThreadID);
    
    // Run the UI (this blocks until user closes)
    RunUserInterface();
    
    // Cleanup
    connected = 0;  // Signal threads to stop
    
    if (threadPoolHandle > 0) {
        // Wait for threads to finish
        if (discoveryComplete && connected) {
            CmtWaitForThreadPoolFunctionCompletion(threadPoolHandle, updateThreadID, 
                                                  OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        }
        CmtWaitForThreadPoolFunctionCompletion(threadPoolHandle, discoveryThreadID, 
                                              OPT_TP_PROCESS_EVENTS_WHILE_WAITING);
        CmtDiscardThreadPool(threadPoolHandle);
    }
    
    if (connected) {
        PSB_SetOutputEnable(&psb, 0);  // Turn off output
        PSB_SetRemoteMode(&psb, 0);    // Exit remote mode
        PSB_Close(&psb);
    }
    
    DiscardPanel(panelHandle);
    return 0;
}

/******************************************************************************
 * Panel Callback
 ******************************************************************************/
int CVICALLBACK PanelCallback(int panel, int event, void *callbackData, 
                              int eventData1, int eventData2) {
    if (event == EVENT_CLOSE) {
        QuitUserInterface(0);
    }
    return 0;
}

/******************************************************************************
 * Remote Mode Toggle Callback
 ******************************************************************************/
int CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData,
                                 int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;  // Only respond to actual button clicks
    }
    
    if (!connected) {
        printf("ERROR: Not connected to PSB device\n");
        UpdateStatus("Not connected to PSB device");
        return 0;
    }
    
    int toggleState;
    int result;
    
    // Get toggle state (this is what the user wants to set)
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &toggleState);
    
    printf("=== User requesting Remote Mode: %s ===\n", toggleState ? "ON" : "OFF");
    
    // Send command to PSB
    result = PSB_SetRemoteMode(&psb, toggleState);
    if (result != PSB_SUCCESS) {
        printf("FAILED to set remote mode: %s\n", PSB_GetErrorString(result));
        UpdateStatus("Failed to set remote mode");
        
        // Reset toggle to previous state since command failed
        SetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, !toggleState);
        return 0;
    }
    
    printf("Remote mode command sent successfully\n");
    
    char statusMsg[100];
    snprintf(statusMsg, sizeof(statusMsg), "Remote mode %s requested", toggleState ? "ON" : "OFF");
    UpdateStatus(statusMsg);
    
    return 0;
}

/******************************************************************************
 * Set Values Button Callback
 ******************************************************************************/
int CVICALLBACK SetValuesCallback(int panel, int control, int event, void *callbackData,
                                  int eventData1, int eventData2) {
    if (event != EVENT_COMMIT) {
        return 0;  // Only respond to actual button clicks
    }
    
    if (!connected) {
        printf("ERROR: Not connected to PSB device\n");
        return 0;
    }
    
    double voltage, current;
    int result;
    int remoteModeState;
    
    // Check if remote mode is enabled
    GetCtrlVal(panel, PANEL_TOGGLE_REMOTE_MODE, &remoteModeState);
    if (!remoteModeState) {
        printf("ERROR: Remote mode must be enabled first!\n");
        printf("Please turn on the Remote Mode toggle switch.\n");
        return 0;
    }
    
    // Get values from UI
    GetCtrlVal(panel, PANEL_NUM_SET_VOLTAGE, &voltage);
    GetCtrlVal(panel, PANEL_NUM_SET_CURRENT, &current);
    
    printf("=== Setting PSB values: %.2fV, %.2fA ===\n", voltage, current);
    
    // Step 1: Set voltage
    printf("1. Setting voltage to %.2fV...\n", voltage);
    result = PSB_SetVoltage(&psb, voltage);
    if (result != PSB_SUCCESS) {
        printf("   FAILED: %s\n", PSB_GetErrorString(result));
        return 0;
    }
    printf("   SUCCESS\n");
    
    // Step 2: Set current  
    printf("2. Setting current to %.2fA...\n", current);
    result = PSB_SetCurrent(&psb, current);
    if (result != PSB_SUCCESS) {
        printf("   FAILED: %s\n", PSB_GetErrorString(result));
        return 0;
    }
    printf("   SUCCESS\n");
    
    // Step 3: Enable output
    printf("3. Enabling output...\n");
    result = PSB_SetOutputEnable(&psb, 1);
    if (result != PSB_SUCCESS) {
        printf("   FAILED: %s\n", PSB_GetErrorString(result));
        return 0;
    }
    printf("   SUCCESS\n");
    
    printf("=== PSB configuration completed ===\n");
    
    return 0;
}

/******************************************************************************
 * Test Button Callback
 ******************************************************************************/
int CVICALLBACK TestButtonCallback(int panel, int control, int event, void *callbackData,
                                   int eventData1, int eventData2) {
    if (event == EVENT_COMMIT) {
        TestBasicCommunication();
    }
    return 0;
}

/******************************************************************************
 * Debug Function to Test Basic Communication
 ******************************************************************************/
void TestBasicCommunication(void) {
    if (!connected) {
        printf("=== NOT CONNECTED TO PSB ===\n");
        printf("Try restarting the application to re-scan for devices.\n");
        return;
    }
    
    printf("\n=== TESTING CURRENT PSB CONNECTION ===\n");
    
    PSB_Status status;
    if (PSB_GetDeviceStatus(&psb, &status) == PSB_SUCCESS) {
        printf("? Communication successful!\n");
        printf("Voltage: %.2fV\n", status.voltage);
        printf("Current: %.2fA\n", status.current);
        printf("Power: %.2fW\n", status.power);
        printf("Output: %s\n", status.outputEnabled ? "ON" : "OFF");
        printf("Remote: %s\n", status.remoteMode ? "ACTIVE" : "LOCAL");
        printf("Mode: %d\n", status.operationMode);
    } else {
        printf("? Communication failed!\n");
        printf("Device may have been disconnected.\n");
    }
    
    printf("=== TEST COMPLETE ===\n\n");
}