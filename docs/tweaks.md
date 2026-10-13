---
title: Klipper Tweaks
---

# Klipper Tweaks

Advanced experimental tweaks for Klipper stepper motor driver configuration. These settings can **only** be configured via the [Firmware Configuration](firmware_config.md) web interface under **Settings → Tweaks**.

> **Warning**: These are experimental features that modify low-level stepper driver parameters. Use with caution and monitor your printer carefully after enabling.

## TMC AutoTune

Applies optimized stepper motor driver settings for TMC2240 drivers.

**What it does:**
- Optimizes PWM settings for quieter operation
- Configures StallGuard and CoolStep parameters
- Adjusts timing parameters for better heat management
- Fine-tunes driver parameters for improved performance

**Risks:**
- May cause motors to overheat if cooling is insufficient
- Could result in reduced torque or skipped steps under heavy load
- Incorrect settings may affect print quality
- Changes low-level driver parameters that override defaults

**Recommendation:**
- Monitor motor temperatures during first use
- Test with simple prints before production work
- Revert to disabled if you experience issues

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

## TMC Reduced Current

Lowers the stepper motor run current from 1.2A to 1.0A for X and Y axes.

**What it does:**
- Reduces X and Y axis motor current to 1.0A
- Lowers motor heat generation
- Results in quieter motor operation

**Risks:**
- May cause skipped steps under heavy load or fast movements
- Could result in layer shifts on demanding prints
- May reduce positioning accuracy under high acceleration

**Recommendation:**
- Monitor print quality after enabling
- Watch for layer shifts or positioning issues
- Disable if you experience motion problems

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

## Max Speed

Raises the XY motion limits and speeds up tool changes. Motion settings only — no
stepper driver registers are changed, so sensorless homing is unaffected.

Both tiers run the printer above the motion limits Snapmaker ships it with — the
`Stock` column below is the rated configuration, and every value to the right of it
is beyond what the machine is specified for. These numbers are community-tested,
not vendor-validated, and the extra speed is paid for in wear.

