# EC-Lab OLE COM Setup Guide

**Status:** Directories created ✅
**Next:** Manual setup steps below

---

## What's Already Done

✅ **Step 1: Directories Created**
- `C:\Users\nrasm\Documents\battery-tester\eclab_settings` - For .mps template files
- `C:\Users\nrasm\Documents\battery-tester\eclab_data` - For .mpr output files

---

## Step 2: Register EC-Lab as COM Server (⚠️ Requires Administrator)

EC-Lab must be registered in Windows as a COM server for OLE automation.

### Instructions:

1. **Find EC-Lab installation**
   - Default: `C:\Program Files (x86)\EC-Lab\11.63\EClab.exe`
   - Or search for `EClab.exe` on your system

2. **Open Command Prompt as Administrator**
   - Press `Windows Key`
   - Type: `cmd`
   - Right-click "Command Prompt"
   - Select: **"Run as administrator"**

3. **Navigate to EC-Lab directory**
   ```cmd
   cd "C:\Program Files (x86)\EC-Lab\11.63"
   ```
   *(Adjust path if your EC-Lab is installed elsewhere)*

4. **Register EC-Lab**
   ```cmd
   EClab.exe /regserver
   ```

5. **Verify**
   - Command completes silently (no output is normal)
   - No error message = success!

**⚠️ Important:** This only needs to be done **once per system**.

---

## Step 3: Create .mps Template Files

.mps files contain technique settings for EC-Lab. You need to create templates for each technique you'll use.

### Method 1: Create New Templates in EC-Lab

For **OCV** (Open Circuit Voltage):

1. **Launch EC-Lab**

2. **Connect your Bio-Logic device**
   - Device → Connect
   - Select your SP-150e

3. **Create OCV technique**
   - Experiment → Technique
   - Select: **OCV (Open Circuit Voltage)**
   - Set parameters:
     - Duration: 10 seconds
     - Record every dT: 1.0 s
     - Record every dE: 0.01 V
     - E range: Auto

4. **Save settings**
   - File → Save Settings
   - Navigate to: `C:\Users\nrasm\Documents\battery-tester\eclab_settings`
   - Filename: `ocv_default.mps`
   - Click **Save**

For **GEIS** (Galvanostatic EIS):

1. **In EC-Lab, create new technique**
   - Experiment → Technique
   - Select: **GEIS (Galvanostatic Electrochemical Impedance Spectroscopy)**

2. **Set parameters** (matching your code defaults):
   - vs: Initial
   - Initial current: 0.0 A
   - Duration: 1.0 s
   - Record every dT: 0.0 s
   - Record every dE: 0.01 V
   - Initial frequency: 10000 Hz (10 kHz)
   - Final frequency: 0.1 Hz
   - Logarithmic spacing
   - Amplitude: 500 mA (0.5 A)
   - Number of frequencies: 31
   - Average N: 2
   - Wait for steady: 0.1 period
   - I range: 1 A

3. **Save settings**
   - File → Save Settings
   - Navigate to: `C:\Users\nrasm\Documents\battery-tester\eclab_settings`
   - Filename: `geis_default.mps`
   - Click **Save**

### Method 2: Copy from Battery Exploder (if available)

If you have access to a working battery_exploder installation:

```cmd
copy <battery_exploder_path>\eclab_settings\*.mps C:\Users\nrasm\Documents\battery-tester\eclab_settings\
```

### Verify Templates Created

Check that these files exist:
- ✅ `eclab_settings\ocv_default.mps`
- ✅ `eclab_settings\geis_default.mps`

---

## Step 4: Verify EC-Lab Configuration

Before switching modes, verify in EC-Lab:

1. **Launch EC-Lab**

2. **Check device/channel numbers**
   - Look at device list in EC-Lab
   - Usually: Device 0, Channel 0
   - If different, update `common.h`:
     ```c
     #define ECLAB_DEVICE_NUMBER    0  // ← Change if needed
     #define ECLAB_CHANNEL_NUMBER   0  // ← Change if needed
     ```

3. **Test connection manually**
   - Connect device in EC-Lab
   - Load one of your .mps files
   - Run a quick measurement
   - Verify it works before using from code

---

## Step 5: Switch to EC-Lab Mode

Once setup is complete:

1. **Open `common.h`**
   - Find line: `#define BIOLOGIC_CONTROL_MODE 0`
   - Change to: `#define BIOLOGIC_CONTROL_MODE 1`

2. **Rebuild project**
   - Build → Rebuild All in LabWindows/CVI

3. **Start EC-Lab FIRST**
   - ⚠️ **Important:** EC-Lab must be running before your application
   - Launch EC-Lab
   - Connect your device

4. **Run your application**
   - The abstraction layer will connect to EC-Lab via COM

5. **Test with BIO commands**
   ```
   BIO MODE    → Should show "EC-Lab OLE COM mode"
   BIO TEST    → Test connection
   BIO OCV     → Run measurement (watch EC-Lab GUI!)
   BIO GEIS    → Run EIS (watch EC-Lab GUI!)
   ```

---

## Troubleshooting

### "Failed to create EC-Lab COM object"
- **Cause:** EC-Lab not registered as COM server
- **Fix:** Run Step 2 (register as administrator)

### "Settings file not found"
- **Cause:** .mps file missing or wrong filename
- **Fix:** Check `eclab_settings\` directory has the .mps files

### "Device not found"
- **Cause:** Device/channel numbers don't match
- **Fix:** Check device numbers in EC-Lab, update `common.h` if needed

### "EC-Lab not running or not responding"
- **Cause:** EC-Lab must be launched before your application
- **Fix:** Start EC-Lab first, connect device, then run your app

---

## Summary

**Completed:**
- ✅ Directories created

**Manual Steps Required:**
1. [ ] Register EC-Lab as COM server (Administrator command prompt)
2. [ ] Create .mps template files (in EC-Lab GUI)
3. [ ] Verify EC-Lab device/channel configuration
4. [ ] Switch to EC-Lab mode and test

**Estimated Time:** 10-15 minutes

---

## Benefits After Setup

Once complete, you can:
- **Switch modes** by changing one line in `common.h` and rebuilding
- **Visual debugging** - watch measurements in EC-Lab GUI
- **Parameter flexibility** - change settings in .mps files without recompiling
- **Cross-validation** - compare DLL vs EC-Lab results
- **Same commands work in both modes** - no code changes needed!

---

**Ready?** Follow Step 2 (COM registration) first, then Step 3 (.mps creation).
