# Battery Tester
This git project is created solely for the purpose of version control. The bare upstream repository is located at `/srv/batter-tester.git/`.

# Battery Tester - Comprehensive Documentation

## Project Overview

Battery Tester is a sophisticated LabWindows/CVI 2020 application designed to control and coordinate battery testing operations using two primary instruments:
- **PSB 10000 Series Power Supply** - Programmable DC power supply for charge/discharge operations
- **Bio-Logic SP-150e** - Electrochemical workstation for advanced battery analysis

The application provides automated testing, real-time monitoring, and data logging capabilities for comprehensive battery characterization.

## Architecture Overview

### Core Design Principles

1. **Thread-Safe Command Queue System**: All device communications are handled through priority-based command queues to ensure thread safety and prevent conflicts
2. **Modular Architecture**: Each major component (PSB control, BioLogic control, logging, status monitoring) is self-contained
3. **Asynchronous Operations**: Long-running operations execute in background threads to maintain UI responsiveness
4. **Comprehensive Error Handling**: Every operation includes error checking and recovery mechanisms

## Directory Structure

```
BatteryTester/
│
├── BatteryTester.c         # Main application entry point and UI callbacks
├── BatteryTester.h         # UI resource definitions (auto-generated)
├── BatteryTester.uir       # LabWindows/CVI user interface file
├── common.h                # Shared definitions, types, and macros
├── logging.c/.h            # Thread-safe logging system
├── status.c/.h             # Background device status monitoring
├── device_queue.c/.h       # Generic thread-safe command queue framework
├── exp_capacity.c/.h       # Battery capacity testing experiment
├── README.md               # This file
│
├── psb10000/              # PSB 10000 Power Supply modules
│   ├── psb10000_dll.c/.h  # Low-level PSB communication via Modbus
│   ├── psb10000_queue.c/.h # Thread-safe PSB command queue
│   └── psb10000_test.c/.h # Comprehensive PSB test suite
│
└── biologic/              # Bio-Logic SP-150e modules
    ├── biologic_dll.c/.h   # Bio-Logic DLL wrapper functions
    ├── biologic_queue.c/.h # Thread-safe BioLogic command queue
    └── BLStructs.h         # Bio-Logic data structures

lib/                        # External Bio-Logic DLL files (from Lab Development Package)
```

## Program Flow

### 1. Application Startup (BatteryTester.c)

```c
int main (int argc, char *argv[])
```

The main function performs the following initialization sequence:

1. **Initialize LabWindows/CVI Runtime**
   - Load the user interface from BatteryTester.uir
   - Create the main application panel

2. **Initialize Core Systems**
   ```c
   InitializeLogging();           // Start logging system
   CmtNewThreadPool();           // Create thread pool for async operations
   CmtNewLock();                 // Create global busy lock for synchronization
   ```

3. **Initialize Device Queue Managers**
   - **PSB Queue Manager**: Controls access to PSB 10000 power supply
   - **BioLogic Queue Manager**: Controls access to SP-150e (if enabled)
   
   Queue managers provide:
   - Thread-safe command queuing
   - Automatic reconnection on disconnect
   - Priority-based command execution
   - Transaction support for atomic operations

4. **Start Status Monitoring**
   ```c
   Status_Initialize(g_mainPanelHandle);
   Status_Start();
   ```
   Status monitoring runs at 5Hz in the background, continuously:
   - Checking device connections
   - Updating UI indicators (LEDs, status strings)
   - Reading device values (voltage, current, power)

5. **Display UI and Run**
   ```c
   DisplayPanel(g_mainPanelHandle);
   RunUserInterface();
   ```

### 2. Device Communication Architecture

#### Thread-Safe Command Queue System

The application uses a sophisticated queue-based architecture to ensure thread-safe device access:

```
UI Thread → Command Queue → Processing Thread → Device
    ↑                              ↓
    └──────── Callback ←───────────┘
```

**Key Components:**