Values derived from [@JNP-1](https://github.com/JNP-1/Snapmaker-U1-Config).

**Two tiers:**

| | Stock | Balanced | Aggressive |
|---|---|---|---|
| Max XY velocity | 500 mm/s | 600 mm/s | 750 mm/s |
| Max acceleration | 20000 mm/s² | 22000 mm/s² | 25000 mm/s² |
| Tool change speed | 400 mm/s | 550 mm/s | 700 mm/s |
| Slow dock-entry speed | 60 mm/s | 40 mm/s | 60 mm/s |
| Dock grab speed | 10 mm/s | 30 mm/s | 50 mm/s |
| Tool change acceleration | 5000 mm/s² | 12000 mm/s² | 25000 mm/s² |

**Which one:** start with `Balanced` and move to `Aggressive` only once tool changes
are reliable. `Balanced`'s dock-entry speed is deliberately below stock — that move
is short, so a slightly slower dock approach is expected, not a regression.

**Requirements:**
- Dock positions are calibrated correctly
- Belt tension is correct
- Input shaper is calibrated for your printer (run `SHAPER_CALIBRATE`)
- Tool changes are already reliable with the stock configuration

**Risks:**
- Accelerated wear from running the printer above its rated limits
- Crashes into the tool docks if dock positions are off
- Layer shifts and skipped steps under heavy load
- Higher driver and motor temperatures
- Ringing and other quality artifacts if input shaper is not calibrated

**Recommendation:**
- Calibrate the printer fully before enabling
- Start with `Balanced` and a small test print, and watch the first tool changes
- Disable if you see dock alignment problems or layer shifts

**Note:** This tweak can be combined with
[TMC Reduced Current](#tmc-reduced-current). The combination runs X/Y motors at
1.0A with raised speed limits, so monitor for skipped steps, layer shifts, and
motor temperature during the first prints. [TMC AutoTune](#tmc-autotune) can
also be used alongside Max Speed; the two do not change any setting in common.

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

## Fan Curves

Selects nonlinear fan curves for the power fan and all four nozzle fans.

**Options:**

- `Quiet` - the normal profile. The power fan starts at 20% when a nozzle target or actual temperature reaches 70°C, then rises nonlinearly to 60%. Nozzle fans begin their normal table at 50°C with a zero-duty entry and rise gently. The power fan `heavy load` rule reaches 100% only when at least two nozzle targets strictly exceed 200°C.
- `Quiet (Experimental)` - ordinary temperature control starts at 220°C and rises from 20% to 60%. It is intended for low-noise experiments, not as the production default.
- `Stock` - removes the override and restores the curves from the base printer configuration.

The nozzle fan curves ignore nozzle target temperature for their normal table selection, so a 250°C target does not immediately select the 250°C nozzle-fan speed. Each custom `heater_fan` section declares `fan_curve_mode: actual`, which selects actual-temperature table control with hysteresis and leaves the original table behavior unchanged for every other section. The custom profiles use target temperatures only for the power-fan pre-spin or the power-fan `heavy load` decision; ordinary table entries stay at or below 60%. The power-fan heavy-load rule reaches 100% only when at least two nozzle targets are strictly above 200°C: `200°C` does not trigger it, while two `201°C` targets do. The nozzle-fan cavity guard raises each custom nozzle fan to at least 60% above 55°C, including when its ordinary curve is at zero; it reduces heat-creep risk in a warm chamber, while `Stock` preserves the original 45°C/100% behavior.

The setting is available under **Settings → Tweaks → Fan Curves**. Applying an option writes one file to `/oem/printer_data/config/extended/klipper/20_fan_curves.cfg` and restarts Klipper.

The Klipper patch accepts either `heavy_load_target_temp_*` or the earlier `emergency_target_temp_*` field names at the same parameter boundary. If both names are present, their values must agree; otherwise Klipper reports the conflict instead of silently choosing one. Existing profiles therefore remain valid without a startup migration script. Firmware Config also recognizes the old `balanced` and `lazy` markers as aliases for `Quiet` and `Quiet (Experimental)`. This compatibility applies to profiles read by the patched Klipper; a firmware without this patch still requires selecting `Stock` before downgrading because it does not understand `fan_curve_mode`.

## Object Processing for Adaptive Mesh

Enables object processing in Moonraker's file manager to support adaptive mesh features.

**What it does:**
- Processes gcode files to extract object information
- Generates boundaries for adaptive mesh leveling
- Allows per-object print settings and controls

**Risks:**
- Can cause very long processing times for large gcode files (> 100MB)
- May result in extended delays when uploading files
- Snapmaker Orca may stay at 100% for a long time when sending prints
- High memory usage during file processing
- Can cause delays before prints can start

**Important:**
- Enabling this setting alone is not enough to use adaptive mesh
- You must also update your slicer start gcode to use: `BED_MESH_CALIBRATE ADAPTIVE=1`
- This tells Klipper to only mesh the area where objects will be printed

**Recommendation:**
- **Preferred approach:** Enable `Exclude Object` in your slicer settings instead of this option
- Slicer-generated object labels are more reliable and don't require server-side processing
- Only enable Moonraker object processing if your slicer doesn't support exclude object
- Disable if you frequently print large gcode files
- Monitor file upload times after enabling
- Consider splitting large models into smaller prints if processing is too slow

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

## How to Configure

1. Open the printer's web interface (Fluidd or Mainsail)
2. Navigate to **Firmware Config** in the menu
3. Go to **Settings → Tweaks**
4. Select the desired option for each tweak
5. Confirm the warning dialog
6. Klipper will automatically restart to apply changes

Changes take effect immediately after Klipper restarts (no reboot required).

## Technical Details

These tweaks work by adding or removing configuration files from `/oem/printer_data/config/extended/`:
- `klipper/tmc_autotune.cfg` - TMC AutoTune parameters
- `klipper/tmc_current.cfg` - Reduced current settings
- `klipper/10_max_speed.cfg` - Max Speed motion limits and tool change speeds (either tier installs to this one file)
- `klipper/20_fan_curves.cfg` - Selected power and nozzle fan curves
- `moonraker/object_processing.cfg` - Moonraker object processing settings

These files are automatically included by the main printer configuration if present. Manual editing of these files is not recommended as they will be overwritten by the Firmware Configuration interface.
