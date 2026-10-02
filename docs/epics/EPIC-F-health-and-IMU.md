# EPIC-F — Runtime health and IMU experiment

## Goal

Verify services and compare vibration proxies before and after the fan/drive change.

## Health checks

- Klipper connected and `ready`.
- Moonraker `/server/info` healthy.
- Firmware Config `/api/settings` exposes `Fan Curves`.
- `printer` limits match selected Max Speed.
- `tmc2240 stepper_x/y.run_current` report `1.0` when reduced current is enabled.
- Tailscale service and node status remain present.

## IMU matrix

Collect at least three repeats per state, with 20–30 seconds per sample:

1. Idle, all heaters/fans off.
2. Power fan at each available command speed.
3. Nozzle fans at 0%, 10%, 30%, 60%, and 100% where controllable.
4. Quiet and Balanced curve during controlled nozzle heating.
5. Typical print motion with Max Speed + Reduced Current enabled, if a safe test file is available.

## Data

Store raw accelerometer output, state labels, timestamps, command transcript, temperature, RPM if available, and calculated RMS/peak/percentile summaries. Treat IMU results as vibration proxies; they are not calibrated acoustic dBA.

## Stop conditions

Stop motion tests on skipped steps, layer shift, abnormal motor temperature, thermal warning, or any loss of service.
