# EC-Lab OLE COM Integration Guide

## Overview

The Battery Exploder system now supports two modes for controlling the BioLogic SP-150e potentiostat:

1. **Direct DLL Mode** (default) - Direct hardware control via ECLib.dll
2. **EC-Lab OLE COM Mode** (new) - Control via EC-Lab GUI automation

This guide explains how to set up and use EC-Lab OLE COM mode.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│         Experiment Layer (exp_temp_ramp.c)          │
│              (No changes required)                  │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│     Abstraction Layer (biologic_abstract.h/c)       │
│         Automatic mode dispatching                  │
└─────────────────────────────────────────────────────┘
                         │
          ┌──────────────┴──────────────┐
          ▼                             ▼
┌──────────────────────┐    ┌──────────────────────┐
│   Direct DLL Mode    │    │   EC-Lab OLE COM     │
│   biologic_dll.c     │    │   biologic_eclab.c   │
│   biologic_queue.c   │    │   eclab_olecom.c     │
│         │            │    │         │            │
│         ▼            │    │         ▼            │
│   ECLib64.dll        │    │   EC-Lab.exe         │
│         │            │    │   (OLE COM Server)   │
│         ▼            │    │         │            │
│   SP-150e Hardware   │    │         ▼            │
│      via USB         │    │   SP-150e Hardware   │
└──────────────────────┘    └──────────────────────┘
```

---

## One-Time Setup

### Step 1: Register EC-Lab as OLE COM Server

**IMPORTANT:** This requires administrator privileges and must be done once.

1. Open Command Prompt **as Administrator**
2. Navigate to EC-Lab installation directory:
   ```cmd
   cd "C:\Program Files (x86)\EC-Lab"
   ```
3. Register EC-Lab:
   ```cmd
   ECLab.exe /regserver
   ```
4. No confirmation message will appear (this is normal)

### Step 2: Create Directory Structure

Create directories for .mps template files and output data:

```cmd
mkdir C:\BatteryExploder\eclab_settings
mkdir C:\BatteryExploder\eclab_data
```

### Step 3: Place Your .mps Files

Copy your existing .mps template files to the settings directory:

```
C:\BatteryExploder\eclab_settings\
├── ocv_default.mps      ← Your OCV settings from EC-Lab
├── peis_default.mps     ← Your PEIS settings from EC-Lab
└── geis_default.mps     ← Your GEIS settings from EC-Lab
```

**How to create .mps files in EC-Lab (if needed):**
1. Open EC-Lab GUI
2. Configure your technique with desired parameters
3. Go to File → Save Settings
4. Save as `.mps` file with appropriate name
5. Copy to `C:\BatteryExploder\eclab_settings\`

---

## Configuration

### Enable EC-Lab OLE COM Mode

Edit `common.h` and change the control mode:

```c
// BioLogic control mode selection
// 0 = Direct DLL mode (default)
// 1 = EC-Lab OLE COM mode
#define BIOLOGIC_CONTROL_MODE  1  // ← Change from 0 to 1
```

### Verify Settings

Check that these paths in `common.h` match your setup:

```c
// EC-Lab OLE COM mode settings
#define ECLAB_SETTINGS_DIR     "C:\\BatteryExploder\\eclab_settings"
#define ECLAB_DATA_DIR         "C:\\BatteryExploder\\eclab_data"
#define ECLAB_DEVICE_NUMBER    0       // EC-Lab device index
#define ECLAB_CHANNEL_NUMBER   0       // EC-Lab channel index
#define ECLAB_EXECUTABLE_PATH  "C:\\Program Files (x86)\\EC-Lab\\ECLab.exe"