1. **Command Queuing**
   - Commands are queued with priority levels (HIGH, NORMAL, LOW)
   - Each command includes parameters and optional callback
   - Blocking and asynchronous modes supported

2. **Processing Thread**
   - One dedicated thread per device
   - Processes commands in priority order
   - Handles connection management and recovery

3. **Device Adapters**
   - Abstract interface for device-specific operations
   - PSB adapter uses Modbus RTU protocol
   - BioLogic adapter uses vendor DLL

**Example: Setting PSB Voltage**
```c
// Asynchronous command
PSB_QueueCommandAsync(queueMgr, PSB_CMD_SET_VOLTAGE, 
                     &params, PSB_PRIORITY_HIGH, 
                     MyCallback, userData);

// Blocking command
PSB_QueueCommandBlocking(queueMgr, PSB_CMD_SET_VOLTAGE,
                        &params, PSB_PRIORITY_HIGH, 
                        &result, 5000);  // 5 second timeout
```

### 3. PSB 10000 Power Supply Control

#### Connection Process

1. **Auto-Discovery Mode** (default)
   - Scans all available COM ports
   - Tests multiple baud rates (9600, 19200, 38400, 57600, 115200)
   - Searches for device with matching serial number ("2872380001")

2. **Connection Establishment**
   - Opens Modbus RTU connection
   - Verifies device identity
   - Enables remote control mode

#### Command Types

- **Control Commands**: Set voltage, current, power, output enable
- **Limit Commands**: Set voltage/current/power limits
- **Query Commands**: Read status, actual values, alarms
- **Sink Mode Commands**: Control electronic load functionality

#### Modbus Implementation

The PSB uses Modbus RTU protocol with comprehensive register support:

**Modbus Function Codes:**
- `0x03` - Read Holding Registers
- `0x05` - Write Single Coil  
- `0x06` - Write Single Register

**Device Information Registers:**
```
REG_DEVICE_CLASS      0x0000 (0)     - Device class identifier
REG_DEVICE_TYPE       0x0001 (1)     - Device type string (40 chars, registers 1-20)
REG_SERIAL_NUMBER     0x0097 (151)   - Serial number (40 chars, registers 151-170)
```

**Control Registers (Coils):**
```
REG_REMOTE_MODE       0x0192 (402)   - Remote mode enable/disable (coil)
REG_DC_OUTPUT         0x0195 (405)   - Output enable/disable (coil)
```

**Source Mode Registers:**
```
REG_SET_VOLTAGE       0x01F4 (500)   - Set output voltage
REG_SET_CURRENT       0x01F5 (501)   - Set output current  
REG_SET_POWER_SOURCE  0x01F6 (502)   - Set output power
```

**Sink Mode (Electronic Load) Registers:**
```
REG_SINK_MODE_POWER   0x01F2 (498)   - Set sink power
REG_SINK_MODE_CURRENT 0x01F3 (499)   - Set sink current
```

**Status and Measurement Registers:**
```
REG_DEVICE_STATE      0x01F9 (505)   - Device state (32-bit, uses 505-506)
REG_ACTUAL_VOLTAGE    0x01FB (507)   - Measured voltage
REG_ACTUAL_CURRENT    0x01FC (508)   - Measured current
REG_ACTUAL_POWER      0x01FD (509)   - Measured power
```

**Limit Registers:**
```
REG_VOLTAGE_MIN       0x2329 (9001)  - Minimum voltage limit
REG_VOLTAGE_MAX       0x2328 (9000)  - Maximum voltage limit
REG_CURRENT_MIN       0x232B (9003)  - Minimum current limit
REG_CURRENT_MAX       0x232A (9002)  - Maximum current limit
REG_POWER_MAX         0x232C (9004)  - Maximum power limit
REG_SINK_POWER_MAX    0x232D (9005)  - Maximum sink power limit
REG_SINK_CURRENT_MIN  0x2331 (9009)  - Minimum sink current limit
REG_SINK_CURRENT_MAX  0x2330 (9008)  - Maximum sink current limit
```

