# Manual Capacity Entry - UI Setup Guide

This guide explains how to add the manual capacity entry controls to the Baseline Experiment panel in LabWindows/CVI 2020.

## Overview

The manual capacity entry feature allows users to skip the time-consuming Phase 2 (capacity measurement) by directly entering a known battery capacity value. This saves 3-6 hours when the battery capacity is already known.

**Important:** Phase 1 (initial discharge) still runs to establish the 0% SOC baseline needed for accurate EIS measurements in Phase 3.

## Required UI Controls

You need to add **two controls** to the **BASELINE** tab panel:

### 1. Checkbox: "Use Manual Capacity"
- **Control Type:** Radio Button (checkbox)
- **Control ID:** `BASELINE_CHK_MANUAL_CAPACITY` (ID: 9)
- **Label:** "Use Manual Capacity"
- **Default Value:** 0 (unchecked)
- **Tooltip:** "Check to skip Phase 2 capacity measurement and enter capacity manually"

### 2. Numeric Input: "Battery Capacity (mAh)"
- **Control Type:** Numeric
- **Control ID:** `BASELINE_NUM_MANUAL_CAPACITY` (ID: 10)
- **Label:** "Battery Capacity (mAh)"
- **Data Type:** Double
- **Default Value:** 1000.0
- **Min Value:** 1.0
- **Max Value:** 100000.0
- **Format:** "%.2f"
- **Tooltip:** "Enter the known battery capacity in mAh"

## Step-by-Step Instructions

### Step 1: Open the UI Editor

1. Launch **LabWindows/CVI 2020**
2. Open the project: `BatteryTester.prj`
3. In the project tree, double-click `BatteryTester.uir` to open the UI Editor

### Step 2: Navigate to the BASELINE Panel

1. In the UI Editor, find the **BASELINE** tab panel (it should be one of the tab pages)
2. Select it to make it active

### Step 3: Add the Checkbox Control

1. From the **Controls** palette, select **Radio Button**
2. Place it on the BASELINE panel below the existing controls (suggested location: below "Log Interval")
3. Right-click the control → **Properties**
4. Set the following properties:
   - **Label:** "Use Manual Capacity"
   - **Constant Name:** `BASELINE_CHK_MANUAL_CAPACITY`
   - **Data Type:** Integer
   - **Default Value:** 0
   - **Tooltip:** "Check to skip Phase 2 capacity measurement and enter capacity manually"
5. Click **OK**

### Step 4: Add the Numeric Input Control

1. From the **Controls** palette, select **Numeric**
2. Place it next to or below the checkbox
3. Right-click the control → **Properties**
4. Set the following properties:
   - **Label:** "Battery Capacity (mAh)"
   - **Constant Name:** `BASELINE_NUM_MANUAL_CAPACITY`
   - **Data Type:** Double
   - **Default Value:** 1000.0
   - **Minimum:** 1.0
   - **Maximum:** 100000.0
   - **Format:** `%.2f`
   - **Increment:** 10.0
   - **Tooltip:** "Enter the known battery capacity in mAh"
5. Click **OK**

### Step 5: Verify Control IDs

1. In the UI Editor, go to **Code → Generate** menu
2. Select **Control IDs**
3. Verify that the generated constants match:
   ```c
   #define  BASELINE_CHK_MANUAL_CAPACITY     9
   #define  BASELINE_NUM_MANUAL_CAPACITY     10
   ```
4. If the IDs are different, you have two options:
   - **Option A:** Update `BatteryTester.h` to match the auto-generated IDs
   - **Option B:** Manually renumber the controls in the UI to match IDs 9 and 10

### Step 6: Save and Generate Code

1. Save the UI file: **File → Save**
2. Generate the UI header: **Code → Generate → All Code**
3. Close the UI Editor

### Step 7: Rebuild the Project

1. In the main CVI window, select **Build → Rebuild All**
2. Verify there are no compilation errors
3. Fix any ID mismatches if necessary

## How It Works

### When Checkbox is UNCHECKED (default):
- Experiment runs **all 4 phases** normally:
  - **Phase 1:** Initial discharge + temperature setup (~30-60 min)
  - **Phase 2:** Charge/discharge to measure capacity (~3-6 hours)
  - **Phase 3:** EIS measurements during charge
  - **Phase 4:** Discharge to 50%