// .mps template filenames
#define ECLAB_OCV_TEMPLATE     "ocv_default.mps"
#define ECLAB_PEIS_TEMPLATE    "peis_default.mps"
#define ECLAB_GEIS_TEMPLATE    "geis_default.mps"
```

Adjust these if:
- Your EC-Lab is installed in a different location
- Your .mps files have different names
- You're using a different device or channel number

### Rebuild Project

After changing `common.h`:
1. Open BatteryExploder.prj in LabWindows/CVI 2020
2. Build → Rebuild All

---

## Runtime Usage

### Starting Measurements

1. **Start EC-Lab** - EC-Lab must be running before starting Battery Exploder
2. **Connect device in EC-Lab** - Ensure your SP-150e is connected in EC-Lab
3. **Start Battery Exploder** - Run your application normally

The system will:
- Automatically connect to EC-Lab via OLE COM
- Connect to the configured device
- Use your .mps templates for all measurements

### Running Experiments

**No code changes required!** Your existing experiments work transparently:

```c
// This code works identically in both modes
BIO_TechniqueData *eisData = NULL;
int result = BIO_RunPEISQueued(
    deviceID, channel,
    // ... parameters ...
    &eisData,
    timeout,
    DEVICE_PRIORITY_NORMAL,
    progressCallback,
    userData,
    &cancelled
);
```

In EC-Lab mode:
- The parameters are ignored
- Settings come from your .mps template
- Results are returned in the same format
- Progress callbacks work the same way

---

## Key Differences Between Modes

| Feature | Direct DLL Mode | EC-Lab OLE COM Mode |
|---------|----------------|---------------------|
| **Performance** | Fast, direct hardware | Slightly slower (polling) |
| **Parameters** | Specified in code | From .mps file |
| **Monitoring** | Log files only | EC-Lab GUI + logs |
| **Debugging** | More difficult | Easier (GUI validation) |
| **Setup** | Firmware loading | EC-Lab must be running |
| **Dependencies** | ECLib.dll | EC-Lab installation |
| **Best for** | Production testing | Development, debugging, validation |

---

## Monitoring Experiments

### In Battery Exploder

All logging works the same in both modes:
- Check the log output textbox
- Status updates appear every few seconds
- Progress callbacks are called normally

### In EC-Lab GUI

**Bonus!** In OLE COM mode, you can watch experiments live in EC-Lab:
1. Keep EC-Lab window visible
2. Watch real-time graphs
3. See current status
4. View technique parameters

This is excellent for debugging and validation!

---

## Data Files

### Output Location

Measurement data is saved to:
```
C:\BatteryExploder\eclab_data\
├── ocv_20250127_143022.mpr
├── peis_20250127_143155.mpr
├── peis_20250127_143342.mpr
└── geis_20250127_143529.mpr
```

Files are automatically named with timestamp.

### .mpr File Format

- Binary format created by EC-Lab
- Can be opened in EC-Lab for analysis
- Converted automatically to `BIO_TechniqueData` format
- Original .mpr preserved for reference

---

## Troubleshooting

### "Failed to create EC-Lab COM object"

**Cause:** EC-Lab not registered as OLE COM server

**Solution:**
```cmd
cd "C:\Program Files (x86)\EC-Lab"
ECLab.exe /regserver
```

### "EC-Lab not running or not responding"

**Cause:** EC-Lab must be running

**Solution:**
1. Start EC-Lab application
2. Connect your device in EC-Lab
3. Then start Battery Exploder

### "Settings file not found: ..."

**Cause:** .mps template files not in correct location

**Solution:**
1. Check path in `common.h` (ECLAB_SETTINGS_DIR)
2. Verify .mps files exist in that directory
3. Check filenames match defines in `common.h`

### "Device not found"

**Cause:** Device number/channel mismatch

**Solution:**
1. Check device is connected in EC-Lab
2. Verify ECLAB_DEVICE_NUMBER in `common.h`
3. Verify ECLAB_CHANNEL_NUMBER in `common.h`

### "Measurement timeout"

**Possible causes:**
- Technique parameters in .mps file too long
- Device communication issues
- EC-Lab frozen or not responding

**Solution:**
1. Check EC-Lab is responding
2. Verify timeout value in code is adequate
3. Test technique manually in EC-Lab first

---

## Switching Between Modes

To switch modes:

1. Edit `common.h`:
   ```c
   #define BIOLOGIC_CONTROL_MODE  0  // For Direct DLL
   // or
   #define BIOLOGIC_CONTROL_MODE  1  // For EC-Lab OLE COM
   ```

2. Rebuild project

3. No other code changes needed!

---

## Advanced: Custom .mps Files Per Measurement

You can override the default templates:

```c
// Use custom .mps file for this specific measurement
result = BIO_ECLAB_RunPEIS(
    "C:\\BatteryExploder\\eclab_settings\\peis_high_freq.mps",  // Custom file
    NULL,  // Auto-generate output filename
    &eisData,
    timeout,
    progressCallback,
    userData,
    &cancelled
);
```

This allows:
- Different parameter sets for different experiments
- A/B testing of technique settings
- Specialized configurations per battery type

---

## Best Practices

### For Development

Use **EC-Lab OLE COM mode**:
- Validate techniques in EC-Lab GUI first
- Watch experiments in real-time
- Easy parameter adjustments (edit .mps, no recompile)
- Excellent for debugging

### For Production

Use **Direct DLL mode**:
- Maximum performance
- No EC-Lab dependency
- Lower overhead
- Automated environments

### For Validation

Use **both modes** and compare results:
- Run same experiment in both modes
- Compare impedance spectra
- Verify data consistency
- Builds confidence in both systems

---

## Example: Temperature Ramp EIS Experiment

Your existing experiment code requires **zero changes**:

```c
// exp_temp_ramp.c - PerformEISMeasurement()