**Device State Register (505-506) Bit Definitions:**

The 32-bit device state register provides comprehensive status information:
```
Bits 0-3:   Control Location (0x0F mask)
Bit 4:      Output Enabled (0x10)
Bits 9-10:  Regulation Mode (0x600) - 0=CV, 1=CC, 2=CP
Bit 11:     Remote Mode Active (0x800)
Bit 12:     Alarms Active (0x1000)
Bit 15:     Sink/Source Mode (0x8000) - 0=Source, 1=Sink
```

**Data Format:**
- All register values use 16-bit unsigned integers
- Values are scaled: actual_value = (register_value / 32767) * nominal_value
- Nominal values: 60V, 60A, 1200W (for the derated 60V/60A version)
- Coil values: 0xFF00 = ON, 0x0000 = OFF

**Communication Parameters:**
- Default slave address: 1
- Supported baud rates: 9600, 19200, 38400, 57600, 115200
- Data bits: 8, Parity: None, Stop bits: 1
- CRC-16 (Modbus) for error checking

### 4. Bio-Logic SP-150e Control

#### Connection Process

1. **DLL Initialization**
   - Loads ECLab.dll and blfind.dll
   - Initializes function pointers

2. **Device Discovery**
   - Scans for USB devices (default)
   - Also supports Ethernet and BCS connections

3. **Connection**
   - Connects using device address (e.g., "USB0")
   - Retrieves device information
   - Tests connection integrity

#### Key Operations

- **Channel Control**: Start/stop experiments on specific channels
- **Technique Loading**: Load and configure experimental techniques
- **Data Acquisition**: Real-time data collection during experiments
- **Parameter Updates**: Modify experimental parameters on-the-fly

### 5. Status Monitoring System

The status module runs continuously in the background:

```c
Status_TimerThread() → runs every 200ms
    ├── Check PSB connection status
    │   ├── Update connection LED (red/yellow/green)
    │   ├── Update status string
    │   └── Read voltage/current/power values
    │
    └── Check BioLogic connection status
        ├── Update connection LED
        └── Update status string
```

**Features:**
- Automatic UI updates using PostDeferredCall
- Connection state tracking
- Pause/resume capability during experiments

### 6. Test Suite System

#### PSB Test Suite

Comprehensive testing of PSB functionality:

1. **Remote Mode Control**
2. **Status Register Reading**
3. **Voltage Control & Limits**
4. **Current Control & Limits**
5. **Power Control & Limits**
6. **Output Enable/Disable**
7. **Boundary Condition Testing**
8. **Sink Mode Operations**

Test execution:
- Runs in dedicated thread
- Pauses status monitoring during tests
- Provides real-time progress updates
- Generates detailed test report

### 7. Battery Capacity Experiment

Advanced battery testing capability:

```
Preparation → Verify Charged → Discharge → Charge → Analysis
```

**Process:**

1. **Preparation**
   - Configure PSB limits
   - Set power limits to 20W
   - Verify battery voltage

2. **Discharge Phase**
   - Sink mode at specified current
   - Monitor voltage decline
   - Stop at voltage threshold
   - Log data to discharge.csv

3. **Charge Phase**
   - Source mode at specified current
   - Monitor voltage rise
   - Stop at voltage threshold
   - Log data to charge.csv

4. **Data Collection**
   - Time-stamped measurements
   - Real-time graph updates
   - CSV file generation
   - Capacity calculation

### 8. Logging System

Thread-safe logging with multiple outputs:

```c
LogMessage("Operation completed");
LogError("Connection failed: %s", errorMsg);
LogMessageEx(LOG_DEVICE_PSB, "PSB voltage set to %.2fV", voltage);
```

