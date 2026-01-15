# Battery Name Field - UI Implementation Guide

## Overview
This guide provides step-by-step instructions for adding battery name input fields to both the CDC and Baseline experiment tabs in the LabWindows/CVI user interface.

**IMPORTANT**: These changes must be performed on the remote computer with LabWindows/CVI 2020 installed.

---

## Prerequisites
- LabWindows/CVI 2020 IDE installed
- `BatteryTester.uir` file accessible
- Code changes from this commit already applied

---

## Part 1: Add UI Controls in LabWindows/CVI IDE

### Step 1: Open the UI Editor
1. Launch **LabWindows/CVI 2020**
2. Open the project: `BatteryTester.prj`
3. Double-click `BatteryTester.uir` to open the UI Editor

---

### Step 2: Add Battery Name Field to Baseline Tab

1. **Navigate to Baseline Tab**
   - In the UI Editor, select the **Baseline experiment tab**
   - Look for existing input controls (Target Temperature, EIS Interval, etc.)

2. **Add String Control**
   - From the Controls palette, select **String Control** (text input box)
   - Click to place it on the panel (recommended: near the top, above other parameters)

3. **Configure the Control**
   - Right-click the new control → **Properties**
   - Set the following properties:
     - **Control ID**: `BASELINE_BATTERYNAME`
     - **Label**: `Battery Name:`
     - **Data Type**: `String`
     - **Max Length**: `63`
     - **Default Value**: (leave empty)
     - **Width**: Similar to other string inputs (~200 pixels)

4. **Position the Control**
   - Place it logically with other input parameters
   - Suggested position: First input field at the top
   - Align with other controls for consistency

---

### Step 3: Add Battery Name Field to CDC Tab

1. **Navigate to CDC Tab**
   - In the UI Editor, select the **CDC experiment tab**
   - Look for existing input controls (Target Voltage, Target Current, etc.)

2. **Add String Control**
   - From the Controls palette, select **String Control** (text input box)
   - Click to place it on the panel (recommended: near the top)

3. **Configure the Control**
   - Right-click the new control → **Properties**
   - Set the following properties:
     - **Control ID**: `CDC_BATTERYNAME`
     - **Label**: `Battery Name:`
     - **Data Type**: `String`
     - **Max Length**: `63`
     - **Default Value**: (leave empty)
     - **Width**: Similar to other string inputs (~200 pixels)

4. **Position the Control**
   - Place it at the top of the input parameters
   - Align with other controls for consistency

---

### Step 4: Save and Generate Header

1. **Save the UI File**
   - File → Save (Ctrl+S)
   - The `.uir` file is now updated

2. **Generate Constants Header**
   - LabWindows/CVI should automatically generate updated constants
   - If not automatic: Design → Generate Constants → Confirm
   - This updates `BatteryTester.h` with new control IDs

---

## Part 2: Update Code to Read Battery Name

### Step 5: Update Baseline Experiment Code

1. **Open** `exp_baseline.c`

2. **Find the parameter reading section**
   - Search for the function where UI parameters are read
   - Look for code around line 630-680
   - Find existing `GetCtrlVal()` calls that read parameters

3. **Add battery name reading**
   - Add this line after other `GetCtrlVal()` calls:
   ```c
   GetCtrlVal(tabPanelHandle, BASELINE_BATTERYNAME, ctx->params.batteryName);
   ```

4. **Example context** (your code may look like this):
   ```c
   // Read experiment parameters from UI
   GetCtrlVal(tabPanelHandle, BASELINE_TARGETTEMP, &ctx->params.targetTemperature);
   GetCtrlVal(tabPanelHandle, BASELINE_EISINTERVAL, &ctx->params.eisInterval);
   GetCtrlVal(tabPanelHandle, BASELINE_BATTERYNAME, ctx->params.batteryName);  // ADD THIS
   GetCtrlVal(tabPanelHandle, BASELINE_CURRENTTHRESH, &ctx->params.currentThreshold);
   // ... more parameters
   ```

---

### Step 6: Update CDC Experiment Code

1. **Open** `exp_cdc.c`

2. **Find the parameter reading section**
   - Look in both `CDCChargeCallback()` and `CDCDischargeCallback()` functions
   - Find existing `GetCtrlVal()` calls

3. **Add battery name reading**
   - Add this line after other `GetCtrlVal()` calls:
   ```c
   GetCtrlVal(tabPanelHandle, CDC_BATTERYNAME, ctx.params.batteryName);
   ```

