# EC-Lab OLE COM Integration Checklist

This checklist guides you through integrating EC-Lab OLE COM support into your battery testing application.

**Estimated Time:** 20 minutes to 2 hours (depending on strategy)
**Skill Level:** Intermediate LabWindows/CVI developer
**Prerequisites:** Functioning Direct DLL-based Bio-Logic integration

---

## Pre-Integration Checks

### ☐ Verify System Requirements
- [ ] LabWindows/CVI 2020 (or compatible) installed
- [ ] EC-Lab 11.x installed and working
- [ ] SP-150e or compatible device available
- [ ] Administrator access available (one-time setup)
- [ ] Backup of your current project created

### ☐ Verify Existing Code Structure
- [ ] Project has `biologic/` directory with DLL integration
- [ ] Project has `common.h` or equivalent configuration file
- [ ] Project uses `device_queue` system (or equivalent)
- [ ] Main initialization function identified

### ☐ Choose Integration Strategy
Select one:
- [ ] **Strategy A - Minimal:** Add EC-Lab alongside DLL (fastest, lowest risk)
- [ ] **Strategy B - Full:** Convert all calls to abstraction layer (recommended)
- [ ] **Strategy C - Hybrid:** Mix of both (balanced approach)

**Selected Strategy:** ___________________

---

## Phase 1: File Integration (10-15 minutes)

### ☐ Step 1: Copy Source Files to Project

#### Copy Core EC-Lab Files
From `eclab_update_package/source_files/biologic/`, copy to your `biologic/` directory:

- [ ] `eclab_olecom_interface.h`
- [ ] `eclab_olecom.h`
- [ ] `eclab_olecom.c`
- [ ] `biologic_eclab.h`
- [ ] `biologic_eclab.c`
- [ ] `biologic_abstract.h`
- [ ] `biologic_abstract.c`

**Verification:**
```cmd
dir <your_project>\biologic\*eclab* /b
dir <your_project>\biologic\*abstract* /b
```
You should see all 7 files listed.

### ☐ Step 2: Add Files to LabWindows/CVI Project

1. Open your project (.prj) in LabWindows/CVI 2020
2. For each file, right-click project tree → Add Files to Project → Target
3. Add these files:

**Header Files:**
- [ ] `biologic/eclab_olecom_interface.h`
- [ ] `biologic/eclab_olecom.h`
- [ ] `biologic/biologic_eclab.h`
- [ ] `biologic/biologic_abstract.h`

**Source Files:**
- [ ] `biologic/eclab_olecom.c`
- [ ] `biologic/biologic_eclab.c`
- [ ] `biologic/biologic_abstract.c`

4. Verify in project tree that all files appear

### ☐ Step 3: Configure Compiler/Linker Settings

#### Add Include Directory (if needed)
1. Build → Configuration → Compiler
2. Preprocessor tab → Include Directories
3. Verify `biologic/` is in the list (usually automatic)

#### Add OLE32.lib Library
1. Build → Configuration → Linker
2. Libraries/Object tab
3. Click "Add Library"
4. Navigate to: `C:\Program Files (x86)\Windows Kits\<version>\Lib\<arch>\um\`
5. Add: `OLE32.lib`
6. OR add manually: `OLE32.lib` in Library List

**Verification:** Build → Build (do not run yet)
- [ ] No "unresolved external" errors related to CoCreateInstance, SysFreeString, etc.

---

## Phase 2: Configuration (5-10 minutes)

### ☐ Step 4: Update common.h

#### Option A: Append Content
1. Open `eclab_update_package/source_files/patches/common.h.additions`
2. Copy entire contents
3. Open your project's `common.h` (or equivalent)
4. Paste at end of file (before `#endif` if present)
5. Adjust paths for your system

#### Option B: Manual Entry
Add these sections to your `common.h`:

```c
//==============================================================================
// EC-Lab OLE COM Integration
//==============================================================================

// BioLogic control mode selection
// 0 = Direct DLL mode (default, uses ECLib.dll)
// 1 = EC-Lab OLE COM mode (uses EC-Lab GUI automation)
#define BIOLOGIC_CONTROL_MODE           0  // Start with DLL mode for testing

// EC-Lab OLE COM mode settings (used when BIOLOGIC_CONTROL_MODE == 1)
#define ECLAB_SETTINGS_DIR              "C:\\<YOUR_APP>\\eclab_settings"
#define ECLAB_DATA_DIR                  "C:\\<YOUR_APP>\\eclab_data"
#define ECLAB_DEVICE_NUMBER             0       // EC-Lab device index (depends on configuration)
#define ECLAB_CHANNEL_NUMBER            0       // EC-Lab channel index (depends on configuration)
#define ECLAB_EXECUTABLE_PATH           "C:\\Program Files (x86)\\EC-Lab\\11.63\\EClab.exe"

// .mps template filenames (place these in ECLAB_SETTINGS_DIR)
#define ECLAB_OCV_TEMPLATE              "ocv_default.mps"
#define ECLAB_PEIS_TEMPLATE             "peis_default.mps"
#define ECLAB_GEIS_TEMPLATE             "geis_default.mps"
```

**Customization Checklist:**
- [ ] Update `ECLAB_SETTINGS_DIR` to your application path
- [ ] Update `ECLAB_DATA_DIR` to your application path
- [ ] Verify `ECLAB_EXECUTABLE_PATH` matches your EC-Lab installation
- [ ] Keep `BIOLOGIC_CONTROL_MODE = 0` for now (test DLL mode first)
- [ ] Update `.mps` filenames if you use different names

### ☐ Step 5: Verify Compilation
1. Build → Rebuild All
2. Check for errors

**Common Errors:**
- "Cannot find file": Check Include Directories setting
- "Undefined symbol": Check that all .c files are in project
- "Syntax error": Check for missing semicolons in common.h additions

**Status:**
- [ ] Compiles successfully with `BIOLOGIC_CONTROL_MODE = 0`

---

## Phase 3: Code Integration (varies by strategy)

### ☐ Step 6: Update Initialization Code

Find your main() or initialization function where Bio-Logic is initialized.

#### Add After Queue Manager Initialization

Find where you initialize your Bio-Logic queue manager:
```c
// Your existing code probably looks like:
g_bioQueueMgr = BIO_InitializeQueueManager(...);
```

Add immediately after:
```c
// Set global queue managers for abstraction layer
BIO_Abstract_SetQueueManagers(g_bioQueueMgr, NULL);

// Optional: Set connection mode programmatically
// (Otherwise uses BIOLOGIC_CONTROL_MODE from common.h)
// BIO_SetConnectionMode(BIO_MODE_DIRECT_DLL);  // or BIO_MODE_ECLAB
```

**Example from battery_exploder:**
See `eclab_update_package/source_files/patches/initialization.c.example`

**Verification:**
- [ ] Code compiles after adding initialization
- [ ] Application starts without errors in DLL mode

---

### Strategy A: Minimal Integration (Skip to Phase 4)

If you chose Strategy A (minimal integration):
- **You're done with code changes!**
- Keep using existing `BIO_Run*Queued()` functions
- Skip Step 7 and go to Phase 4

### Strategy B or C: Function Call Conversion

### ☐ Step 7: Convert Technique Calls to Abstraction Layer

#### Find All Bio-Logic Technique Calls

Search your codebase for:
- `BIO_RunOCVQueued`
- `BIO_RunPEISQueued`
- `BIO_RunGEISQueued`
- `BIO_RunGCPLQueued`

**Conversion Table:**

| Old Function (DLL) | New Function (Abstraction) | Notes |
|--------------------|----------------------------|-------|
| `BIO_RunOCVQueued()` | `BIO_Abstract_RunOCV()` | Same parameters |
| `BIO_RunPEISQueued()` | `BIO_Abstract_RunPEIS()` | Same parameters |
| `BIO_RunGEISQueued()` | `BIO_Abstract_RunGEIS()` | Same parameters |
| `BIO_RunGCPLQueued()` | `BIO_Abstract_RunGCPL()` | Same parameters |

#### Conversion Example

**Before:**
```c
BIO_TechniqueData *eisData = NULL;
int result = BIO_RunPEISQueued(
    deviceID, channel,
    vsInitial, initialVoltage,
    duration, recordEveryDT, recordEveryDE,
    initialFreq, finalFreq, sweepLinear,
    amplitude, freqNumber, averageN,
    correction, waitForSteady,
    &eisData,
    timeout, priority,
    progressCallback, userData,
    &cancelled
);
```

**After:**
```c
BIO_TechniqueData *eisData = NULL;
int result = BIO_Abstract_RunPEIS(
    deviceID, channel,
    vsInitial, initialVoltage,
    duration, recordEveryDT, recordEveryDE,
    initialFreq, finalFreq, sweepLinear,
    amplitude, freqNumber, averageN,
    correction, waitForSteady,
    &eisData,
    timeout,
    progressCallback, userData,
    &cancelled
);
```

