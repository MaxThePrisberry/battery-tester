# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is a battery testing system built in **LabWindows/CVI 2020 (C99)** for automated battery characterization. It integrates 5 hardware devices (PSB 10000 Power Supply, Bio-Logic SP-150e Potentiostat, DTB4848 Temperature Controllers, Teensy Microcontroller, cDAQ-9178) through a thread-safe command queue architecture.

## Build System

This is a LabWindows/CVI project (`.prj` file):

- **Build**: Open `BatteryTester.prj` in LabWindows/CVI 2020 IDE and use the IDE's build system
- **No command-line build**: LabWindows/CVI projects must be built through the IDE
- **Dependencies**: Requires NIDAQmx, Bio-Logic Development Package, and Windows RS232 libraries

## Architecture Overview

### Thread-Safe Queue System (Core Pattern)

The system's central architectural pattern is a **thread-safe device command queue** with priority levels. This prevents race conditions when multiple parts of the application need device access:

- **3 Priority Levels**: HIGH (user commands), NORMAL (experiments), LOW (status monitoring)
- **Sequential Execution**: Commands execute in order within each priority level
- **Transaction Support**: Group commands atomically (all succeed or all fail)
- **Auto-Reconnection**: Automatic recovery from communication failures

**Key Files**: `device_queue.h/c` (generic implementation), `{device}_queue.h/c` (device-specific wrappers)

### Device Integration Pattern

Each hardware device follows the same integration pattern:

1. **DLL Layer** (`{device}_dll.h/c`): Direct hardware communication (Modbus, USB, Serial)
2. **Queue Layer** (`{device}_queue.h/c`): Thread-safe command queuing using `DeviceAdapter` interface
3. **Integration**: Global queue manager in `BatteryTester.c`, status monitoring in `status.c`

### Hardware Devices

| Device | Purpose | Communication | Key Files |
|--------|---------|---------------|-----------|
| PSB 10000 | Bidirectional power supply (charge/discharge) | Modbus RTU (COM3) | `psb10000/` |
| Bio-Logic SP-150e | Electrochemical impedance spectroscopy (EIS) | USB (Bio-Logic API) | `biologic/` |
| DTB4848 | PID temperature controllers | Modbus ASCII (COM5) | `dtb4848/` |
| Teensy | Digital I/O and relay switching | Serial (COM6) | `teensy/` |
| cDAQ-9178 | Thermocouple monitoring | NIDAQmx | `cdaq_utils.h/c` |

### Relay Switching (Teensy)

The Teensy controls relays that connect/disconnect the PSB and Bio-Logic from the battery. Only one device can be connected at a time. Pin definitions are in `common.h`:

```c
#define TNY_PSB_PIN          0   // Teensy pin for PSB relay
#define TNY_BIOLOGIC_PIN     1   // Teensy pin for BioLogic relay
#define TNY_SWITCH_DELAY_MS  100 // Delay after relay switching
```

Switching functions in `exp_baseline.c`:
- **`SwitchToPSB()`**: Disconnect Bio-Logic relay → delay → connect PSB relay → enable PSB output
- **`SwitchToBioLogic()`**: Disable PSB output → disconnect PSB relay → delay → connect Bio-Logic relay

The sequence always disconnects the current device before connecting the next, with delays for safe settling.

### Configuration

Device parameters are in `common.h`:
- COM port assignments: `PSB_COM_PORT`, `DTB_COM_PORT`, `TNY_COM_PORT`
- Device enable flags: `ENABLE_PSB`, `ENABLE_BIOLOGIC`, `ENABLE_DTB`, `ENABLE_TNY`, `ENABLE_CDAQ`
- Safety limits: `PSB_SAFE_VOLTAGE_MAX`, `PSB_SAFE_CURRENT_MAX`

### Threading Model

- **UI Thread**: LabWindows/CVI main thread (never block this)
- **Device Queue Threads**: One per device for sequential command processing
- **Experiment Threads**: Background threads for long-running operations
- **Status Monitor Thread**: Continuous 1Hz device status checking

**Critical**: Never call device DLL functions directly from UI callbacks. Always use queue managers.

## Experiment Modules

### CDC Experiment (`exp_cdc.h/c`)

Charge/Discharge Control with real-time monitoring. Simple single-operation experiment: charges or discharges to a target voltage using `Battery_GoToVoltage()`.

### Baseline Experiment (`exp_baseline.h/c`)

Comprehensive 4-phase battery characterization experiment:

**Phase 1 - Discharge & Temperature** (`RunPhase1_DischargeAndTemp`):
Discharges battery to `dischargeVoltage` to establish 0% SOC baseline. If DTB enabled, waits for target temperature and stabilization. Measures initial discharge capacity.

