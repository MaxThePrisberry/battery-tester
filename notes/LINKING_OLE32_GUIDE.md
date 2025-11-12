# How to Link OLE32.lib in LabWindows/CVI 2020

## The Error

```
Undefined symbol '_CLSIDFromProgID@' referenced in Debug\eclab_olecom.obj
Undefined symbol '_CoCreateInstance@' referenced in Debug\eclab_olecom.obj
Undefined symbol '_SysFreeString@' referenced in Debug\eclab_olecom.obj
... (and many others)
```

**Cause:** OLE32.lib is not linked to the project.

---

## Solution: Add OLE32.lib to Project

### Method 1: Via Build Configuration (Recommended)

1. **Open your project** in LabWindows/CVI 2020
   - File → Open Project → `BatteryTester.prj`

2. **Open Build Configuration**
   - Menu: **Build** → **Configuration**
   - Or press: **Shift+F7**

3. **Navigate to Linker settings**
   - In the left panel, select: **Linker**
   - Click the **Libraries/Object** tab on the right

4. **Add OLE32.lib**

   **Option A: Browse to the file**
   - Click **Add Library** button
   - Navigate to Windows SDK library directory:
     ```
     C:\Program Files (x86)\Windows Kits\10\Lib\<version>\um\x64\OLE32.lib
     ```
     Or for 32-bit:
     ```
     C:\Program Files (x86)\Windows Kits\10\Lib\<version>\um\x86\OLE32.lib
     ```
   - Select `OLE32.lib`
   - Click **Open**

   **Option B: Type library name directly**
   - In the **Library List** field at the bottom
   - Add a new line: `OLE32.lib`
   - The system will find it automatically

5. **Apply and Close**
   - Click **OK** to save configuration
   - Close the dialog

6. **Rebuild**
   - Menu: **Build** → **Rebuild All**
   - Or press: **Ctrl+F7**

---

### Method 2: Edit Project File Directly

If Method 1 doesn't work, you can edit the `.prj` file:

1. **Close LabWindows/CVI** (important!)

2. **Open BatteryTester.prj in a text editor**

3. **Find the section:** `[Compiler Options]`

4. **Add this line under the library section:**
   ```
   Link Library = "ole32.lib"
   ```

5. **Save and close**

6. **Reopen project in LabWindows/CVI**

7. **Rebuild**

---

### Method 3: Pragma Directive (Quick Fix)

Add this at the top of `eclab_olecom.c`:

```c
#ifdef _WIN32
    #pragma comment(lib, "ole32.lib")
#endif
```

This tells the compiler to link OLE32.lib automatically.

**Location:** Add after the includes, around line 20

---

## Verification

After adding OLE32.lib, you should see in the build output:

```
Linking...
   Creating library Debug\BatteryTester.lib and object Debug\BatteryTester.exp
BatteryTester.exe - 0 error(s), XX warning(s)
Build succeeded.
```

No more "Undefined symbol" errors!

---

## Common Issues

### Issue 1: "Cannot find ole32.lib"

**Cause:** Windows SDK not installed or wrong path

**Solution:**
1. Find your Windows SDK installation:
   ```
   C:\Program Files (x86)\Windows Kits\10\Lib\
   ```
2. List available versions:
   ```
   dir "C:\Program Files (x86)\Windows Kits\10\Lib\"
   ```
3. Pick the highest version number (e.g., `10.0.19041.0`)
4. Use full path:
   ```
   C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\um\x64\OLE32.lib
   ```

### Issue 2: "LNK1104: cannot open file 'OLE32.lib'"

**Cause:** Wrong architecture (x86 vs x64)

**Solution:**
- If building 32-bit: Use `\um\x86\OLE32.lib`
- If building 64-bit: Use `\um\x64\OLE32.lib`

Check your project configuration:
- Build → Configuration → Compiler → Target Type

### Issue 3: Still getting undefined symbols

**Cause:** Other COM libraries also needed

**Solution:** Add these libraries too:
- `OLEAUT32.lib` - OLE Automation support
- `UUID.lib` - GUID definitions

In Library List, add:
```
OLE32.lib
OLEAUT32.lib
UUID.lib
```

---

## Quick Checklist

- [ ] OLE32.lib added to project libraries
- [ ] Correct Windows SDK version selected
- [ ] Correct architecture (x86 or x64) selected
- [ ] Project rebuilt (not just compiled)
- [ ] No "Undefined symbol" errors in output

---

## Expected Build Output (Success)

```
Build Status (BatteryTester.prj - Debug)
 battery_utils.c
 BatteryTester.c
 biologic_abstract.c - 11 warnings
 biologic_dll.c
 biologic_eclab.c
 eclab_olecom.c
 ... (other files)
Linking...
BatteryTester.exe - 0 error(s), 36 warning(s)
Build succeeded.
```

Warnings are OK - the important thing is **0 error(s)**!

---

## If All Else Fails

Try **Method 3** (pragma directive) as it's the most reliable:

1. Open `biologic\eclab_olecom.c`
2. Find line ~15 (after the includes)
3. Add:
   ```c
   #ifdef _WIN32
       #pragma comment(lib, "ole32.lib")
       #pragma comment(lib, "oleaut32.lib")
   #endif
   ```
4. Save and rebuild

This forces the linker to include these libraries.
