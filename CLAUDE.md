# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is a battery testing system built in **LabWindows/CVI 2020 (C99)** for automated battery characterization. It integrates 5 hardware devices (PSB 10000 Power Supply, Bio-Logic SP-150e Potentiostat, DTB4848 Temperature Controllers, Teensy Microcontroller, cDAQ-9178) through a sophisticated thread-safe command queue architecture.

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
| Teensy | Digital I/O control | Serial (COM6) | `teensy/` |
| cDAQ-9178 | Thermocouple monitoring | NIDAQmx | `cdaq_utils.h/c` |

### Configuration

Device parameters are in `common.h`:
- COM port assignments: `PSB_COM_PORT`, `DTB_COM_PORT`, `TNY_COM_PORT`
- Device enable flags: `ENABLE_PSB`, `ENABLE_BIOLOGIC`, `ENABLE_DTB`, `ENABLE_TNY`, `ENABLE_CDAQ`
- Safety limits: `PSB_SAFE_VOLTAGE_MAX`, `PSB_SAFE_CURRENT_MAX`

### Experiment Modules

- **CDC Experiment** (`exp_cdc.h/c`): Charge/Discharge Control with real-time monitoring
- **Baseline Experiment** (`exp_baseline.h/c`): Long-term monitoring with periodic EIS measurements

### Threading Model

- **UI Thread**: LabWindows/CVI main thread (never block this)
- **Device Queue Threads**: One per device for sequential command processing
- **Experiment Threads**: Background threads for long-running operations
- **Status Monitor Thread**: Continuous 1Hz device status checking

**Critical**: Never call device DLL functions directly from UI callbacks. Always use queue managers.

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
typedef struct {
    int panel;
    int control;
    double value;
} UIUpdateData;

void UpdateUICallback(void *callbackData) {
    UIUpdateData *data = (UIUpdateData*)callbackData;
    SetCtrlVal(data->panel, data->control, data->value);
    free(data);
}

// In background thread:
UIUpdateData *data = malloc(sizeof(UIUpdateData));
data->panel = panel; data->control = control; data->value = newValue;
PostDeferredCall(UpdateUICallback, data);
```

### Logging

Thread-safe logging with device-specific prefixes:

```c
#include "logging.h"

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

## Bio-Logic EIS Techniques

The Bio-Logic SP-150e supports multiple electrochemical techniques:

- **OCV**: Open Circuit Voltage monitoring
- **PEIS**: Potentiostatic EIS (voltage-controlled impedance)
- **GEIS**: Galvanostatic EIS (current-controlled impedance)
- **SPEIS**: Staircase PEIS (multi-voltage impedance mapping)

Default GEIS parameters in `common.h`:
- Frequency range: 10 kHz to 0.1 Hz (logarithmic, 31 points)
- Amplitude: 500 mA
- 5 decades × 6 points/decade + 1 final point

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

Run tests via UI: Tests menu → Device Test Suite
