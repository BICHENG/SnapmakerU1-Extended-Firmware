# U1 GPU runtime and shared G-code preview

The `gpu` branch maintains the Mali runtime, one G-code implementation for both
interfaces, and their display adapters. The original UI remains primary.
See [GPU architecture](../../../../docs/gpu-architecture.md) for ownership,
previous measurements and acceptance status.

This overlay enables the existing RK3562 Mali-G52 node, builds the pinned
Rockchip kernel's bifrost driver and installs the checksummed Mali g24p0 EGL,
GLES and GBM library with its licence. It preserves the GPU voltage table and
panel timing. Ordinary widgets in the original UI and Helix still use LVGL's
software renderer.

## Setting and startup

Use **Firmware → Snapmaker components → Touchscreen GPU acceleration**.
The setting preserves the selected interface and reboots only when the printer
is idle and every heater target is zero. The equivalent configuration is:

```ini
[components]
mali_gpu = true
```

The supported device tree identifies `rockchip,rk3562-evb2-ddr4-v10` and
`rockchip,rk3562`. Builds require the existing `vdd_gpu` regulator and both
400 MHz OPP entries. Unknown board layouts fail the build. Startup sets and
reads back a 400 MHz maximum. Consumers also check this limit before GPU use;
the existence of `/dev/mali0` alone does not establish successful startup.

GPU availability is separate from widget refresh policy. GPU off keeps normal
thumbnails usable and does not load the shared G-code GPU renderer. The supported
display adapters retain their 22 ms refresh policy with GPU off.

## One G-code implementation

`apps/gcode-preview` installs `/usr/local/lib/libgcode-preview.so`. The original
UI's `72-stock-ui` adapter and the U1 Helix widget both use its versioned
`gcode_preview_get_api(1)` C ABI. This library owns parsing, file identity,
current-layer following, cameras, geometry and rendering. The Helix adapter
does not maintain a second parser, camera or renderer.

The library's worker owns EGL and opens `/dev/dri/renderD128`, leaving display
ownership to fbdev. Callers submit a source and view state, then poll for the
latest completed image. Parsing can finish while the widget is paused; a loaded
model is not evidence of a first displayed frame. Generation checks discard
obsolete results after a file change. UI drawing only composites a completed
image and does not wait for GL work.

The native renderer supplies the same supersampled toolpaths, opaque current
layer, translucent surrounding layers and native color palette to both UIs.
The U1 Helix adapter provides the native fullscreen controls and passes the
known printer filename alongside the local cached path. It reveals the preview
on the first completed frame and retains the thumbnail while loading. Manual
layer selection cancels follow. The current-layer button is checked only when
the preview actually follows the live job. Preview object picking and palette
overrides are unsupported; the adapter does not advertise them as effective.

`apps/ui-gpu/gpu_output.c` is compiled into the shared library. It owns the
linear DMA-BUF, EGL image, texture and CPU read synchronization. Unsupported
imports use `glReadPixels`; a failed synchronization aborts the frame. Copies
into LVGL and fbdev remain. No additional graphics daemon is installed.

The U1 Helix build uses `DISPLAY_BACKEND=fbdev`, `ENABLE_GLES_3D=no` and
`ENABLE_OPENGLES=no`. Its thin adapter loads the shared library only when GPU is
enabled. The patched executable runs only with matching installed assets;
otherwise the official executable is selected.

## Complete-frame display and remote capture

The shared `ui-display` presenter gathers LVGL's partial flushes in a complete
480×320 CPU canvas. It presents only after the last flush. The boot configuration
requests two framebuffer pages with `drm_kms_helper.drm_fbdev_overalloc=200`.
When the driver provides both pages, the presenter copies to the back page and
uses `FBIOPAN_DISPLAY` with `FB_ACTIVATE_VBL`. The pinned Rockchip driver uses a
blocking atomic commit, so the previous page is not reused before completion.