**Changes:**
- Function name: `BIO_RunPEISQueued` → `BIO_Abstract_RunPEIS`
- Removed: `priority` parameter (uses NORMAL internally)
- Everything else: **identical**

#### Conversion Checklist
- [ ] Converted all OCV calls (or documented decision not to)
- [ ] Converted all PEIS calls (or documented decision not to)
- [ ] Converted all GEIS calls (or documented decision not to)
- [ ] Converted all GCPL calls (if used)
- [ ] Code compiles after conversions
- [ ] No warnings about priority parameter

**Total Calls Converted:** _____ / _____

---

## Phase 4: System Setup (One-Time, 5-10 minutes)

### ☐ Step 8: Register EC-Lab as OLE COM Server

**IMPORTANT:** Requires Administrator privileges

1. Open Command Prompt **as Administrator**
   - Windows Key → type "cmd" → right-click → "Run as administrator"

2. Navigate to EC-Lab directory:
   ```cmd
   cd "C:\Program Files (x86)\EC-Lab\11.63"
   ```
   (Adjust path if your EC-Lab version differs)

3. Register EC-Lab:
   ```cmd
   EClab.exe /regserver
   ```

4. **No output is normal** - command completes silently

**Verification:**
- [ ] Command completed without errors
- [ ] No "Access Denied" message (if so, retry as Administrator)

**Troubleshooting:**
- If EC-Lab path differs, check: `ECLAB_EXECUTABLE_PATH` in common.h
- If version differs, update version number in paths

### ☐ Step 9: Create Directory Structure

Create directories for EC-Lab files:

```cmd
mkdir C:\<YOUR_APP>\eclab_settings
mkdir C:\<YOUR_APP>\eclab_data
```

Replace `<YOUR_APP>` with the paths you set in common.h.

**Example:**
```cmd
mkdir C:\BatteryTester\eclab_settings
mkdir C:\BatteryTester\eclab_data
```

**Verification:**
- [ ] `eclab_settings` directory exists
- [ ] `eclab_data` directory exists
- [ ] Paths match `ECLAB_SETTINGS_DIR` and `ECLAB_DATA_DIR` in common.h

### ☐ Step 10: Create/Copy .mps Template Files

You need .mps files for each technique you use.

#### Option A: Use Existing .mps Files
If you already have .mps files from EC-Lab:
1. Copy them to `eclab_settings/` directory
2. Rename to match defines in common.h:
   - `ocv_default.mps`
   - `peis_default.mps`
   - `geis_default.mps`

#### Option B: Create New .mps Files in EC-Lab
1. Launch EC-Lab
2. Connect your device
3. Configure OCV technique with desired parameters
4. File → Save Settings → save as `ocv_default.mps`
5. Repeat for PEIS and GEIS
6. Copy all .mps files to `eclab_settings/` directory

#### Option C: Extract from battery_exploder
If you have access to battery_exploder:
1. Copy .mps files from battery_exploder's `eclab_settings/`
2. Place in your `eclab_settings/` directory
3. Modify in EC-Lab if parameters differ

**Verification:**
- [ ] `ocv_default.mps` exists in settings directory
- [ ] `peis_default.mps` exists in settings directory (if used)
- [ ] `geis_default.mps` exists in settings directory (if used)
- [ ] Filenames match defines in common.h

---

## Phase 5: Testing (15-30 minutes)

### ☐ Step 11: Test Direct DLL Mode (Baseline)

**Purpose:** Verify existing functionality still works

1. Verify `common.h` has: `#define BIOLOGIC_CONTROL_MODE  0`
2. Build → Rebuild All
3. Run application
4. Connect to device
5. Run simple OCV measurement
6. Verify measurement completes successfully
7. Check data is saved/processed correctly

**Test Results:**
- [ ] Application launches successfully
- [ ] Device connects in DLL mode
- [ ] OCV completes successfully
- [ ] EIS completes successfully (if applicable)
- [ ] Data quality matches previous results
- [ ] No new errors or warnings

**If tests fail:** Stop and fix issues before proceeding. Do not test EC-Lab mode until DLL mode works.

### ☐ Step 12: Test EC-Lab OLE COM Mode

**Purpose:** Verify new EC-Lab integration works