**Features:**
- Simultaneous file and UI logging
- Device-specific prefixes
- Log level support (INFO, WARNING, ERROR, DEBUG)
- Thread-safe queue for UI updates
- Automatic file rotation at 10MB

### 9. Error Handling

Comprehensive error handling throughout:

1. **Device Communication Errors**
   - Automatic reconnection attempts
   - Exponential backoff
   - Error callbacks

2. **Resource Management**
   - SAFE_MALLOC/SAFE_FREE macros
   - Automatic cleanup on errors
   - Lock timeout handling

3. **User Feedback**
   - Error popups for critical issues
   - Status messages for recoverable errors
   - Detailed logging for debugging

## Key Design Patterns

### 1. Command Pattern
All device operations are encapsulated as commands with parameters and results.

### 2. Observer Pattern
Callbacks notify interested parties of command completion or device state changes.

### 3. Producer-Consumer Pattern
UI produces commands, processing threads consume and execute them.

### 4. Singleton Pattern
Global queue managers ensure single point of device access.

## Usage Examples

### Running a PSB Test Suite

1. Click "Test PSB" button
2. System automatically:
   - Pauses status monitoring
   - Runs all tests sequentially
   - Updates progress in real-time
   - Generates test report
   - Resumes status monitoring

### Performing Capacity Test

1. Configure test parameters in UI
2. Click "Start Capacity Test"
3. System performs:
   - Battery state verification
   - Automated discharge cycle
   - Automated charge cycle
   - Data logging and analysis

### Manual Control

1. Enable Remote Mode toggle
2. Adjust voltage/current sliders
3. Enable output
4. Monitor real-time values

## Configuration

### Enabling/Disabling Devices

In `status.h`:
```c
#define STATUS_MONITOR_PSB         1    // Enable PSB monitoring
#define STATUS_MONITOR_BIOLOGIC    0    // Disable BioLogic monitoring
```

### PSB Configuration

In `BatteryTester.c`:
```c
#define PSB_TARGET_SERIAL "2872380001"  // Your PSB serial number
```

### Logging Configuration

In `logging.c`:
```c
#define MAX_LOG_FILE_SIZE (10 * 1024 * 1024)  // 10MB max size
#define LOG_FILE_NAME "BatteryTester.log"
```

## Troubleshooting

### PSB Connection Issues

1. **Check COM Port**: Ensure PSB is connected and drivers installed
2. **Verify Serial Number**: Confirm PSB_TARGET_SERIAL matches your device
3. **Check Baud Rate**: Default is auto-detect, can force specific rate
4. **Remote Mode**: Some operations require remote mode enabled

### BioLogic Connection Issues

1. **Check USB Connection**: Ensure device is connected
2. **Install Drivers**: Bio-Logic USB drivers must be installed
3. **DLL Path**: Verify ECLab.dll is in PATH or project directory
4. **Device Address**: Default is "USB0", adjust if needed

### Performance Issues

1. **Reduce Update Rate**: Adjust STATUS_UPDATE_RATE_HZ if needed
2. **Check Thread Pool**: Increase THREAD_POOL_SIZE for more parallelism
3. **Disable Unused Devices**: Turn off monitoring for disconnected devices

## Building the Project

1. Open project in LabWindows/CVI 2020
2. Ensure all paths are configured correctly
3. Build in Release mode for optimal performance
4. Deploy with all DLL dependencies

## Future Enhancements

1. **Multi-Device Support**: Control multiple PSBs or BioLogics simultaneously
2. **Advanced Experiments**: Add more battery testing protocols
3. **Data Analysis**: Integrated analysis tools for test results
4. **Remote Control**: Network-based control interface
5. **Database Integration**: Store test results in database

## Dependencies

- LabWindows/CVI 2020 Runtime
- Bio-Logic ECLab Development Package (for SP-150e support)
- Windows 7 or later
- Serial port driver for PSB 10000

---

*Note: This documentation reflects the current state of the Battery Tester application. Always refer to the source code for the most up-to-date implementation details.*