### When Checkbox is CHECKED:
- Experiment **skips only Phase 2**:
  - **Phase 1:** Initial discharge + temperature setup (~30-60 min) → **RUNS NORMALLY**
  - **Phase 2:** ~~Charge/discharge to measure capacity~~ → **SKIPPED** ✓
  - **Phase 3:** EIS measurements during charge (uses manual capacity)
  - **Phase 4:** Discharge to 50% (uses manual capacity)

**Why Phase 1 still runs:** Phase 1 discharges the battery to the minimum voltage (0% SOC), establishing a known baseline for the EIS measurements in Phase 3. Without this, SOC calculations would be inaccurate.

The manually entered capacity value is used for:
- SOC calculations during Phase 3 (EIS measurements)
- Determining when to stop Phase 4 (50% of manual capacity)
- Reported capacity values in the final results

### Time Savings

- **Phase 1:** ~30-60 minutes (discharge + temperature stabilization) → **STILL RUNS**
- **Phase 2:** ~3-6 hours (full charge/discharge cycle) → **SKIPPED**
- **Total savings:** ~3-6 hours per experiment!

## Validation

The code includes validation to ensure the manual capacity value is reasonable:
- Minimum: 1 mAh (prevents divide-by-zero errors)
- Maximum: 100,000 mAh (prevents unrealistic values)
- Default: 1000 mAh (typical for small batteries)

## Suggested UI Layout

```
┌─────────────────────────────────────────────────┐
│ Baseline Experiment Parameters                 │
├─────────────────────────────────────────────────┤
│                                                 │
│  Temperature (°C):        [  25.0  ]           │
│  EIS Interval (% SOC):    [  10.0  ]           │
│  Current Threshold (A):   [  0.01  ]           │
│  Log Interval (s):        [   10   ]           │
│                                                 │
│  ☐ Use Manual Capacity                         │
│  Battery Capacity (mAh):  [ 1000.00 ]          │
│                                                 │
│  [  Start Baseline Experiment  ]               │
│                                                 │
│  Status: Idle                                   │
└─────────────────────────────────────────────────┘
```

## Testing

After adding the controls:

1. Run the application
2. Navigate to the Baseline Experiment tab
3. Check the "Use Manual Capacity" checkbox
4. Enter a test capacity value (e.g., 2000 mAh)
5. Start the baseline experiment
6. Verify in the log:
   ```
   === PHASE 1: Initial Discharge and Temperature Setup ===
   (Phase 1 runs normally...)
   === SKIPPING PHASE 2: Using manual capacity entry ===
   Manual capacity: 2000.00 mAh
   === PHASE 3: EIS Measurements During Charge ===
   ```
7. Confirm the experiment runs Phase 1, skips Phase 2, then continues with Phase 3

## Troubleshooting

### Problem: Compilation errors about undefined constants
**Solution:** Verify the control IDs in `BatteryTester.h` match the auto-generated IDs from the UI file.

### Problem: Controls don't appear on the panel
**Solution:** Make sure you're editing the correct tab panel (BASELINE) and the controls are inside the panel boundaries.

### Problem: Experiment doesn't skip phases
**Solution:** Check that:
- The checkbox is actually checked when starting the experiment
- The capacity value is greater than 0
- The code was recompiled after changes

## Additional Notes

- The checkbox and numeric input are **independent** - you can leave the capacity field blank when the checkbox is unchecked
- Consider adding a **label** or **group box** around these controls for better organization
- You may want to **gray out** the capacity field when the checkbox is unchecked (requires callback function)
- The feature works in **both Direct DLL and EC-Lab modes**

## Code Files Modified

The following files were modified to implement this feature:
- `exp_baseline.h` - Added `useManualCapacity` and `manualCapacity_mAh` to parameters struct
- `exp_baseline.c` - Added logic to skip Phase 2 and use manual capacity
- `BatteryTester.h` - Added control ID constants

All modifications are complete - you only need to add the UI controls!

## Important Notes

- **Phase 1 always runs** to establish 0% SOC baseline
- **Phase 2 is skipped** when manual capacity is used
- Manual capacity is used for SOC calculations in Phases 3 & 4
- Time savings: **3-6 hours** (Phase 2 only)