4. **Example context** (in both charge and discharge callbacks):
   ```c
   // Read parameters from UI
   GetCtrlVal(tabPanelHandle, CDC_TARGETVOLTAGE, &ctx.params.targetVoltage);
   GetCtrlVal(tabPanelHandle, CDC_TARGETCURRENT, &ctx.params.targetCurrent);
   GetCtrlVal(tabPanelHandle, CDC_BATTERYNAME, ctx.params.batteryName);  // ADD THIS
   GetCtrlVal(tabPanelHandle, CDC_CURRENTTHRESH, &ctx.params.currentThreshold);
   // ... more parameters
   ```

---

## Part 3: Build and Test

### Step 7: Build the Project

1. **Clean Build**
   - Build → Clean (removes old object files)

2. **Build Project**
   - Build → Build (or F7)
   - Fix any compilation errors

3. **Expected result**: Clean build with no errors

---

### Step 8: Test the Feature

1. **Run the Application**
   - Execute → Run (or Ctrl+F5)

2. **Test Baseline Experiment**
   - Navigate to Baseline tab
   - Enter a battery name: e.g., "INR18650-25R"
   - Start an experiment
   - Check that folder is created: `data_baseline/INR18650-25R_baseline_YYYYMMDD_HHMMSS/`
   - Check `experiment_settings.txt` contains: `Battery_Name = INR18650-25R`

3. **Test CDC Experiment**
   - Navigate to CDC tab
   - Enter a battery name: e.g., "Samsung_30Q"
   - Start a charge or discharge operation
   - Verify folder naming (if CDC creates folders - otherwise just verify parameter is stored)

4. **Test Edge Cases**
   - Leave battery name blank → Should create: `baseline_YYYYMMDD_HHMMSS/`
   - Use spaces: "Test Battery" → Should sanitize to: `Test_Battery_baseline_...`
   - Use special characters → Should replace with underscores

---

## Part 4: Verify Implementation

### Step 9: Check Generated Files

1. **Baseline Experiment Files**
   - Navigate to experiment directory
   - Open `experiment_settings.txt`
   - Verify `Battery_Name` field is present and correct
   - Open `experiment_summary.txt` (after experiment completes)
   - Verify battery name appears in overview section

2. **Directory Naming**
   - Check folder structure follows pattern:
     - With name: `BatteryName_baseline_20260115_143052/`
     - Without name: `baseline_20260115_143052/`

---

## Troubleshooting

### Issue: Control IDs not found during compilation
**Solution**:
- Regenerate constants: Design → Generate Constants
- Save `.uir` file again
- Rebuild project

### Issue: GetCtrlVal crashes or returns error
**Solution**:
- Verify control ID matches exactly (case-sensitive)
- Check that you're using correct panel handle
- Use debugger to check `tabPanelHandle` value

### Issue: Battery name not appearing in files
**Solution**:
- Add breakpoint at `GetCtrlVal()` line
- Verify `ctx->params.batteryName` is populated
- Check that parameter structure is passed correctly to file writing functions

### Issue: Folder name still doesn't include battery name
**Solution**:
- Verify `CreateTimestampedDirectoryWithBattery()` is being called (not old function)
- Check that battery name parameter is passed correctly
- Add debug logging to see what name is received

---

## Expected Results After Implementation

### Directory Structure Example:
```
data_baseline/
├── INR18650-25R_baseline_20260115_143052/
│   ├── experiment_settings.txt
│   ├── experiment_summary.txt
│   ├── phase1_initial_discharge/
│   ├── phase2_capacity_test/
│   └── ...
└── Samsung_30Q_baseline_20260115_151230/
    └── ...
```

### Settings File Example:
```ini
[Experiment_Parameters]
Battery_Name = INR18650-25R
Target_Temperature_C = 25.0
EIS_Interval_Percent = 10.0
...
```

### Summary File Example:
```ini
[Experiment_Overview]
Battery_Name = INR18650-25R
Start_Time = 2026-01-15 14:30:52
End_Time = 2026-01-15 18:45:23
...
```

---

## Code Changes Summary

All backend code changes are already implemented in this commit:

✅ **Structures updated**: `CDCExperimentParams`, `BaselineExperimentParams`
✅ **Directory function**: `CreateTimestampedDirectoryWithBattery()` in `common.c`
✅ **Baseline**: Folder creation, settings file, summary file
✅ **CDC**: Structure ready for implementation

❌ **UI controls**: Must be added in LabWindows/CVI IDE (this guide)
❌ **GetCtrlVal calls**: Must be added after UI controls exist (this guide)

---

## Questions or Issues?

If you encounter problems during implementation:
1. Check LabWindows/CVI documentation for UI control creation
2. Verify control IDs match exactly in both `.uir` and code
3. Use debugger to trace parameter flow
4. Check log files for directory creation errors

---

## Commit Message Reference

This guide accompanies commit with message:
"Add battery name field to CDC and baseline experiments - UI implementation required"

---

**End of Implementation Guide**
