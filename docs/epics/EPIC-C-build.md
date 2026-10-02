# EPIC-C — Build and artifact gate

## Goal

Produce one identifiable upgrade image without touching the printer.

## Actions

1. Use WSL Ubuntu 24.04 and the repository build entry point.
2. Download or verify the base firmware against `vars.mk` SHA-256.
3. Build `extended` with the overlay.
4. Hash `firmware/firmware_extended.bin` and `tmp/firmware/update.img`.
5. Inspect the generated image contents and `UPFILE_VERSION`.

## Evidence

`reports/build/`, full build log, hashes, branch/base commit, and artifact sizes.

## Stop conditions

Stop if the base hash differs, ownership checks fail, non-ARM binaries are found, or the output image is missing.

## Rollback

The printer remains unchanged; discard the artifact and use the baseline firmware.
