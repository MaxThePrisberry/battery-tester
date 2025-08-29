# Battery Tester System

## Overview

This is a comprehensive battery testing system built in **LabWindows/CVI 2020 (C99)** designed for automated battery characterization and analysis. The system integrates multiple hardware instruments to provide battery testing capabilities including charge/discharge cycles, electrochemical impedance spectroscopy (EIS), temperature control, and comprehensive data logging.

**Developed by:** Maxwell Prisbrey

---

## System Architecture

### Hardware Components

The system interfaces with 5 main hardware devices through various communication protocols:

1. **[PSB 10000 Power Supply](#psb-10000-power-supply)** - EA Elektro-Automatik bidirectional power supply (60V/60A derated)
2. **[Bio-Logic SP-150e](#bio-logic-sp-150e-potentiostat)** - Potentiostat for electrochemical measurements
3. **[DTB4848 Temperature Controllers](#dtb4848-temperature-controllers)** - K-type thermocouple PID controllers
4. **[Teensy Microcontroller](#teensy-microcontroller)** - Digital I/O control via Arduino-compatible board
5. **[cDAQ-9178 System](#cdaq-9178-system)** - National Instruments data acquisition for temperature monitoring

---

## Device Modules

### PSB 10000 Power Supply

**Purpose:** Primary power source/sink for battery charging and discharging operations

**Files:**
- `psb10000/psb10000_dll.h/c` - Core device interface
- `psb10000/psb10000_queue.h/c` - Thread-safe command queuing
- `tests/psb10000_test.h/c` - Comprehensive test suite

**Communication:** Modbus RTU over RS232 (COM3, 9600 baud, Slave Address 1)

**Capabilities:**
- **Voltage Range:** 0-60V (61.2V max for safety)
- **Current Range:** ±60A (61.2A max for safety) 
- **Power Rating:** 1200W (1224W max)
- **Operating Modes:** 
  - Source mode (power supply for charging)
  - Sink mode (electronic load for discharging)
  - Automatic CV/CC/CP regulation

**Key Functions:**
```c
// Connection management
int PSB_Connect(PSB_Handle *handle, const char *targetSerial);
int PSB_TestConnection(PSB_Handle *handle);

// Parameter control (queued for thread safety)
int PSB_SetVoltageQueued(double voltage, DevicePriority priority);
int PSB_SetCurrentQueued(double current, DevicePriority priority); 
int PSB_SetPowerQueued(double power, DevicePriority priority);
int PSB_SetSinkCurrentQueued(double current, DevicePriority priority);

// Status monitoring
int PSB_GetStatusQueued(PSB_Status *status, DevicePriority priority);
```

**Safety Features:**
- Automatic over-voltage/current/power protection
- Safe startup sequence with zero values
- Serial number verification ("2872380001")
- Current limiting for both source and sink modes

### Bio-Logic SP-150e Potentiostat

**Purpose:** Electrochemical measurements including impedance spectroscopy and advanced techniques

**Files:**
- `biologic/biologic_dll.h/c` - Core Bio-Logic API wrapper
- `biologic/biologic_queue.h/c` - Thread-safe technique management  
- `biologic/BLStructs.h` - Bio-Logic data structures
- `tests/biologic_test.h/c` - Technique validation tests

**Communication:** USB connection using Bio-Logic Development Package API

**Supported Techniques:**
- **OCV** - Open Circuit Voltage measurements
- **PEIS** - Potentiostatic Electrochemical Impedance Spectroscopy
- **GEIS** - Galvanostatic Electrochemical Impedance Spectroscopy  
- **SPEIS** - Staircase PEIS for multi-voltage impedance
- **CA/CP** - Chronoamperometry/Chronopotentiometry
- **CV** - Cyclic Voltammetry

**Key Functions:**
```c
// High-level technique interface
int BIO_StartOCV(int deviceID, uint8_t channel, double duration, 
                double sampleRate, BIO_TechniqueContext **context);

int BIO_StartPEIS(int deviceID, uint8_t channel, double startFreq, 
                 double endFreq, double amplitude, int numPoints,
                 BIO_TechniqueContext **context);

// Data retrieval with processing
int BIO_GetTechniqueData(BIO_TechniqueContext *context, 
                        BIO_TechniqueData **data);
int BIO_ProcessTechniqueData(BIO_TechniqueData *rawData, 
                           BIO_ProcessedData **processedData);
```

**Advanced Features:**
- Automatic impedance data processing (magnitude, phase, Nyquist plots)
- Multi-channel support with firmware loading
- Real-time technique progress monitoring
- Automatic range selection for optimal measurements

### DTB4848 Temperature Controllers

**Purpose:** Precise temperature control using K-type thermocouples with PID regulation

**Files:**
- `dtb4848/dtb4848_dll.h/c` - Modbus ASCII protocol implementation
- `dtb4848/dtb4848_queue.h/c` - Multi-device queue management

**Communication:** Modbus ASCII over RS232 (COM5, 9600 baud)

**Features:**
- **Temperature Range:** -199.9°C to 999.9°C (K-type thermocouple)
- **Control Methods:** PID, ON/OFF, Manual
- **PID Modes:** 5 different tuning parameter sets with auto-selection
- **Multi-Device Support:** Up to 16 controllers with different slave addresses

**Key Functions:**
```c
// Individual device control
int DTB_SetSetPoint(DTB_Handle *handle, double temperature);
int DTB_SetRunStop(DTB_Handle *handle, int run);
int DTB_GetStatus(DTB_Handle *handle, DTB_Status *status);

// Multi-device operations (thread-safe)
int DTB_SetSetPointAllQueued(double temperature, DevicePriority priority);
int DTB_GetStatusAllQueued(DTB_Status *statuses, int *numDevices, DevicePriority priority);

// Configuration management
int DTB_ConfigureAtomic(int slaveAddress, const DTB_Configuration *config,
                       DTBTransactionCallback callback, void *userData);
```

**PID Control Features:**
- Auto-tuning capability for optimal performance
- Configurable alarm thresholds with hysteresis
- Front panel lock for security during automated operation
- Write protection to prevent unauthorized changes

### Teensy Microcontroller

**Purpose:** Digital I/O control for external equipment and switching

**Files:**
- `teensy/teensy_dll.h/c` - Serial communication protocol
- `teensy/teensy_queue.h/c` - Pin control command queue

**Communication:** Serial over USB (9600 baud, Arduino-style protocol)

**Protocol:** Simple ASCII commands: `D<pin><H/L>\n` → Response: `<pin><0/1>\n`

**Capabilities:**
- **Pin Control:** Digital pins 0-16 with HIGH/LOW states
- **Verification:** Automatic readback confirmation of pin states
- **Multi-Pin Operations:** Batch pin setting with error recovery

**Key Functions:**
```c
// Direct pin control
int TNY_SetPin(TNY_Handle *handle, int pin, int state);
int TNY_SetMultiplePins(TNY_Handle *handle, const int *pins, 
                       const int *states, int count);

// Queued operations (thread-safe)
CommandID TNY_SetPinAsync(int pin, int state, TNYCommandCallback callback, 
                         void *userData, DevicePriority priority);
```

### cDAQ-9178 System

**Purpose:** High-precision thermocouple temperature monitoring using National Instruments hardware

**Files:**
- `cdaq_utils.h/c` - NIDAQmx interface for thermocouple measurements

**Hardware Configuration:**
- **Slots 2 & 3:** NI 9213 thermocouple input modules (16 channels each)
- **Total Channels:** 32 K-type thermocouple inputs
- **Temperature Range:** 0-400°C with 25°C cold junction compensation

**Key Functions:**
```c
// Individual channel reading  
int CDAQ_ReadTC(int slot, int tc_number, double *temperature);

// Array reading for efficiency
int CDAQ_ReadTCArray(int slot, double *temperatures, int *num_read);

// System management
int CDAQ_Initialize(void);
void CDAQ_Cleanup(void);
```

---

## Device Queue System

### Architecture Overview

The heart of the system is a sophisticated **thread-safe command queue system** that ensures reliable, sequential device communication while preventing conflicts between different parts of the application.

**Files:**
- `device_queue.h/c` - Generic queue implementation
- Each device has its own queue wrapper (e.g., `psb10000_queue.h/c`)

### Key Features

**1. Priority-Based Queuing**
```c
typedef enum {
    DEVICE_PRIORITY_HIGH = 0,    // User commands (immediate)
    DEVICE_PRIORITY_NORMAL = 1,  // Experiment operations  
    DEVICE_PRIORITY_LOW = 2      // Status monitoring
} DevicePriority;
```

**2. Command Execution Modes**
```c
// Blocking execution (waits for completion)
int DeviceQueue_CommandBlocking(DeviceQueueManager *mgr, int commandType,
                               void *params, DevicePriority priority,
                               void *result, int timeoutMs);

// Asynchronous execution (returns immediately) 
DeviceCommandID DeviceQueue_CommandAsync(DeviceQueueManager *mgr, int commandType,
                                        void *params, DevicePriority priority,
                                        DeviceCommandCallback callback, void *userData);
```

**3. Transaction Support**
```c
// Atomic operations - all commands succeed or all fail
DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(mgr);
DeviceQueue_AddToTransaction(mgr, txn, CMD_TYPE, &params);
DeviceQueue_AddToTransaction(mgr, txn, CMD_TYPE2, &params2); 
int result = DeviceQueue_CommitTransaction(mgr, txn, callback, userData);
```

**4. Automatic Error Recovery**
- Connection monitoring with automatic reconnection
- Command retry with exponential backoff
- Device state recovery after communication errors
- Graceful handling of partial failures

### Device Adapter Pattern

Each device implements a standardized interface:

```c
typedef struct DeviceAdapter {
    const char *deviceName;
    
    // Connection management
    int (*connect)(void *deviceContext, void *connectionParams);
    int (*disconnect)(void *deviceContext);
    int (*testConnection)(void *deviceContext);
    bool (*isConnected)(void *deviceContext);
    
    // Command execution  
    int (*executeCommand)(void *deviceContext, int commandType, void *params, void *result);
    
    // Memory management
    void* (*createCommandParams)(int commandType, void *sourceParams);
    void (*freeCommandParams)(int commandType, void *params);
    // ... etc
} DeviceAdapter;
```

This allows the queue system to work with any device while maintaining type safety and proper resource management.

---

## Experiment Modules

### CDC (Charge/Discharge Control) Experiment

**Purpose:** Basic battery charge and discharge operations with real-time monitoring

**Files:** `exp_cdc.h/c`

**Workflow:**
1. **Parameter Validation** - Verify target voltage, current, and thresholds
2. **Battery State Verification** - Check if battery is already at target state
3. **PSB Configuration** - Set appropriate current limits for charge/discharge
4. **Operation Execution** - Monitor voltage/current until completion criteria met
5. **Data Logging** - Record voltage, current, and time data to graphs

**Key Features:**
- Automatic charge/discharge detection based on current voltage
- User-configurable current thresholds for completion detection
- Real-time graphical updates during operation
- Safety timeouts and power limiting
- Coulomb counting for capacity measurement

**Control Flow:**
```c
// User clicks Charge or Discharge button
StartCDCOperation(panel, control, CDC_MODE_CHARGE/DISCHARGE)
├── Check system busy state
├── Verify PSB connection  
├── Initialize experiment context with UI parameters
├── Launch background thread: CDCExperimentThread()
│   ├── Show parameter confirmation dialog
│   ├── Initialize PSB to safe state (zero values)
│   ├── VerifyBatteryState() - check if already charged/discharged
│   ├── RunOperation() - main control loop
│   │   ├── Configure PSB with target voltage/current
│   │   ├── Enable output and start operation
│   │   ├── Monitor loop until completion:
│   │   │   ├── Read PSB status (voltage, current)
│   │   │   ├── Update graphs and UI
│   │   │   ├── Check completion criteria
│   │   │   └── Check for user cancellation
│   │   └── Disable output and safe shutdown
│   └── RestoreUI() and cleanup
└── Update button states
```

### Baseline Experiment

**Purpose:** Long-term battery monitoring with periodic EIS measurements for degradation analysis

**Files:** `exp_baseline.h/c`

**Workflow:**
1. **Temperature Control Setup** - Configure DTB controllers for thermal management
2. **Initial EIS Baseline** - Perform reference impedance measurement
3. **Monitoring Loop** - Continuous voltage/temperature monitoring
4. **Periodic EIS** - Repeat impedance measurements at specified intervals  
5. **Data Analysis** - Track impedance changes over time for degradation assessment

**Key Parameters:**
- **Temperature Setpoint** - Target temperature for thermal management
- **Monitoring Interval** - How often to check voltage/temperature
- **EIS Interval** - How often to perform impedance measurements
- **Current Threshold** - Minimum current for valid EIS measurements

**Integration Points:**
- Uses **Bio-Logic** for EIS measurements (PEIS or GEIS techniques)
- Uses **DTB controllers** for temperature regulation  
- Uses **cDAQ system** for additional temperature monitoring
- Uses **PSB** for controlled current sourcing during EIS

**Typical Use Case:**
Long-term battery aging studies where you need to track how internal resistance and other electrochemical properties change over days/weeks/months of testing.

---

## Battery Utilities Module

**Purpose:** Mathematical functions for battery calculations and analysis

**Files:** `battery_utils.h/c`

### Core Calculation Functions

**1. Capacity Calculations (Coulomb Counting)**
```c
// Trapezoidal integration for precise capacity measurement
double Battery_CalculateCapacityIncrement(double current1_A, double current2_A, 
                                         double deltaTime_s);

// Energy calculations using voltage and current
double Battery_CalculateEnergyIncrement(double voltage1_V, double current1_A,
                                       double voltage2_V, double current2_A,
                                       double deltaTime_s);
```

**2. Efficiency Analysis**
```c
// Round-trip coulombic efficiency (charge/discharge capacity ratio)
double Battery_CalculateCoulombicEfficiency(double chargeCapacity_mAh, 
                                           double dischargeCapacity_mAh);

// Energy efficiency (energy out / energy in)  
double Battery_CalculateEnergyEfficiency(double chargeEnergy_Wh, 
                                        double dischargeEnergy_Wh);
```

**3. High-Level Battery Operations**
```c
// Automated charge/discharge to target voltage with monitoring
int Battery_GoToVoltage(VoltageTargetParams *params);

// Precise capacity transfer (charge/discharge specific mAh)
int Battery_TransferCapacity(CapacityTransferParams *params);
```

### Parameter Structures

The utility functions use comprehensive parameter structures that include:

- **Target Parameters:** Voltage, current, capacity, power limits
- **Termination Criteria:** Current thresholds, time limits, voltage limits
- **Monitoring Options:** Update intervals, callback functions
- **UI Integration:** Panel handles for real-time updates  
- **Results Tracking:** Actual values achieved, completion reasons
- **Safety Features:** Cancellation flags, timeout handling

This allows experiments to use high-level battery operations without dealing with low-level device control details.

---

## Logging and Status System

### Logging Module

**Purpose:** Centralized, thread-safe logging with device-specific prefixes

**Files:** `logging.h/c`

**Features:**
- **Multi-Level Logging:** Debug, Info, Warning, Error
- **Device-Specific Prefixes:** [PSB], [BIO], [DTB], [TNY], [DAQ]  
- **Thread-Safe Operations:** Safe logging from background threads
- **UI Integration:** Automatic updates to status displays
- **File Output:** Optional logging to files for analysis

**Usage Examples:**
```c
// Device-specific logging
LogMessageEx(LOG_DEVICE_PSB, "Setting voltage to %.2f V", voltage);
LogErrorEx(LOG_DEVICE_BIO, "Failed to load technique: %s", BIO_GetErrorString(error));

// General logging
LogMessage("Experiment started successfully");
LogWarning("Battery temperature elevated: %.1f°C", temp);
```

### Status Monitoring System

**Purpose:** Continuous monitoring of all device connections and states

**Files:** `status.h/c`

**Architecture:**
- **1 Hz Update Rate** - Continuous monitoring without overwhelming devices
- **Async Status Queries** - Non-blocking device communication
- **Connection State Management** - Automatic reconnection attempts
- **UI Synchronization** - Real-time status indicator updates

**Device States:**
```c
typedef enum {
    CONN_STATE_IDLE = 0,          // Not started/stopped
    CONN_STATE_DISCOVERING,       // Searching for device
    CONN_STATE_CONNECTING,        // Found, connecting  
    CONN_STATE_CONNECTED,         // Successfully connected
    CONN_STATE_ERROR,             // Error state
    CONN_STATE_RECONNECTING       // Attempting reconnection
} ConnectionState;
```

**Status Information Tracked:**
- **PSB:** Voltage, current, power, output state, regulation mode
- **Bio-Logic:** Connection state, channel status, active techniques
- **DTB Controllers:** Temperature readings, setpoints, PID states
- **Teensy:** Connection state, pin states
- **cDAQ:** Module status, thermocouple readings

---

## User Interface

### Main Panel Structure

The UI is organized using LabWindows/CVI's built-in panel system with:

**1. Main Control Areas:**
- **Manual Control Section:** Direct device parameter entry
- **Status Indicators:** Real-time device connection states
- **System Controls:** Remote mode, emergency stops

**2. Tabbed Experiment Interface:**
- **CDC Tab:** Charge/discharge experiment controls
- **Baseline Tab:** Long-term monitoring experiment setup

**3. Real-Time Displays:**
- **Graphs:** Voltage, current, and power vs. time
- **Digital Readouts:** Precise numerical values
- **Status Messages:** Current operation status

**4. Device-Specific Controls:**
- **PSB Controls:** Voltage/current setpoints, output enable
- **DTB Controls:** Temperature setpoints, run/stop buttons  
- **Bio-Logic Status:** Technique progress, connection state

### Control Flow Management

**Thread Safety in UI Updates:**
- Background device operations run in separate threads
- UI updates use LabWindows/CVI's `PostDeferredCall()` mechanism
- Control dimming/enabling prevents user conflicts during experiments
- Modal dialogs for critical confirmations

**Experiment Control Logic:**
```c
// Typical experiment flow
if (experiment_running) {
    // Show "Stop" button, dim other controls
    DimExperimentControls(mainPanel, tabPanel, 1, controlList, numControls);
} else {
    // Show "Start" button, enable all controls  
    DimExperimentControls(mainPanel, tabPanel, 0, controlList, numControls);
}
```

---

## Error Handling and Safety

### Comprehensive Error System

**Error Code Hierarchy:**
```c
#define SUCCESS                 0

// System errors (-1000 to -1999)
#define ERR_BASE_SYSTEM         -1000
#define ERR_INVALID_PARAMETER   (ERR_BASE_SYSTEM - 1)
#define ERR_TIMEOUT             (ERR_BASE_SYSTEM - 6)
#define ERR_COMM_FAILED         (ERR_BASE_SYSTEM - 10)

// Device-specific error ranges
#define ERR_BASE_PSB            -2000  // PSB 10000 errors
#define ERR_BASE_BIO            -3000  // Bio-Logic errors  
#define ERR_BASE_CDAQ           -4000  // cDAQ errors
#define ERR_BASE_DTB            -8000  // DTB errors
#define ERR_BASE_TNY            -9000  // Teensy errors
```

### Safety Mechanisms

**1. Hardware Protection:**
- Over-voltage/current/power protection on PSB
- Temperature limits on DTB controllers  
- Communication timeouts prevent lockups
- Emergency stop capability

**2. Software Protection:**
- Parameter validation before sending to devices
- Automatic safe shutdown on errors
- Connection monitoring with auto-reconnect
- Experiment timeout limits

**3. User Protection:**
- Confirmation dialogs for destructive operations
- Clear error messages with corrective actions
- Automatic UI state restoration after errors
- Prevention of conflicting operations

**4. Data Protection:**
- Graceful handling of partial data loss
- Automatic retry mechanisms
- Transaction rollback on failures
- Consistent state maintenance

---

## Threading Model

### Thread Pool Architecture

**Main Threads:**
1. **UI Thread:** LabWindows/CVI main thread for UI updates
2. **Device Queue Threads:** One per device for sequential command processing
3. **Experiment Threads:** Background threads for long-running experiments  
4. **Status Monitor Thread:** Continuous device status checking

**Thread Safety Mechanisms:**
- **Queue System:** Thread-safe command queuing with priorities
- **Mutex Protection:** Global busy flags and resource locks
- **Deferred UI Updates:** Background threads post UI changes to main thread
- **Transaction Atomicity:** Grouped commands execute as single units

**Example Thread Coordination:**
```c
// User clicks "Start Experiment" (UI Thread)
├── Set global busy flag (prevents other operations)
├── Launch experiment thread (Background)
│   ├── Queue device commands (Thread-safe)
│   ├── Wait for command completion  
│   ├── Post UI updates via PostDeferredCall()
│   └── Clear busy flag on completion
└── Update UI immediately (dim controls, change button text)
```

---

## File Organization and Dependencies

### Project Structure
```
battery-tester/
├── BatteryTester.c/h/uir          # Main application
├── common.h/c                     # Shared definitions  
├── device_queue.h/c               # Generic queue system
├── battery_utils.h/c              # Battery calculations
├── logging.h/c                    # Logging system
├── status.h/c                     # Status monitoring
├── controls.h/c                   # UI control management
├── cdaq_utils.h/c                 # NI-DAQmx interface
├── cmd_prompt.h/c                 # Debug console
│
├── biologic/                      # Bio-Logic SP-150e
│   ├── biologic_dll.h/c           # Core API wrapper
│   ├── biologic_queue.h/c         # Queue management
│   └── BLStructs.h                # Bio-Logic structures
│
├── psb10000/                      # PSB 10000 Power Supply
│   ├── psb10000_dll.h/c           # Modbus RTU interface
│   └── psb10000_queue.h/c         # Queue management
│
├── dtb4848/                       # DTB4848 Temperature Controllers
│   ├── dtb4848_dll.h/c            # Modbus ASCII interface  
│   └── dtb4848_queue.h/c          # Queue management
│
├── teensy/                        # Teensy Microcontroller
│   ├── teensy_dll.h/c             # Serial interface
│   └── teensy_queue.h/c           # Queue management
│
├── tests/                         # Test Suites
│   ├── biologic_test.h/c          # Bio-Logic validation
│   ├── psb10000_test.h/c          # PSB validation
│   ├── device_queue_test.h/c      # Queue system tests
│   └── ...
│
├── exp_baseline.h/c               # Baseline experiment
└── exp_cdc.h/c                    # CDC experiment
```

### Key Dependencies

**External Libraries:**
- **LabWindows/CVI 2020 Runtime** - Core CVI functionality
- **NIDAQmx** - National Instruments data acquisition
- **Bio-Logic Development Package** - SP-150e communication
- **Windows RS232 Libraries** - Serial communication

**Internal Dependencies:**
- All device modules depend on `device_queue.h/c`
- All modules include `common.h` for shared definitions
- Experiment modules depend on device queue managers
- UI updates depend on `logging.h` for thread-safe operations

---

## Configuration and Setup

### Hardware Configuration

**Serial Port Assignments (configurable in common.h):**
```c
#define PSB_COM_PORT            3       // PSB 10000 COM port
#define DTB_COM_PORT            5       // DTB 4848 COM port  
#define PSB_SLAVE_ADDRESS       1       // PSB Modbus slave address
#define PSB_BAUD_RATE           9600    // PSB baud rate
```

**Device Enable Flags:**
```c
#define ENABLE_PSB         1    // Enable PSB 10000 monitoring
#define ENABLE_BIOLOGIC    1    // Enable BioLogic SP-150e monitoring  
#define ENABLE_DTB         1    // Enable DTB4848 monitoring
#define ENABLE_TNY         1    // Enable Teensy monitoring
#define ENABLE_CDAQ        1    // Enable cDAQ 9178
```

**Safety Limits (PSB 10000 - 60V/60A derated version):**
```c
#define PSB_NOMINAL_VOLTAGE     60.0    // V
#define PSB_NOMINAL_CURRENT     60.0    // A  
#define PSB_NOMINAL_POWER       1200.0  // W
#define PSB_SAFE_VOLTAGE_MAX    61.2    // V (102% of nominal)
#define PSB_SAFE_CURRENT_MAX    61.2    // A (102% of nominal)
```

### Software Configuration

**Queue Sizes and Timeouts:**
```c
#define DEVICE_QUEUE_HIGH_PRIORITY_SIZE    20
#define DEVICE_QUEUE_NORMAL_PRIORITY_SIZE  20  
#define DEVICE_QUEUE_COMMAND_TIMEOUT_MS    30000
#define THREAD_POOL_SIZE                   10
```

**Update Rates:**
```c
#define STATUS_UPDATE_RATE_HZ              1     // Status monitoring frequency
#define CDC_GRAPH_UPDATE_RATE             1.0   // Graph update during experiments
```

---

## Troubleshooting Guide

### Common Issues and Solutions

**1. Device Connection Problems**

*Symptoms:* Red status indicators, "Device not connected" messages

*Troubleshooting Steps:*
- Check physical connections (USB, serial cables)  
- Verify COM port assignments in `common.h`
- Check Windows Device Manager for port conflicts
- Try different baud rates or slave addresses
- Use device test suites to isolate problems:
  ```c
  // In main menu: Tests → PSB Test Suite
  // Or call directly:
  PSB_TestSuite_Run(psbHandle, panelHandle, statusControl);
  ```

**2. Queue System Deadlocks**

*Symptoms:* System appears frozen, commands not executing

*Troubleshooting Steps:*
- Check system busy state: `g_systemBusy` flag
- Look for unmatched busy flag set/clear operations
- Examine queue statistics: `DeviceQueue_GetStats()`
- Check for circular dependencies between device operations
- Verify proper transaction commit/rollback

**3. Thread Safety Issues**

*Symptoms:* Intermittent crashes, corrupted data, UI freezing

*Troubleshooting Steps:*
- Enable debug mode: `g_debugMode = 1`
- Check for UI updates from background threads (use `PostDeferredCall()`)
- Verify all device operations use queue system, not direct calls
- Check mutex usage around shared data structures
- Look for race conditions in experiment state management

**4. Memory Leaks**

*Symptoms:* Gradually increasing memory usage, eventual crash

*Common Sources:*
- Bio-Logic technique data not freed: `BIO_FreeTechniqueData()`
- Queue command parameters not freed by adapters
- UI controls not properly disposed
- Thread handles not cleaned up

**5. Communication Timeouts**

*Symptoms:* Slow operations, timeout errors in log

*Troubleshooting Steps:*
- Check cable quality and length
- Reduce communication frequency in status monitoring  
- Increase timeout values for slow devices
- Check for electrical interference
- Verify device power supply stability

### Debug Features

**1. Debug Logging**
```c
// Enable detailed logging
g_debugMode = 1;
LogDebug("Detailed information: %d", value);
```

**2. Queue Statistics**
```c
DeviceQueueStats stats;
DeviceQueue_GetStats(queueManager, &stats);
LogMessage("Queued: H=%d N=%d L=%d, Processed=%d, Errors=%d", 
          stats.highPriorityQueued, stats.normalPriorityQueued, 
          stats.lowPriorityQueued, stats.totalProcessed, stats.totalErrors);
```

**3. Device Test Suites**
Each device has comprehensive test suites that can be run independently:
- Connection testing
- Parameter validation  
- Communication reliability
- Error recovery testing
- Performance benchmarking

**4. Status Monitoring**
The status system provides real-time visibility into:
- Device connection states
- Queue depths and processing rates
- Error frequencies
- Communication timing

---

## Development Guidelines

### Adding New Devices

To integrate a new device into the system:

**1. Create Device DLL Module**
```c
// newdevice/newdevice_dll.h/c
#include "common.h"

typedef struct {
    // Device-specific handle structure
} NEWDEV_Handle;

// Core functions
int NEWDEV_Initialize(NEWDEV_Handle *handle, /* connection params */);
int NEWDEV_TestConnection(NEWDEV_Handle *handle);
// ... device-specific functions
```

**2. Implement Device Adapter**
```c
// newdevice/newdevice_queue.c  
static const DeviceAdapter g_newdevAdapter = {
    .deviceName = "New Device",
    .connect = NEWDEV_AdapterConnect,
    .disconnect = NEWDEV_AdapterDisconnect,  
    .testConnection = NEWDEV_AdapterTestConnection,
    .isConnected = NEWDEV_AdapterIsConnected,
    .executeCommand = NEWDEV_AdapterExecuteCommand,
    // ... other adapter functions
};
```

**3. Integrate with Main Application**
```c
// Add to BatteryTester.c
#include "newdevice_queue.h"

// Global queue manager
NEWDEVQueueManager *g_newdevQueueMgr = NULL;

// Initialize in main()
g_newdevQueueMgr = NEWDEV_QueueInitialize(g_threadPool, connectionParams);

// Add to status monitoring in status.c
// Add to cleanup in BatteryTester.c
```

### Code Style Guidelines

**1. Naming Conventions**
- **Functions:** `ModuleName_FunctionName()` (e.g., `PSB_SetVoltage()`)
- **Types:** `ModuleName_TypeName` (e.g., `PSB_Status`)  
- **Constants:** `MODULE_CONSTANT_NAME` (e.g., `PSB_NOMINAL_VOLTAGE`)
- **Global Variables:** `g_variableName` (e.g., `g_mainPanelHandle`)

**2. Error Handling**
- Always check return values from device functions
- Use consistent error codes from `common.h`
- Provide meaningful error messages with context
- Clean up resources in error paths

**3. Thread Safety**
- Use device queues for all device communication
- Never call device DLL functions directly from UI callbacks
- Use `PostDeferredCall()` for UI updates from background threads
- Protect shared data with mutexes

**4. Memory Management**  
- Always free dynamically allocated memory
- Use consistent allocation/deallocation patterns
- Check for NULL pointers before use
- Initialize structures with `memset()` or initialization

### Testing Requirements

**Unit Testing**
Each module should have comprehensive test coverage:
- Connection/disconnection testing
- Parameter validation
- Error condition handling  
- Performance characteristics

---

*Last Updated: Based on analysis of BatteryTester.prj version 1.0*  
