# U1 Mali GPU foundation

This overlay enables the existing RK3562 Mali-G52 node, builds the pinned
Rockchip kernel's bifrost driver and installs the checksummed Mali g24p0 EGL,
GLES and GBM library with its licence. It does not change the original UI's
drawing backend, panel timings or GPU voltage table.

The supported device tree identifies `rockchip,rk3562-evb2-ddr4-v10` and
`rockchip,rk3562`. Builds require its existing `vdd_gpu` regulator and both
400 MHz OPP entries. Unknown board layouts fail the build.

Set the following in `/oem/printer_data/config/extended/extended2.cfg` and
reboot while the printer is idle:

```ini
[components]
mali_gpu = true
```

Startup confirms a 400 MHz maximum by reading it back.
The original screen can run without this opt-in. Consumers must also check
the limit before using the GPU; `/dev/mali0` alone does not confirm startup
completed successfully.

## Checks

On a Linux build host:

```sh
python3 -m unittest discover -s overlays/mods/BICHENG/71-ui-gpu/test -p 'test_*.py'
```

For manual device acceptance, copy `test/egl_smoke.py` to the printer and run
it as root while idle. Close GPU previews first for an isolated suspend check:

```sh
python3 egl_smoke.py
```

The probe draws one offscreen 16×16 GLES 3 triangle. It checks the Mali-G52
renderer, center and background pixels, GL errors and EGL cleanup. It never
writes the screen or sends touch input. Drawing runs in a child process so
all driver descriptors are closed before the parent checks runtime suspend.
If another process holds `/dev/mali0`, `suspend_check=not_isolated` reports
that power-management acceptance is inconclusive. It does not stop clients
or mistake their activity for a probe cleanup failure. Without other clients,
failure to suspend within five seconds fails the check.

This is a smoke check, not a performance, long-print or driver-recovery test.
