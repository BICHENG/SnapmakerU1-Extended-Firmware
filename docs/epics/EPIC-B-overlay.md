# EPIC-B — Firmware Config overlay

## Goal

Expose reversible nonlinear fan curves and permit Max Speed with TMC Reduced Current.

## Actions

1. Keep the existing `32-feature-klipper-tweaks` overlay.
2. Add one Firmware Config setting with `Quiet`, `Balanced`, and `Stock` options.
3. Install one active `20_fan_curves.cfg`; remove it to restore stock.
4. Use measured nozzle temperature through `temp_speed_table`; keep a 55°C cavity guard at 60%.
5. Remove only the two Max Speed/TMC mutual exclusion shell checks.

## Verification

- Parse every YAML file with PyYAML.
- Check five fan sections in each template and descending temperature rules.
- Confirm the Firmware Config merge exposes the setting and `Stock` default.
- Inspect the built rootfs for the three selector options and two templates.

## Stop conditions

Do not build if YAML parsing fails, an option can write outside the extended config directory, or a template introduces duplicate unrelated sections.

## Rollback

Restore the branch diff; on the printer select `Stock`, remove `20_fan_curves.cfg`, or restore the baseline backup.
