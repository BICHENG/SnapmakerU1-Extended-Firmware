# EPIC-B — Firmware Config overlay

## Goal

Expose reversible nonlinear fan curves and permit Max Speed with TMC Reduced Current.

## Actions

1. Keep the existing `32-feature-klipper-tweaks` overlay.
2. Add one Firmware Config setting with `Quiet`, `Balanced`, and `Stock` options.
3. Install one active `20_fan_curves.cfg`; remove it to restore stock.
4. Use measured nozzle temperature through `temp_speed_table`; keep the stock `stepped_temp_table` parseable but let the new table take priority.
5. Keep the cavity guard at 55°C and 60% while documenting that the current Klipper guard only takes over after the base curve is nonzero.
6. Remove only the two Max Speed/TMC mutual exclusion shell checks.

## Verification

- Parse every YAML file with PyYAML.
- Check five fan sections in each template and descending temperature rules.
- Confirm the Firmware Config merge exposes the setting and `Stock` default.
- Inspect the built rootfs for the three selector options and two templates.
- Apply `09_fan_temp_speed_table_override.patch` with `patch --dry-run` before building.
- Load `Quiet` on the printer and confirm Klippy reaches `ready`.
- Heat one nozzle to 75°C: the selected nozzle fan reaches 10%, power fan stays 0%, and cooling below 70°C returns it to 0%.

## Stop conditions

Do not build if YAML parsing fails, the Klipper patch does not apply, an option can write outside the extended config directory, or a template introduces duplicate unrelated sections.

## Rollback

Restore the branch diff; on the printer select `Stock`, remove `20_fan_curves.cfg`, or restore the baseline backup.
