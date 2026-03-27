# EC-Lab OLE COM Debugging Checklist

This checklist will help you diagnose and fix EC-Lab OLE COM integration issues.

## Current Status

The Battery Exploder application is configured to use EC-Lab OLE COM mode (BIOLOGIC_CONTROL_MODE=1 in common.h).
Enhanced diagnostic logging has been added to help identify where the connection is failing.

## Prerequisites

### 1. EC-Lab Version
- **Required:** EC-Lab v11.11 or later (or BT-Lab v1.57+)
- **Check:** Open EC-Lab and go to Help → About to verify version
- **Action:** If version is too old, update EC-Lab before proceeding

### 2. EC-Lab Registration
- **Requirement:** EC-Lab must be registered as an OLE COM server
- **Check:** Look for error in Battery Exploder log: "CLSIDFromProgID failed"
- **Action:** Run as Administrator:
  ```cmd
  cd "C:\Program Files (x86)\EC-Lab"
  ECLab.exe /regserver
  ```
- **Verify:** No error message should appear. Registration is silent on success.

### 3. EC-Lab Must Be Running
- **Requirement:** EC-Lab application must be started BEFORE Battery Exploder
- **Check:** Look for error: "CoCreateInstance failed: 0x80080005"
- **Action:**
  1. Start EC-Lab
  2. Wait for it to fully load
  3. Then start Battery Exploder

### 4. OLECOM Mode Indicator
- **Requirement:** EC-Lab must enable OLE COM automation
- **Check:** Look at EC-Lab status bar (bottom of window)
- **Expected:** You should see "OLECOM" indicator (blue or orange)
- **If missing:** EC-Lab may not have OLE COM enabled. Try:
  - Restart EC-Lab
  - Check EC-Lab settings for automation options

## Directory Structure

Verify these directories exist:

```
C:\BatteryExploder\
├── eclab_settings\          # .mps template files
│   ├── ocv_default.mps
│   ├── peis_default.mps
│   └── geis_default.mps
└── eclab_data\              # Measurement output files (.mpr)
```

**Action if missing:**
1. Create directories manually
2. Copy template files from EC-Lab installation or create new ones in EC-Lab
3. Verify paths in common.h match:
   - ECLAB_SETTINGS_DIR
   - ECLAB_DATA_DIR

## Device Connection

### In EC-Lab (BEFORE starting Battery Exploder):
1. Open EC-Lab
2. Go to Device menu → Connect Device
3. Select your BioLogic SP-150e
4. Verify it shows as connected (green indicator)
5. Note the device number (usually 0)

### In Battery Exploder:
- Verify ECLAB_DEVICE_NUMBER in common.h matches EC-Lab device number
- Default is 0, but check if you have multiple devices

## Diagnostic Steps

Follow these steps in order when starting Battery Exploder:

### Step 1: Check Initial Connection
**What to look for in log:**
```
========================================
Initializing EC-Lab OLE COM connection
========================================
Working directory: C:\BatteryExploder\eclab_data
Step 1: Initializing COM...
COM initialized successfully
```

**If Step 1 fails:**
- "CoInitialize failed" = Windows COM system issue
- **Fix:** Restart computer, check Windows COM services

### Step 2: Check Registration
**What to look for in log:**
```
Step 2: Resolving ProgID 'ECLab.Application'...
ProgID resolved successfully. EC-Lab is registered.
```

**If Step 2 fails:**
- "CLSIDFromProgID failed: 0x80040154" = EC-Lab not registered
- **Fix:** Run `ECLab.exe /regserver` as Administrator (see section 2 above)

### Step 3: Check EC-Lab Instance
**What to look for in log:**
```
Step 3: Creating EC-Lab COM instance...
NOTE: EC-Lab must be running for this to succeed.
EC-Lab COM instance created successfully!
```

**If Step 3 fails:**
- Error 0x80080005 (CO_E_SERVER_EXEC_FAILURE) = EC-Lab not running
  - **Fix:** Start EC-Lab first
