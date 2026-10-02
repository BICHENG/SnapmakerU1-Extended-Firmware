# EPIC-A — Workspace and printer baseline

## Goal

Keep the experiment in the requested workspace and preserve a trustworthy pre-change reference.

## Entry gate

- The repository is on `poc/fan-curves-imu-20261001`.
- `git status` is recorded before further edits.
- The printer is reachable on SSH and Moonraker HTTP.

## Actions

1. Record branch, remote, base commit, `vars.mk`, and changed files.
2. Read-only query printer health, active config, firmware settings, fan objects, MCU objects, Tailscale state, and disk space.
3. Capture the current `printer.cfg`, extended config directory, relevant logs, and service state into a timestamped backup.

## Evidence

`reports/baseline/`, `reports/decisions.tsv`, command transcripts, and SHA-256 hashes.

## Stop conditions

Stop before any mutation if SSH identity is unresolved, backup storage is insufficient, Klipper is not `ready`, or Tailscale state cannot be described.

## Rollback

No mutation occurs in this EPIC.