**Phase 2 - Capacity Test** (`RunPhase2_CapacityExperiment`):
Full charge/discharge cycle to measure battery capacity. Charges to `chargeVoltage`, then discharges back to `dischargeVoltage`. The measured capacities (`measuredChargeCapacity_mAh`, `measuredDischargeCapacity_mAh`) are used by later phases for SOC calculation.

**Phase 3 - EIS During Charge** (`RunPhase3_EISCharge`):
Charges battery with periodic EIS measurements at SOC intervals defined by `eisInterval`. At each SOC target: disables PSB → switches relay to Bio-Logic → performs relaxation EIS series (multiple GEIS measurements over relaxation period to capture impedance dynamics) → switches relay back to PSB → resumes charging. Configuration constants in `exp_baseline.h`:
- `BASELINE_RELAXATION_EIS_INTERVAL`: seconds between EIS measurements during relaxation
- `BASELINE_RELAXATION_EIS_DURATION`: total relaxation measurement window
- Dynamic SOC targets are added if battery capacity was underestimated

**Phase 4 - Discharge to 50%** (`RunPhase4_Discharge50Percent`):
Discharges to exactly 50% of Phase 1's measured discharge capacity using `Battery_TransferCapacity()` for precise coulomb-counted capacity control.

**Data flow**: Phase 1 → `measuredDischargeCapacity_mAh` → Phase 4 uses it. Phase 2 → capacity estimates → Phase 3 uses for SOC tracking.

## Common Development Tasks

### Reading Device Status

```c
// Blocking (waits for result)
PSB_Status status;
int error = PSB_GetStatusQueued(&status, DEVICE_PRIORITY_LOW);

// Async (callback when complete)
void MyCallback(DeviceCommandID cmdId, int cmdType, void *result, void *userData) {
    PSB_Status *status = (PSB_Status*)result;
    // Use status
}
PSB_GetStatusAsync(MyCallback, userData, DEVICE_PRIORITY_LOW);
```

### Setting Device Parameters

```c
// Queue a command at NORMAL priority
int error = PSB_SetVoltageQueued(targetVoltage, DEVICE_PRIORITY_NORMAL);
```

### Atomic Operations (Transactions)

```c
// Configure PSB atomically - all commands succeed or all fail
DeviceTransactionHandle txn = DeviceQueue_BeginTransaction(g_psbQueueMgr);
DeviceQueue_AddToTransaction(g_psbQueueMgr, txn, PSB_CMD_SET_VOLTAGE, &voltageParams);
DeviceQueue_AddToTransaction(g_psbQueueMgr, txn, PSB_CMD_SET_CURRENT, &currentParams);
DeviceQueue_CommitTransaction(g_psbQueueMgr, txn, callback, userData);
```

### UI Updates from Background Threads

**Never update UI directly from background threads**. Use `PostDeferredCall()`:

```c
UIUpdateData *data = malloc(sizeof(UIUpdateData));
data->panel = panel; data->control = control; data->value = newValue;
PostDeferredCall(UpdateUICallback, data);
```

### Logging

Thread-safe logging with device-specific prefixes:

```c
LogMessageEx(LOG_DEVICE_PSB, "Setting voltage to %.2f V", voltage);
LogErrorEx(LOG_DEVICE_BIO, "Failed to load technique: %s", errorMsg);
LogWarning("Temperature elevated: %.1f°C", temp);
```

## Adding a New Device

1. **Create DLL module** (`newdevice/newdevice_dll.h/c`):
   - Implement `NEWDEV_Initialize()`, `NEWDEV_TestConnection()`, device-specific functions
   - Follow naming convention: `NEWDEV_FunctionName()`

2. **Implement Device Adapter** (`newdevice/newdevice_queue.h/c`):
   ```c
   static const DeviceAdapter g_newdevAdapter = {
       .deviceName = "New Device",
       .connect = NEWDEV_AdapterConnect,
       .executeCommand = NEWDEV_AdapterExecuteCommand,
       // ... implement all adapter functions
   };
   ```

3. **Integrate with main application**:
   - Add queue manager to `BatteryTester.c`: `NEWDEVQueueManager *g_newdevQueueMgr`
   - Initialize in `main()`: `g_newdevQueueMgr = NEWDEV_QueueInitialize(...)`
   - Add to status monitoring in `status.c`
   - Add cleanup code to `BatteryTester.c`

## Error Handling

Error codes are hierarchical (defined in `common.h`):
- System errors: `-1000` to `-1999` (`ERR_INVALID_PARAMETER`, `ERR_TIMEOUT`, etc.)
- Device-specific: `ERR_BASE_PSB` (-3000), `ERR_BASE_BIO` (-2000), `ERR_BASE_DTB` (-8000), etc.

Always check return values and clean up resources in error paths.