#### Preparation
1. **Close your application** if running
2. **Open EC-Lab** GUI
3. **Connect device** in EC-Lab
4. Leave EC-Lab running

#### Configuration
1. Edit `common.h`: change to `#define BIOLOGIC_CONTROL_MODE  1`
2. Build → Rebuild All (mode is compile-time)
3. Verify no compilation errors

#### Testing
1. Run your application
2. Watch for log messages about EC-Lab connection
3. Run simple OCV measurement
4. **Watch EC-Lab GUI** - you should see measurement running!
5. Wait for completion
6. Check data is returned correctly

**EC-Lab Mode Test Results:**
- [ ] EC-Lab connection established (check logs)
- [ ] Device connection succeeds
- [ ] OCV measurement starts and completes
- [ ] Measurement visible in EC-Lab GUI
- [ ] .mpr file created in `ECLAB_DATA_DIR`
- [ ] Data returned to application correctly
- [ ] No errors in logs

**If EC-Lab tests fail:** See Troubleshooting section below

### ☐ Step 13: Cross-Mode Validation

**Purpose:** Verify both modes produce equivalent results

#### Run Comparison Test
1. Run OCV in DLL mode, save result
2. Switch to EC-Lab mode (rebuild)
3. Run OCV in EC-Lab mode, save result
4. Compare voltage values - should be within mV

#### For EIS (if applicable)
1. Run GEIS in DLL mode, note final impedance
2. Switch to EC-Lab mode
3. Run GEIS in EC-Lab mode
4. Compare Nyquist plots - should overlap

**Validation Results:**
- [ ] OCV voltages match within acceptable tolerance
- [ ] EIS impedance spectra match visually
- [ ] No systematic differences between modes
- [ ] Both modes equally reliable

**Acceptable Differences:**
- Voltage: ±1-5 mV (measurement noise)
- Impedance: ±1-2% (frequency-dependent)
- Timing: EC-Lab mode slightly slower (expected)

---

## Phase 6: Deployment

### ☐ Step 14: Choose Default Mode

Decide which mode to use by default:

**Option A: Direct DLL (Recommended for Production)**
- Set `BIOLOGIC_CONTROL_MODE = 0` in common.h
- Use EC-Lab mode only for debugging
- Rebuild and deploy

**Option B: EC-Lab OLE COM (Recommended for Development)**
- Set `BIOLOGIC_CONTROL_MODE = 1` in common.h
- Keep EC-Lab running during testing
- Switch to DLL for production deployment

**Option C: Runtime Selection (Advanced)**
- Use `BIO_SetConnectionMode()` to switch dynamically
- Requires additional application logic
- See documentation for details

**Selected Default Mode:** ___________________

### ☐ Step 15: Document Configuration

Create a document for your team with:
- [ ] Which mode is currently active
- [ ] Locations of .mps template files
- [ ] How to switch modes (edit common.h + rebuild)
- [ ] Troubleshooting contacts

### ☐ Step 16: Update Version Control

Commit your changes:
```cmd
git add biologic/eclab_*.* biologic/biologic_abstract.* biologic/biologic_eclab.*
git add common.h
git commit -m "Add EC-Lab OLE COM support

- Add abstraction layer for dual-mode operation
- Add EC-Lab OLE COM interface
- Add .mpr file parsing
- Default to Direct DLL mode"
git push
```

---

## Troubleshooting

### Compilation Issues

#### "Cannot find eclab_olecom.h"
- **Cause:** Include directories not set
- **Fix:** Build → Configuration → Compiler → Include Directories
- Add your `biologic/` directory if not automatic

#### "Unresolved external symbol '_CoCreateInstance@20'"
- **Cause:** OLE32.lib not linked
- **Fix:** Build → Configuration → Linker → Libraries
- Add `OLE32.lib` to library list

#### "Syntax error in common.h"
- **Cause:** Missing semicolon or bracket
- **Fix:** Check common.h.additions was pasted correctly
- Verify no missing `}` or `;` at paste location

### Runtime Issues - DLL Mode

#### "Device connection failed" (after integration)
- **Cause:** Broke existing DLL code
- **Fix:** Verify Step 11 (DLL mode test) passes
- Check if biologic_dll.c was modified accidentally

#### Application crashes on startup
- **Cause:** Initialization order issue
- **Fix:** Verify `BIO_Abstract_SetQueueManagers()` called AFTER queue creation
- Check that queue managers are valid pointers

