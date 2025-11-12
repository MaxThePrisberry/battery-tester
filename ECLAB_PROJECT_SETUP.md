# EC-Lab Integration - LabWindows/CVI Project Setup

This guide covers the **manual steps** you need to complete in the LabWindows/CVI IDE to finish the EC-Lab integration.

## Status: Code Changes Complete ✅

The following code changes have been completed automatically:
- ✅ 7 new source files copied to `biologic/` directory
- ✅ EC-Lab configuration added to `common.h`
- ✅ Initialization code added to `BatteryTester.c`

## Next Steps: Manual IDE Configuration

### Step 1: Add Files to Project (5 minutes)

1. **Open your project** in LabWindows/CVI 2020
   - Open: `BatteryTester.prj`

2. **Add header files** to project:
   - Right-click project tree → **Add Files to Project** → **Target**
   - Navigate to `biologic/` directory
   - Select and add these **4 header files**:
     - `biologic_abstract.h`
     - `biologic_eclab.h`
     - `eclab_olecom.h`
     - `eclab_olecom_interface.h`

3. **Add source files** to project:
   - Right-click project tree → **Add Files to Project** → **Target**
   - Navigate to `biologic/` directory
   - Select and add these **3 source files**:
     - `biologic_abstract.c`
     - `biologic_eclab.c`
     - `eclab_olecom.c`

4. **Verify in project tree**:
   - All 7 files should now appear in your project tree

### Step 2: Link OLE32.lib Library (2 minutes)

1. **Open project configuration**:
   - Menu: **Build** → **Configuration**

2. **Navigate to Linker settings**:
   - Select **Linker** in the left panel
   - Click **Libraries/Object** tab

3. **Add OLE32.lib**:
   - Click **Add Library** button
   - Navigate to: `C:\Program Files (x86)\Windows Kits\10\Lib\<version>\um\x64\`
   - Select: `OLE32.lib`
   - Click **Open**
   - **OR** manually type `OLE32.lib` in the Library List field

4. **Apply and close**:
   - Click **OK** to save configuration

### Step 3: Build Project (2 minutes)

1. **Rebuild entire project**:
   - Menu: **Build** → **Rebuild All**

2. **Expected result**:
   - Should compile without errors
   - May see some warnings (acceptable)

3. **If you get compilation errors**, check:

   **Error:** `Cannot find biologic_abstract.h`
   - **Fix:** Verify Include Directories contains `biologic/` folder
   - Build → Configuration → Compiler → Preprocessor → Include Directories

   **Error:** `Unresolved external symbol '_CoCreateInstance@20'`
   - **Fix:** OLE32.lib not linked correctly - repeat Step 2

   **Error:** `Undefined function 'BIO_Abstract_SetQueueManagers'`
   - **Fix:** `biologic_abstract.c` not added to project - repeat Step 1

### Step 4: Test Baseline (5 minutes)

1. **Verify mode setting**:
   - Open `common.h`
   - Confirm: `#define BIOLOGIC_CONTROL_MODE  0` (DLL mode)

2. **Run application**:
   - Click **Run** or press F5

3. **Check log messages**:
   - Look for: `"Bio-Logic: Direct DLL mode active"`
   - Verify: All existing functionality works normally

4. **Test basic operation**:
   - Connect to devices
   - Run a simple OCV measurement
   - Verify everything works as before

### Troubleshooting

#### Compilation Issues

**Problem:** Files not found during compilation
- **Solution:** Check that all 7 files are in `biologic/` directory
- **Verify:** Run `dir biologic\*eclab* biologic\*abstract*` in terminal

**Problem:** OLE32.lib errors
- **Solution:** Ensure correct path for Windows SDK version
- **Find your SDK:** `C:\Program Files (x86)\Windows Kits\10\Lib\`
- **Use latest version:** Pick highest version number folder

#### Runtime Issues

**Problem:** Application crashes on startup
- **Solution:** Check initialization order - abstraction layer should initialize AFTER queue manager
- **Verify:** `BIO_Abstract_SetQueueManagers()` is called after `BIO_QueueInit()`

**Problem:** Mode not detected correctly
- **Solution:** Rebuild entire project (mode is compile-time constant)
- **Verify:** `BIOLOGIC_CONTROL_MODE` is defined in `common.h`

## What's Next?

After successful compilation and baseline testing:

1. **One-time EC-Lab setup** (next step):
   - Register EC-Lab as COM server
   - Create directories
   - Create .mps template files

2. **Test EC-Lab mode**:
   - Switch `BIOLOGIC_CONTROL_MODE` to 1
   - Rebuild
   - Test with EC-Lab GUI

3. **Validate results**:
   - Compare data between modes
   - Verify consistency

## Summary

**Manual Steps Required:**
- [ ] Add 7 files to LabWindows/CVI project
- [ ] Link OLE32.lib library
- [ ] Build project
- [ ] Test baseline (DLL mode)

**Estimated Time:** 10-15 minutes

Once these steps are complete, proceed to **one-time EC-Lab setup** in the next phase.

---

**Questions?** Refer to:
- `eclab_update_package/INTEGRATION_CHECKLIST.md` - Detailed troubleshooting
- `eclab_update_package/README.md` - Package overview
