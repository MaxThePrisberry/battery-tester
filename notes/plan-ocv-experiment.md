# OCV Experiment Implementation Plan

## Overview
Implement a dedicated OCV (Open Circuit Voltage) experiment that:
1. Rests the battery at a constant temperature for a specified time
2. Measures OCV using the Bio-Logic SP-150e through EC-Lab
3. Available as standalone experiment AND as new Phase 0 in baseline

## Files to Create

### 1. `exp_ocv.h` - Header file
- State enum: `OCV_STATE_IDLE`, `PREPARING`, `SWITCHING_RELAY`, `TEMP_WAIT`, `TEMP_STABILIZE`, `RESTING`, `MEASURING`, `COMPLETED`, `ERROR`, `CANCELLED`
- `OCVExperimentParams` struct: batteryName, targetTemperature, tempTolerance, restTime, enableTempControl, logInterval
- `OCVTemperatureData` struct: timestamp, dtbTemperatures[], dtbAverageTemperature, tc0/tc1 temperatures
- `OCVMeasurementResult` struct: finalOCV_V, averageOCV_V, minOCV_V, maxOCV_V, measurementDuration_s, numDataPoints, tempAtMeasurement, rawOCVData
- `OCVExperimentContext` struct: state, params, cancelRequested, timing fields, temperature flags, result, file handles, UI handles

Public functions:
- `StartOCVExperimentCallback()` - UI button callback
- `OCVExperiment_IsRunning()`, `OCVExperiment_Abort()`, `OCVExperiment_Cleanup()`
- `OCV_RunExperiment()` - For baseline integration (runs full OCV with temp control)
- `OCV_QuickMeasurement()` - Quick OCV without temp/rest (for use within other experiments)
- `OCV_FreeResult()` - Memory cleanup

### 2. `exp_ocv.c` - Implementation
Follow patterns from `exp_cdc.c` and `exp_baseline.c`:

**Static module variables:**
- `g_ocvContext` - Experiment context
- `g_ocvThreadId` - Thread pool function ID
- `g_ocvControls[]` - Controls to dim during experiment

**Internal functions (reuse patterns from exp_baseline.c):**
- `VerifyDevices()` - Check Bio-Logic connected
- `CreateFileSystem()` - Create timestamped directory with battery name
- `SwitchToBioLogic()` - Relay switching (reuse from exp_baseline.c:2133-2162)
- `SetupTemperatureControl()` - Set DTB target, start controllers
- `WaitForTargetTemperature()` - Poll until all DTB within tolerance (reuse pattern from exp_baseline.c:1913)
- `StabilizeTemperature()` - Wait 5 min while stable, restart on drift (reuse pattern from exp_baseline.c:1997)
- `RunRestPeriod()` - Log temperatures at intervals during rest time
- `RunOCVMeasurement()` - Call `BIO_Abstract_RunOCV()`, extract final voltage
- `ReadAllTemperatures()` - Read DTB and thermocouple data
- `SaveResults()` - Write INI summary file
- `CleanupExperiment()` - Close files, cleanup resources

**Thread function `OCVExperimentThread()`:**
1. Create file system
2. Save experiment settings
3. Switch to Bio-Logic relay
4. If temp control enabled: setup → wait → stabilize
5. Rest for specified time (log temps periodically)
6. Run OCV measurement
7. Save results
8. Cleanup (disconnect relay, restore UI, clear busy flag)

## Files to Modify

### 3. `BatteryTester.uir` - Add OCV tab
New tab page with controls:
| Control | Type | Default | Description |
|---------|------|---------|-------------|
| `OCV_BATTERYNAME` | string | "" | Battery name |
| `OCV_NUM_TEMPERATURE` | numeric | 25.0 | Target temperature (C) |
| `OCV_NUM_REST_TIME` | numeric | 300 | Rest time (seconds) |
| `OCV_CHK_TEMP_CONTROL` | checkbox | 0 | Enable temperature control |
| `OCV_NUM_INTERVAL` | numeric | 10 | Temperature log interval (s) |
| `OCV_BTN_START` | command | "Start" | Start/Stop button |
| `OCV_STR_STATUS` | string | "" | Status display |
| `OCV_NUM_OUTPUT` | numeric | 0.0 | Final OCV output (V) |

### 4. `BatteryTester.c` - Main application
- Add `#include "exp_ocv.h"`
- Add `OCVExperiment_Cleanup()` call in cleanup section