### Runtime Issues - EC-Lab Mode

#### "Failed to create EC-Lab COM object"
- **Cause:** EC-Lab not registered as OLE server
- **Fix:** Run (as Admin): `EClab.exe /regserver`
- See Step 8

#### "EC-Lab not running or not responding"
- **Cause:** EC-Lab must be launched before your app
- **Fix:**
  1. Start EC-Lab first
  2. Connect device in EC-Lab
  3. Then start your application

#### "Settings file not found: C:\...\ocv_default.mps"
- **Cause:** .mps file missing or path wrong
- **Fix:**
  1. Verify file exists in `ECLAB_SETTINGS_DIR`
  2. Check path in common.h matches actual location
  3. Check filename matches exactly (case-sensitive)

#### "Device not found" (EC-Lab mode)
- **Cause:** Device/channel number mismatch
- **Fix:**
  1. Check which device/channel in EC-Lab
  2. Update `ECLAB_DEVICE_NUMBER` in common.h
  3. Update `ECLAB_CHANNEL_NUMBER` in common.h
  4. Rebuild

#### Measurement starts but never completes
- **Cause:** Timeout too short or measurement actually failed
- **Fix:**
  1. Check EC-Lab GUI for error messages
  2. Increase timeout value in function call
  3. Test same measurement manually in EC-Lab
  4. Check .mps file parameters

#### ".mpr file not found" or "File contains 0 points"
- **Cause:** Measurement failed but error not detected
- **Fix:**
  1. Check EC-Lab GUI for actual measurement status
  2. Verify technique completed successfully
  3. Check `ECLAB_DATA_DIR` permissions (writable?)
  4. Look for EC-Lab error dialogs

### Data Quality Issues

#### OCV values differ significantly between modes
- **Possible causes:**
  - Different wait times in .mps vs code parameters
  - Different voltage ranges
  - Connection issue in one mode
- **Fix:**
  1. Compare .mps settings with DLL parameters
  2. Check cable connections
  3. Repeat measurements multiple times
  4. Verify both modes use same battery state

#### EIS spectra don't match
- **Possible causes:**
  - Different frequency ranges
  - Different amplitude settings
  - Different averaging
- **Fix:**
  1. Compare .mps frequency settings with code
  2. Compare amplitude in .mps vs code
  3. Check averaging/integration time settings
  4. Ensure battery in same state for both tests

---

## Completion Checklist

### Integration Complete When:
- [ ] All files copied and added to project
- [ ] Application compiles in both modes
- [ ] DLL mode tests pass (baseline established)
- [ ] EC-Lab mode tests pass (new feature works)
- [ ] Cross-mode validation complete (results match)
- [ ] Documentation updated
- [ ] Team trained on mode switching
- [ ] Changes committed to version control

### Optional Enhancements:
- [ ] Add mode indicator to UI
- [ ] Add runtime mode switching
- [ ] Create multiple .mps profiles per technique
- [ ] Integrate .mpr file viewer
- [ ] Add automated mode comparison testing

---

## Post-Integration Notes

### Performance Observations
- DLL mode speed: _______________
- EC-Lab mode speed: _______________
- Speed difference acceptable? [ ] Yes [ ] No

### Data Quality
- OCV voltage tolerance: ±______ mV
- EIS impedance tolerance: ±______ %
- Results consistent? [ ] Yes [ ] No

### Issues Encountered
1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

### Solutions Applied
1. _______________________________________________
2. _______________________________________________
3. _______________________________________________

---

## Sign-Off

Integration completed by: ___________________________

Date: ___________________________

Verified by: ___________________________

Date: ___________________________

Notes:
_________________________________________________________
_________________________________________________________
_________________________________________________________

---

## Next Steps After Integration

1. **Create .mps library** - Build collection of validated .mps files
2. **Document parameter relationships** - Map code parameters to .mps settings
3. **Train users** - Show team how to switch modes
4. **Establish testing protocol** - Define when to use each mode
5. **Monitor performance** - Track mode-specific issues over time

---

**For detailed usage and troubleshooting, see:**
- `README.md` - Package overview
- `documentation/ECLAB_INTEGRATION_GUIDE.md` - Runtime usage guide
- `documentation/ECLAB_OLECOM_Debugging_Checklist.md` - Troubleshooting

**For support:**
- Review log files in your application
- Check EC-Lab GUI for error messages
- Test measurements manually in EC-Lab first
- Compare .mps settings with code parameters
