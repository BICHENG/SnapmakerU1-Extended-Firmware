# EPIC-D — Printer protection and deployment package

## Goal

Make printer mutation reversible and preserve Tailscale.

## Actions

1. Stop active printing and confirm Klipper is idle/ready.
2. Create a printer-local timestamped backup under `/userdata/` without deleting existing data.
3. Save active config, extended config, firmware slot/version information, service status, Tailscale status, and Tailscale state paths.
4. Copy the generated upgrade image to the printer only after the local artifact gate passes.

## Protected items

- `/var/lib/tailscale/` or the device's actual Tailscale state path.
- `/oem/printer_data/config/` and `/userdata/` existing files.
- Existing A/B firmware slot information.

## Stop conditions

Stop if backup hashes cannot be verified, Tailscale is absent/unreadable, or a print/job is active.

## Rollback

Use the original firmware slot or the repository's supported upgrade/recovery path; restore only backed-up config files after service shutdown.