### 5. `BatteryTester.prj` - Project file
- Add `exp_ocv.c` and `exp_ocv.h` to project sources

### 6. `exp_baseline.h` - Add Phase 0
- Update `BaselinePhase` enum: add `BASELINE_PHASE_OCV = 0` before `BASELINE_PHASE_DISCHARGE`
- Rename existing phases: PHASE_1 -> PHASE_1 (discharge), etc.
- Add to `BaselineExperimentParams`: `int runOCVPhase`, `double ocvRestTime`
- Add to `BaselineExperimentContext`: `OCVMeasurementResult phase0Results`

### 7. `exp_baseline.c` - Integrate Phase 0
- Add `#include "exp_ocv.h"`
- Add `RunPhase0_OCV()` function that calls `OCV_RunExperiment()`
- Call `RunPhase0_OCV()` at start of `BaselineExperimentThread()` before Phase 1
- Add Phase 0 UI controls to baseline tab (checkbox to enable, rest time, duration)
- Update `WriteComprehensiveResults()` to include Phase 0 OCV data

## Experiment Flow (Standalone)

```
User clicks Start
    |
Verify Bio-Logic connected
    |
Create experiment directory (data/{battery}_{timestamp}_ocv/)
    |
Switch relay: PSB off -> disconnect PSB -> connect Bio-Logic
    |
[If temp control enabled]
    Set DTB target temperature
    Wait until all DTB within +/-2C (poll every 10s, 45min timeout)
    Stabilize for 5 minutes (restart if drift detected)
    |
Rest for specified time
    Log temperatures every {interval} seconds to rest_temperatures.csv
    |
Run OCV measurement via BIO_Abstract_RunOCV()
    Extract final, average, min, max voltage
    |
Save results:
    - ocv_results.ini (summary)
    - ocv_measurement.csv (raw OCV data)
    - rest_temperatures.csv (temp during rest)
    |
Cleanup: disconnect Bio-Logic relay, restore UI
```

## Baseline Integration (Phase 0)

Phase 0 runs before Phase 1 discharge:
1. Switches to Bio-Logic
2. Optionally waits for/stabilizes temperature
3. Rests for configured time
4. Measures initial OCV
5. Saves to `phase_0/` subdirectory
6. Switches back to PSB for Phase 1

This establishes the initial battery OCV before any discharge/charge operations.

## Key Functions to Reuse

| Function | Source File | Lines | Purpose |
|----------|-------------|-------|---------|
| `SwitchToBioLogic()` | exp_baseline.c | 2133-2162 | Relay switching pattern |
| `WaitForTargetTemperature()` | exp_baseline.c | 1913-1995 | Temperature wait logic |
| `StabilizeTemperature()` | exp_baseline.c | 1997-2088 | Stability monitoring |
| `ReadAllTemperatures()` | exp_baseline.c | 2654-2700 | DTB/thermocouple reading |
| `BIO_Abstract_RunOCV()` | biologic_abstract.c | 150-200 | OCV measurement |
| `CreateTimestampedDirectoryWithBattery()` | common.c | - | Directory creation |

## Configuration Constants (add to exp_ocv.h)

```c
#define OCV_REST_DEFAULT_TIME        300.0   // 5 min default rest
#define OCV_TEMP_TOLERANCE           2.0     // C tolerance
#define OCV_TEMP_CHECK_INTERVAL      10.0    // Poll every 10s
#define OCV_TEMP_TIMEOUT_SEC         1800    // 30 min timeout
#define OCV_TEMP_STABILIZE_TIME      300     // 5 min stabilization
#define OCV_EXP_SAMPLE_INTERVAL_S    0.1     // 10 Hz sampling
#define OCV_EXP_TIMEOUT_MS           120000  // 2 min timeout
```

## Verification

1. **Standalone test**: Run OCV experiment from new tab, verify:
   - Creates correct directory structure
   - Temperature control works (if DTB enabled)
   - Rest period logs temperatures to CSV
   - OCV measurement runs and returns voltage
   - Results saved to INI file

2. **Baseline test**: Run baseline with Phase 0 enabled, verify:
   - Phase 0 runs before Phase 1
   - OCV data saved to phase_0/ subdirectory
   - Phase 0 results included in summary.txt

3. **Command line test**: Use `BIO OCV` command to verify Bio-Logic connectivity
