# Snapmaker U1 Fan/Drive Experiment

Date: 2026-10-01 (Asia/Shanghai)

## Objective
Build, deploy, and verify a rollback-capable Snapmaker U1 extended firmware branch that:
- exposes selectable nonlinear low-noise fan curves in Firmware Config;
- permits Max Speed and TMC Reduced Current together;
- captures baseline and post-change printer health;
- compares IMU vibration proxies for idle/fan/print states;
- preserves Tailscale and produces raw evidence plus conclusions.

## Gates
1. Workspace and branch exist at this path and point to the current pax `develop` commit.
2. The complete `extended` overlay set and submodules are present; no overlay may be removed to make a build pass.
3. Baseline firmware/config/service/feature evidence is archived before mutation.
4. Build succeeds in WSL with every declared overlay and generated artifacts are identified by hash.
5. Upgrade path is explicit and reversible; a partial or hand-edited rootfs is never eligible for flashing.
6. Post-upgrade: Klipper, Moonraker, Tailscale, Firmware Config, Max Speed, TMC Reduced Current, and fan curves are checked.
7. IMU collection is labeled as vibration proxy, not calibrated acoustic dBA.
8. Report includes raw data, method, limitations, and rollback command/path.

## Planned phases
A. Workspace, source branch, toolchain, and printer baseline.
B. Implement overlay and deterministic static checks.
C. Build artifact in WSL and inspect contents/hash.
D. Protectively back up printer files and service state.
E. Upgrade using the repository's supported path.
F. Verify services/features and collect IMU/fan-state experiments.
G. Analyze raw data, write report, and leave rollback instructions.

## Definition of done

The run is complete only when all of these are true:

- `make build PROFILE=extended` succeeds in WSL and the output image hash is recorded.
- The printer backup contains the active firmware/config, service state, Tailscale state summary, and a copy of the deployed artifact.
- Firmware Config exposes `Fan Curves` with a reversible `Stock` option.
- Runtime configuration reports both Max Speed and TMC Reduced Current enabled, with X/Y `run_current` at `1.0A` and the selected motion limits intact.
- Klipper, Moonraker, Firmware Config, and Tailscale pass post-upgrade health checks.
- IMU runs have raw CSV/JSON output, command logs, labels, and enough repeats to compare baseline against each fan state.
- The report separates vibration proxy results from acoustic dBA and states whether each claim is VERIFIED, INCONCLUSIVE, or NOT VERIFIED.

## EPIC order

Read `docs/epics/EPIC-*.md` in order. Each EPIC has an entry gate, one-way actions, evidence artifacts, verification commands, rollback path, and stop conditions.

## Safety constraints
- Do not remove or rewrite Tailscale state.
- Do not mutate the printer before baseline backup and artifact verification.
- Keep stock firmware/config backups separate from generated files.
- Any failed gate stops the next phase and is recorded.
- The previous partial build is quarantined and cannot be used as a new input artifact.
