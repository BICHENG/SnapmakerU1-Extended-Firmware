# EPIC-E — Firmware upgrade

## Goal

Apply the built image through the repository-supported `systemUpgrade.sh` path.

## Actions

1. Record the exact source and destination hashes.
2. Upload the artifact to a new `/userdata/` filename.
3. Invoke `/home/lava/bin/systemUpgrade.sh upgrade soc ...`.
4. Wait for reconnect; do not interrupt power during the upgrade.

## Verification gate

The next EPIC starts only after SSH and Moonraker reconnect and the printer reports `ready`.

## Stop conditions

Stop and enter recovery if the upgrade command fails, the device does not reconnect within the measured timeout, or the active slot/version is inconsistent.

## Rollback

Use the printer's preserved A/B slot or the original stock upgrade image from the baseline.