If a two-page flip fails, the previous complete page remains visible. Only
`EINTR` is retried on the same back page, with at most three total attempts.
The next flush submits the complete retained canvas. If only one page is
available, visible-page copying remains as an unsynchronized compatibility mode.
Check `/tmp/ui-display.json` for `double_buffer`, `flips`, `errors`, `last_error`,
`yoffset` and presentation timing. Physical-panel acceptance also requires
`/sys/kernel/debug/dri/0/state` to show the target CRTC with `enable=1 active=1`
and a nonzero framebuffer on its plane. `bl_power=0` and increasing flip counts
do not prove active scanout. The final two-page firmware still requires flash
and physical-panel acceptance; successful compilation does not establish that
tearing has been removed.

The presenter publishes its latest complete canvas to `/run/ui-frame` using
the layout in `common/ui-frame.h`. Sequence, monotonic timestamp and producer
PID accompany the XRGB8888 pixels. Nonblocking record locks protect a snapshot;
a busy reader causes a publish skip rather than delaying the local UI. The
`gui-remote` encoder consumes this ABI. Its older-firmware framebuffer fallback
does not provide the same complete-frame guarantee. MPP encoding, WebRTC and
remote touch remain owned by `gui-remote`.

`stock-display` is the sole original main-loop sleep-hook owner. It recognizes
exact GUI hashes for 2.0.0.205 and 2.1.0.157. The complete-frame presenter,
animation timer and render counters are verified for the 2.1 adapter; the 2.0
adapter changes only its verified display timer. Unknown binaries bypass
private calls. The optional original G-code adapter supplies `stock_ui_tick`.

The supported 2.1 original UI and patched Helix use 22 ms display and animation
periods independently of GPU availability. Helix pointer polling also uses
22 ms. These periods fit the previously measured panel mode of about 45 Hz;
no 60/120 Hz mode is verified. The timer period is not a promise of one distinct
G-code frame per tick.

## Measuring changes

On original GUI 2.1, `/tmp/stock-ui-perf.json` records completed LVGL renders in
five-second windows and excludes idle refresh callbacks. Flush timings are
callback and wait durations, not panel scanout or touch latency. Applied periods
are recorded in `/tmp/stock-display.log`.

For bounded redraw measurement on an idle device, write a duration from 1 to 15
seconds to `/tmp/stock-display-benchmark-seconds`. This invalidates the existing
page without input or navigation. Compare complete five-second windows inside
that interval on the same page with the same observers.

`UI_DISPLAY_BASELINE=1` restores the 33 ms policy and skips the new presenter.
It is a previous-display-path baseline, not an isolated timer comparison.
For a renderer-output comparison, use `UI_GPU_OUTPUT=readback` versus automatic
output with GPU enabled and identical geometry, viewport and cadence. The
Firmware GPU switch changes G-code availability, so it cannot isolate output
cost. Previous-release measurements are listed in the architecture document.

## Build and checks

The feature firmware workflow calls `helix-gpu.yaml`, then verifies and installs
its artifact. The cache key includes the pinned Helix patch, shared ABI/display
inputs, workflow and package version. Missing, stale or altered artifacts fail
the build. A local firmware build needs the verified artifact in `ui-artifact/`.

On a Linux build host:

```sh
python3 -m unittest discover -s overlays/mods/BICHENG/71-ui-gpu/test -p 'test_*.py'
make -C overlays/mods/BICHENG/71-ui-gpu/apps/stock-display test
make -C overlays/mods/BICHENG/71-ui-gpu/apps/gcode-preview test
```

The display and G-code host tests include sanitizer checks. They do not replace
device tests of EGL imports, framebuffer flips, source changes, widget teardown
or suspend. The final Helix link and 26 startup/hook tests have passed. A device
EGL smoke check through `renderD128` reported Mali-G52 and no GL error. Full
two-page flash acceptance and both interfaces' visual acceptance are pending.

For an isolated device smoke check, run `test/egl_smoke.py` as root while idle
with GPU previews closed. It draws an offscreen 16×16 triangle, checks pixels,
renderer identity and cleanup, and sends no screen or touch input. The child
process closes its driver descriptors before the parent checks runtime suspend.
Other clients holding `/dev/mali0` make the suspend result inconclusive.