// This code works in both modes transparently!
int result = BIO_RunPEISQueued(
    g_bioDeviceID,
    BIO_CHANNEL,
    true,                    // vs_initial
    0.0,                     // initial_voltage_step
    eisDuration,             // duration_step
    0.1,                     // record_every_dT
    0.0,                     // record_every_dI
    eisParams->freqStart,    // initial_freq
    eisParams->freqEnd,      // final_freq
    false,                   // sweep_linear (log)
    eisParams->amplitude,    // amplitude_voltage
    eisParams->numPoints,    // frequency_number
    eisParams->averaging,    // average_n_times
    false,                   // correction
    2.0,                     // wait_for_steady
    true,                    // processData
    &eisData,                // result
    timeout,
    DEVICE_PRIORITY_NORMAL,
    ProgressCallback,
    userData,
    &g_cancelled
);
```

Mode selection happens automatically based on `BIOLOGIC_CONTROL_MODE` in `common.h`.

---

## Files Added

The following new files were added for EC-Lab OLE COM support:

### Core Implementation
- `biologic/eclab_olecom.h` - OLE COM wrapper (low-level)
- `biologic/eclab_olecom.c` - Windows COM implementation
- `biologic/biologic_eclab.h` - High-level technique interface
- `biologic/biologic_eclab.c` - Technique execution and monitoring
- `biologic/biologic_abstract.h` - Unified abstraction layer
- `biologic/biologic_abstract.c` - Mode dispatching logic

### Documentation
- `biologic/ECLAB_INTEGRATION_GUIDE.md` - This file

### Modified Files
- `common.h` - Added EC-Lab configuration constants
- (No experiment files modified!)

---

## Next Steps

1. Complete one-time setup (register EC-Lab, create directories)
2. Place your .mps files in settings directory
3. Configure `common.h` for EC-Lab mode
4. Rebuild project
5. Test with simple OCV or EIS measurement
6. Run full temperature ramp experiment
7. Compare results with Direct DLL mode

---

## Support

For issues or questions:
- Check this guide's Troubleshooting section
- Review EC-Lab OLE COM User Manual (Bio-Logic)
- Examine log output for error messages
- Test technique manually in EC-Lab first

---

**Implementation Date:** January 2025
**Author:** Claude Code Integration
**Version:** 1.0