## Code Style

- **Functions**: `ModuleName_FunctionName()` (e.g., `PSB_SetVoltage()`)
- **Types**: `ModuleName_TypeName` (e.g., `PSB_Status`)
- **Constants**: `MODULE_CONSTANT_NAME` (e.g., `PSB_NOMINAL_VOLTAGE`)
- **Globals**: `g_variableName` (e.g., `g_mainPanelHandle`)

## Key Architectural Principles

1. **Never call device functions directly from UI thread** - always use queue managers
2. **Use PostDeferredCall() for UI updates from background threads**
3. **Transactions for atomic operations** - configuration changes that must succeed together
4. **Priority-based queuing** - HIGH for user commands, NORMAL for experiments, LOW for status
5. **Resource cleanup in error paths** - check for memory leaks in experiment threads
6. **Thread-safe logging** - use `LogMessageEx()` not direct text box updates

## Bio-Logic Dual-Mode Integration (EC-Lab + DLL)

The Bio-Logic SP-150e supports **dual-mode operation** with compile-time mode switching:

### Mode Selection

Control mode is set in `common.h`:
```c
#define BIOLOGIC_CONTROL_MODE  0  // 0 = Direct DLL, 1 = EC-Lab OLE COM
```

**Switching modes:**
1. Change `BIOLOGIC_CONTROL_MODE` in `common.h`
2. Rebuild project (mode is compile-time)
3. No code changes needed - abstraction layer handles everything

### Modes Explained

**Direct DLL Mode (0)** - Production/Automation
- Direct hardware control via ECLib64.dll
- No GUI dependency
- Use for: Production experiments, automated testing

**EC-Lab OLE COM Mode (1)** - Development/Debugging
- Software control via EC-Lab GUI automation
- Visual monitoring in EC-Lab interface
- Parameter changes without recompiling (.mps files)
- Use for: Development, debugging, validation

### Abstraction Layer

All Bio-Logic functionality routes through the abstraction layer (`biologic/biologic_abstract.h/c`):

```c
BIO_Abstract_RunOCV(...)   // Auto-routes to DLL or EC-Lab
BIO_Abstract_RunGEIS(...)  // Based on BIOLOGIC_CONTROL_MODE
BIO_Abstract_RunPEIS(...)
```

### BIO Command-Line Interface

Quick testing and verification via the application's command prompt:

```
BIO MODE   - Show current control mode (DLL/EC-Lab)
BIO TEST   - Test connection to device
BIO ID     - Get device ID
BIO OCV    - Run quick 10s OCV measurement
BIO GEIS   - Run quick GEIS test (10kHz-0.1Hz, 500mA)
BIO HELP   - Show command help
```

### EC-Lab Setup (One-Time)

**Requirements for EC-Lab mode:**
1. EC-Lab registered as COM server (admin: `EClab.exe /regserver`)
2. Directories created: `eclab_settings/` (.mps templates), `eclab_data/` (.mpr output)
3. EC-Lab running before launching application

**Configuration in common.h:**
```c
#define ECLAB_SETTINGS_DIR    "C:\\Users\\...\\battery-tester\\eclab_settings"
#define ECLAB_DATA_DIR        "C:\\Users\\...\\battery-tester\\eclab_data"
#define ECLAB_DEVICE_NUMBER   0    // EC-Lab device index
#define ECLAB_CHANNEL_NUMBER  0    // EC-Lab channel index
```

### Bio-Logic Techniques Supported

- **OCV**: Open Circuit Voltage monitoring
- **PEIS**: Potentiostatic EIS (voltage-controlled impedance)
- **GEIS**: Galvanostatic EIS (current-controlled impedance)
- **SPEIS**: Staircase PEIS (multi-voltage impedance mapping)

Default GEIS parameters in `common.h`:
- Frequency range: 10 kHz to 0.1 Hz (logarithmic, 31 points)
- Amplitude: 500 mA
- 5 decades x 6 points/decade + 1 final point

## Important Files Reference

| File | Purpose |
|------|---------|
| `common.h` | Shared definitions, error codes, device configuration |
| `device_queue.h/c` | Generic thread-safe queue implementation |
| `battery_utils.h/c` | Battery calculations (capacity, efficiency, coulomb counting) |
| `logging.h/c` | Thread-safe logging system |
| `status.h/c` | Continuous device status monitoring |
| `controls.h/c` | UI control management utilities |
| `BatteryTester.c` | Main application entry point |

## Testing

Device test suites are in `tests/`:
- `psb10000_test.h/c`: PSB connection, parameter validation, communication reliability
- `biologic_test.h/c`: Bio-Logic technique validation
- `device_queue_test.h/c`: Queue system stress testing

Run tests via UI: Tests menu -> Device Test Suite
