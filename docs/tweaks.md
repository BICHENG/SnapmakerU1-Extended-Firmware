---
title: Klipper Tweaks
---

# Klipper Tweaks

Configurable Klipper motion, motor-current, and fan-control tweaks. These settings can **only** be configured via the [Firmware Configuration](firmware_config.md) web interface under **Settings → Tweaks**.

> **Note**: `Max Speed` and `TMC Reduced Current` change motion and motor-current settings. `Optimal` is the default fan profile the firmware ships, and it never cools less than stock does once the hotend is past 140°C.

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
[TMC Reduced Current](#tmc-reduced-current). Firmware Config asks for an
explicit confirmation when either setting is enabled. Together they run X/Y
motors at 1.0A with raised speed limits. [TMC AutoTune](#tmc-autotune) can
also be used alongside Max Speed; the two do not change any setting in common.

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

## Fan Curves

Selects cooling curves for the power fan and all four nozzle fans.

**Options:**

- `Stock` - the printer's own stepped fan table. It has no file, the editor shows it read only, and it is what the printer runs when no curve file is active.
- `Optimal` - the profile the firmware ships. Its nozzle curve reaches the stock cooling at `140°C` and follows it from there on, and stays below it while the hotend is still warming up. Its power curve reaches the stock 60% from `200°C` on, so a PLA or ABS print gets the stock cooling.
- Every other profile is a curve of your own. How much it cools the hotend is on your risk.

Every profile except `Stock` is a file named `fan_curves_<name>.json` in `/oem/printer_data/config/extended/fan-curves-presets/`. That directory is the only place profiles are looked for, so a file you add, rename or remove is the one the editor and the driver see. The firmware installs its default profile `Optimal` into that directory once, when the printer boots. If you delete or rename it, it stays gone, and the editor can write it back with **Restore the default profile**.

The duty between two points of a curve is the line between them, so the fan follows the temperature instead of jumping from step to step. The stock tables stay stepped tables, the curves do not.

The nozzle curve follows the effective toolhead temperature, the same as the stock table, so a print that sets 240°C selects the 240°C speed right away. The power fan follows target temperatures only, the same way the stock `temp_speed_table` does, so a toolhead with a faulty thermistor cannot push the power fan up. The first power node is also the power-fan target trigger and duty. All curve profiles use `5°C` hysteresis, ordinary duty stays at or below 100%, probing still locks the nozzle fans to their `probe_speed`, and the cavity temperature guard still forces the fans to 100% when the chamber leaves its guard range.

**Drawing a curve:**

The setting is available under **Settings → Tweaks → Fan Curves**. The Fan Curves card below it shows the profile that is running and opens the curve editor, which has its own page and opens in a modal.

- Click empty space in the chart to add a point, drag a point to move it, right click a point to remove it.
- Click a point to select it, then edit its temperature and duty in the fields under the chart.
- The profile menu creates a new profile, renames one, deletes one, and restores the default profile.
- **Save** writes the curve into the profile file. **Apply** makes that profile the running one and restarts Klipper.

The editor draws the selected curve next to the stock curve and, where the driver holds a nozzle fan at the floor, draws that floor as a line of its own. A profile that is running cannot be deleted; apply another profile first.

Applying an option atomically replaces `/oem/printer_data/config/extended/fan-curves-active.json` and restarts Klipper. When the printer is upgraded from the older editor, the curve it kept in `fan-curves.json` is imported as a profile file named `custom`, so nothing you drew is lost. `Stock` stores a Stock marker and leaves the original printer configuration in control. A legacy `20_fan_curves.cfg` is renamed before Klipper starts so an older install cannot be loaded accidentally.

**Risks:**

- A profile of your own is your own risk. Different materials need different cooling, so test it before a long print
- The rule that holds a nozzle fan turning travels with the profile file, as `min_cool_temperature` and `min_nozzle_duty` (50 and 5% by default), so no curve can leave the heatsink fan off while the hotend is hot. The driver and the editor read the same two numbers, so changing them in the file changes both
- The power fan heavy-load rule is part of the profile: it reaches 100% when at least three toolheads heat up and one of them targets more than `260°C`

**Recommendation:**
- Start with `Optimal`, and use `Stock` for prints that need the full cooling, such as a heated chamber
- Test a profile of your own with a short print before a long one

**Configuration:**
This feature can **only** be configured via Firmware Configuration web interface. Manual configuration is not supported.

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
- `fan-curves-active.json` - Selected profile and curves read by the patched fan driver
- `fan-curves-presets/` - The profile files, one per profile, that the editor draws and the driver reads
- `moonraker/object_processing.cfg` - Moonraker object processing settings

The JSON files are read by the patched fan driver; the remaining files are included by the main printer configuration when present. Manual editing is not recommended while the Firmware Configuration interface is running.