- Error 0x80070005 (E_ACCESSDENIED) = Permission issue
  - **Fix:** Run both EC-Lab and Battery Exploder as Administrator

### Step 4: Check Device Connection
**What to look for in log:**
```
========================================
Connecting to EC-Lab device 0
========================================
IMPORTANT: Device must be connected in EC-Lab first!
...
Calling EC-Lab ConnectDevice method...
========================================
Successfully connected to device 0!
========================================
```

**If Step 4 fails:**
- "ConnectDevice returned error code: X" = Device not available in EC-Lab
- **Fix:**
  1. Open EC-Lab
  2. Connect to device manually first
  3. Verify device appears in device list
  4. Then try Battery Exploder again

## Common Error Codes

| HRESULT | Name | Meaning | Solution |
|---------|------|---------|----------|
| 0x80040154 | REGDB_E_CLASSNOTREG | Class not registered | Run `ECLab.exe /regserver` |
| 0x80080005 | CO_E_SERVER_EXEC_FAILURE | Server exec failure | Start EC-Lab application |
| 0x80070005 | E_ACCESSDENIED | Access denied | Run as Administrator |
| 0x800401F3 | CLSID_E_CLASSSTRING | Invalid class string | Re-register EC-Lab |

## Testing Procedure

### Quick Test
1. Close all applications
2. Start EC-Lab
3. In EC-Lab: Connect to device manually
4. Verify "OLECOM" indicator in EC-Lab status bar
5. Start Battery Exploder
6. Watch log for all 4 steps to complete successfully
7. Try starting Temperature Ramp experiment

### If Still Not Working

**Check experiment.log file:**
- Located in experiment directory (e.g., `notes/temp_ramp_20251031_XXXXXX/experiment.log`)
- Contains detailed step-by-step diagnostic messages
- Look for the first ERROR message
- Match error code to table above

**Enable debug mode (if available):**
- Set LOG_LEVEL to DEBUG in common.h (if implemented)
- Rebuild application
- Repeat test
- Check for additional diagnostic messages

## Manual Verification

You can verify OLE COM works outside Battery Exploder:

### Using VBScript Test
Create test_eclab.vbs:
```vbscript
Set eclab = CreateObject("ECLab.Application")
result = eclab.ConnectDevice(0)
MsgBox "ConnectDevice returned: " & result
```

Run: `cscript test_eclab.vbs`

**Expected:** Message box shows "ConnectDevice returned: 0"
**If fails:** EC-Lab OLE COM is not working properly - reinstall EC-Lab

## Advanced Troubleshooting

### Check Windows Registry
EC-Lab registration creates registry entries:
- Path: `HKEY_CLASSES_ROOT\ECLab.Application`
- Check: Run `regedit` and navigate to this path
- If missing: Registration failed - try re-registering as Admin

### Check EC-Lab Logs
EC-Lab may have its own log files:
- Check EC-Lab installation directory for logs
- Look for OLE COM or automation errors

### Compatibility Mode
If running on Windows 10/11:
1. Right-click EC-Lab.exe
2. Properties → Compatibility
3. Try "Run as Administrator" checkbox
4. Try Windows 7/8 compatibility mode if necessary

## Configuration Summary

**Files to check:**
- `common.h` - BIOLOGIC_CONTROL_MODE=1, directory paths
- `BatteryExploder.c` - EC-Lab config in main()
- `biologic/eclab_olecom.c` - Enhanced diagnostics now present

**Expected behavior:**
- Battery Exploder logs show all 4 initialization steps succeed
- Temperature Ramp experiment starts without "device not connected" error
- EIS measurements trigger EC-Lab to run techniques
- Data files (.mpr) appear in eclab_data directory

## Contact / Support

If all steps above are completed and it still doesn't work:
1. Capture complete log output from Battery Exploder startup
2. Note exact EC-Lab version
3. Check BioLogic website for EC-Lab OLE COM documentation updates
4. Verify .NET Framework version (may be required for newer EC-Lab versions)

---

Last Updated: 2025-10-31
