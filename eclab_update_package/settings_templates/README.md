# EC-Lab Settings Templates Directory

This directory is a placeholder for your `.mps` template files.

## What are .mps files?

`.mps` files are EC-Lab technique settings files that define:
- Measurement parameters (voltage, current, frequency ranges)
- Data recording settings
- Safety limits
- Advanced options

## How to create .mps files

### Option 1: Create in EC-Lab GUI
1. Launch EC-Lab
2. Configure your technique (OCV, PEIS, GEIS, etc.)
3. Set all desired parameters
4. Go to: **File → Save Settings**
5. Save as `.mps` file with appropriate name
6. Copy to your application's `eclab_settings` directory

### Option 2: Use Existing Files
If you already have .mps files from previous EC-Lab work:
1. Copy them to your application's `eclab_settings` directory
2. Rename to match the defines in `common.h`:
   - `ocv_default.mps`
   - `peis_default.mps`
   - `geis_default.mps`

### Option 3: Copy from battery_exploder
If you have access to the battery_exploder project:
1. Find `.mps` files in battery_exploder's `eclab_settings/` directory
2. Copy to your application
3. Modify if needed in EC-Lab

## Required .mps Files

At minimum, you need `.mps` files for each technique you use:

- **ocv_default.mps** - Open Circuit Voltage settings
- **peis_default.mps** - Potentiostatic EIS settings (if you use PEIS)
- **geis_default.mps** - Galvanostatic EIS settings (if you use GEIS)

## File Naming

The filenames must match the defines in your `common.h`:

```c
#define ECLAB_OCV_TEMPLATE              "ocv_default.mps"
#define ECLAB_PEIS_TEMPLATE             "peis_default.mps"
#define ECLAB_GEIS_TEMPLATE             "geis_default.mps"
```

If you change the names, update `common.h` to match.

## Multiple Templates

You can create multiple `.mps` files for different scenarios:

```
eclab_settings/
├── ocv_default.mps       ← General purpose OCV
├── ocv_quick.mps         ← Fast OCV with shorter duration
├── geis_battery.mps      ← Standard battery EIS
├── geis_high_freq.mps    ← High-frequency EIS
└── geis_low_freq.mps     ← Low-frequency EIS
```

To use a specific template:
1. Update `common.h` define to point to it, OR
2. Call `BIO_ECLAB_Run*()` directly with custom path

## Important Notes

1. **Parameters from .mps override code**
   - In EC-Lab mode, technique parameters come from the .mps file
   - Parameters passed in your code are ignored
   - This allows changing settings without recompiling

2. **Test in EC-Lab first**
   - Always test your .mps settings manually in EC-Lab
   - Verify they work before using in automated system
   - Check measurement completes successfully

3. **Safety limits**
   - Set appropriate voltage/current limits in .mps files
   - These protect your battery and equipment
   - Be conservative with limits

4. **Version compatibility**
   - .mps files are specific to EC-Lab version
   - If you update EC-Lab, verify .mps files still load
   - May need to recreate in new version

## Troubleshooting

### "Settings file not found"
- Check file exists in the directory specified by `ECLAB_SETTINGS_DIR`
- Check filename matches exactly (case-sensitive on some systems)
- Check path in `common.h` is correct

### "Failed to load settings"
- .mps file may be corrupted
- .mps file may be from incompatible EC-Lab version
- Try recreating the .mps file in current EC-Lab version

### Measurement doesn't match expectations
- Open .mps file in EC-Lab to review parameters
- Verify frequency range, amplitude, duration, etc.
- Test manually in EC-Lab to see actual behavior

## Example .mps File Parameters

### OCV (Open Circuit Voltage)
- Duration: 10 seconds (typical)
- Rest time: 0 seconds
- Record interval: 1 second
- E range: Auto

### PEIS (Potentiostatic EIS)
- Initial frequency: 10000 Hz
- Final frequency: 0.1 Hz
- Frequency spacing: Logarithmic, 10 points per decade
- Amplitude: 10 mV
- Initial voltage: OCV
- Average: 3 measurements per frequency

### GEIS (Galvanostatic EIS)
- Initial frequency: 10000 Hz
- Final frequency: 0.1 Hz
- Frequency spacing: Logarithmic, 10 points per decade
- Amplitude: 0.1 mA (adjust for your battery)
- Initial current: 0 A
- Average: 3 measurements per frequency

## Creating Your First .mps File

1. **Launch EC-Lab**
2. **Create New Experiment**
3. **Select Technique** (e.g., OCV)
4. **Configure Parameters:**
   - Click technique name in left panel
   - Set duration, recording intervals, etc.
   - Click "Check" to validate settings
5. **Save Settings:**
   - File → Save Settings
   - Navigate to your `eclab_settings` directory
   - Name file according to `common.h` (e.g., `ocv_default.mps`)
   - Click Save
6. **Test in your application**

---

**For more information, see:**
- `INTEGRATION_CHECKLIST.md` - Step-by-step integration
- `documentation/ECLAB_INTEGRATION_GUIDE.md` - Complete user guide
- Bio-Logic EC-Lab documentation - Technique details
