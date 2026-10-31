# U1 Mali GPU foundation

This overlay enables the existing RK3562 Mali-G52 node, builds the pinned
Rockchip kernel's bifrost driver and installs the checksummed Mali g24p0 EGL,
GLES and GBM library with its licence. It does not change the original UI's
drawing backend, panel timings or GPU voltage table.

The supported device tree identifies `rockchip,rk3562-evb2-ddr4-v10` and
`rockchip,rk3562`. Builds require its existing `vdd_gpu` regulator and both
400 MHz OPP entries. Unknown board layouts fail the build.

Use **Firmware → Snapmaker components → Touchscreen GPU acceleration**.
The setting preserves the selected interface and reboots only when the printer
is idle and every heater target is zero. The equivalent configuration is:

```ini
[components]
mali_gpu = true
```

Startup confirms a 400 MHz maximum by reading it back.
The original screen can run without this opt-in. Consumers must also check
the limit before using the GPU; `/dev/mali0` alone does not confirm startup
completed successfully.

## Shared output and refresh policy

`apps/ui-gpu/gpu_output.c` owns a linear DMA-BUF, its EGL image, texture and
CPU read synchronization. It is compiled into both the original UI's G-code
extension and the pinned Helix 1.0.7 build. Consumers own their render target,
orientation and color conversion; the shared module preserves every RGBA byte,
including Helix's alpha selection marker. Unsupported imports use the existing
`glReadPixels` path. A failed read synchronization aborts the frame and lets the
consumer dispose its context and recover. There is no additional graphics daemon.

The original UI and Helix still draw ordinary widgets with LVGL's software
renderer. On U1, enabling this feature selects 22 ms display and animation
periods, matching the measured panel mode of about 45 Hz. Disabling it retains
the 33 ms baseline. These are timer periods, not a promise that every scene
renders 45 new frames per second. Input polling and panel timing are unchanged.

Helix uses a patched executable only when its installed asset version matches
the executable's version record. The official executable remains the fallback.
The patched build keeps the fbdev display and its normal CPU widget renderer;
G-code uses GLES 2 and the shared output module. It does not enable experimental
NanoVG drawing or require users to change renderer options.

On U1, the existing geometry budget limits tubes to four sides without changing
path simplification precision. Uploads omit degenerate triangles, and the main
solid pass writes the selection tag instead of replaying the entire model.
Ghost layers and selection brackets do not overwrite that tag. Coplanar overlap
belongs to the face that actually wins the color depth test.

Before allocating expanded buffers, the same 25% available-memory budget covers
CPU geometry, the actual per-layer upload payload, and a GPU reserve of 8 MiB
plus six times that payload. The reserve includes margin over measured U1
driver allocations; it is not a driver allocation limit. Models over budget use
the existing 2D fallback. With GPU disabled, this entry skips 3D construction.

The small `stock-display` adapter is separate from the G-code extension. It
recognizes exact original GUI hashes for 2.0.0.205 and 2.1.0.157 and is the sole
owner of the original main-loop sleep hook. Unknown binaries remain untouched.
The 2.0 adapter changes only the verified display timer; animation cadence and
performance events are supported on 2.1. The optional G-code consumer supplies
`stock_ui_tick`; it does not install a second sleep hook.

## Measuring the baseline

On original GUI 2.1, `/tmp/stock-ui-perf.json` records a five-second window in
both switch states. `actual_fps` counts completed LVGL renders, excluding idle
refresh callbacks. Flush timings describe callback and wait time, not panel
scanout; incomplete event sequences are marked explicitly. The file is updated
outside the LVGL lock. `/tmp/stock-display.log` records the applied periods.

For a controlled idle-device comparison, write a duration from 1 to 15 seconds
to `/tmp/stock-display-benchmark-seconds`. The adapter consumes the request once
and invalidates the existing 480×320 page at its refresh cadence. It does not
send input, navigate or change page objects. Compare complete five-second
windows wholly inside that interval, on the same page with the same observers.
Without a request, no extra redraw is generated.

For output-path comparisons with identical GPU geometry, set
`UI_GPU_OUTPUT=readback` for the benchmark process. This keeps GLES enabled but
disables DMA-BUF output. The global Firmware switch also affects cadence and
G-code availability, so it is a different comparison.

## Build

The existing feature firmware workflow calls `helix-gpu.yaml`, then verifies and
installs its artifact. The cache key includes the pinned source patch, common
module, build workflow and package version. A missing, stale or altered artifact
fails the build. To build locally, run that workflow's prepare/build/package
steps and place the verified artifact in `ui-artifact/` before `make build`.

## Checks

On a Linux build host:

```sh
python3 -m unittest discover -s overlays/mods/BICHENG/71-ui-gpu/test -p 'test_*.py'
make -C overlays/mods/BICHENG/71-ui-gpu/apps/stock-display test
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
