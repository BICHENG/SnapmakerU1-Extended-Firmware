# U1 GPU runtime and display

This overlay owns Mali startup, native widget drawing, shared G-code rendering
and complete-frame presentation. The factory UI keeps its layout and business
logic. [GPU architecture](../../../../docs/gpu-architecture.md) is the maintained
reference for module boundaries, rendering contracts, measurements and limits.

## Configuration

Use **Firmware → Snapmaker components → Touchscreen GPU rendering**, or set
`mali_gpu = true` in `[components]`. The setting preserves UI selection and
reboots only when idle with every heater target at zero. GPU off retains the
22 ms display policy, complete-frame presentation and normal thumbnails.

Builds require the supported RK3562 board, existing `vdd_gpu` regulator and
400 MHz OPP entries. Startup sets and reads back the 400 MHz cap. Panel timings
and the voltage table are unchanged. An unsupported board fails the build;
an unknown factory executable bypasses private rendering calls.

## Source ownership

| Directory | Owns |
| --- | --- |
| `apps/stock-gpu` | Verified factory LVGL draw-task translation and software recovery |
| `apps/helix-gpu` | Helix NanoVG integration, LVGL fixes and U1 preview widget source |
| `apps/gcode-preview` | Source identity, parsing, view/follow state, GLES rendering and completed preview frames |
| `apps/ui-gpu` | DMA-BUF allocation, EGL import and CPU read synchronization |
| `apps/ui-display` | Complete canvas, bounded pending frame, framebuffer presentation and capture snapshot |
| `apps/stock-display` | Sole factory main-loop hook, cadence and render measurement |

`helix-gpu-shared.patch` changes upstream integration points. Owned adapter code
is copied from `apps/helix-gpu`; do not embed a second copy in the patch.
The factory G-code widget lives in `72-stock-ui`. WebRTC sessions, encoding and
touch belong to `gui-remote` and consume `common/ui-frame.h`.

## Build and verification

The existing feature workflow builds pinned Helix through `helix-gpu.yaml`,
verifies its source manifest, then installs it into the firmware. Cache keys
include the owned adapter, shared ABI and renderer inputs. A local build needs
that verified artifact in `ui-artifact/`; stale artifacts fail verification.

On a Linux host:

```sh
python3 -m unittest discover -s overlays/mods/BICHENG/71-ui-gpu/test -p 'test_*.py'
make -C overlays/mods/BICHENG/71-ui-gpu/apps/stock-display test
make -C overlays/mods/BICHENG/71-ui-gpu/apps/gcode-preview test
```

The preview tests exercise real parsing, lifecycle cancellation, frame metadata,
padded output, file replacement and viewport/follow behavior with a controlled
GPU worker. Device acceptance must also exercise both actual UIs, matching ABI
artifacts, EGL imports, teardown, framebuffer flips and idle suspend.

For diagnosis use `/tmp/ui-renderer.json`, `/tmp/ui-display.json` and, for the
factory UI, `/tmp/stock-ui-perf.json`; check the producer PID. Measurement methods
and their limits are in the architecture document. `test/egl_smoke.py` performs
a bounded offscreen triangle check on an idle printer with previews closed.
