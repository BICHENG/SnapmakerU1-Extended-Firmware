# GUI delivery stages

The original Snapmaker application remains the primary UI. gpu now owns the complete GPU rendering feature, including both G-code consumers. gui combines it with gui-remote; stock-ui adds Fan Curves, Balanced reduced current and the ALSA microphone.

| Delivery | Branch | Scope |
| --- | --- | --- |
| GPU rendering | gpu | Driver, shared output, live factory G-code viewport and version-matched Helix G-code adaptation |
| Remote screen | gui-remote | Framebuffer capture, MPP H.264, WebRTC video/touch and connection lifecycle |
| GUI integration | gui | gpu plus gui-remote |
| Research | gui-gpu | Factory widget GPU drawing and automated reconstruction; excluded from daily integration |

The earlier three-stage split remains useful as implementation history, but the foundation and gui-gcode are now one maintained branch. gui-gcode is a frozen historical reference, not another integration step.

## GPU rendering

Keep the runtime and consumers in their current modules. 71-ui-gpu owns the pinned Mali driver, checked 400 MHz cap, runtime suspend, shared DMA-BUF output, Firmware switch and display/Helix adapters. 72-stock-ui owns the factory G-code parser, progress, viewport and controls. There is no additional graphics daemon.

The factory renderer parses each file once, uploads geometry, redraws changed views and releases resources when hidden. The selected layer is opaque; other layers are composited once at 24% opacity. Supersampling, current-layer following, manual layer browsing and the original time estimate remain part of this feature. The file cursor tracks interpreter progress, which can lead physical movement.

The private factory ABI is guarded by executable size and hash. stock-display owns the single main-loop sleep hook and invokes stock_ui_tick. The worker handles GPU, parsing and nonblocking HTTP work; it does not call LVGL. Unknown binaries retain the original UI. A startup/exit guard is not isolation from a hung GPU call.

Helix 1.0.7 uses its existing GLES G-code renderer with the same shared output source, bounded geometry and matching asset checks. It retains the software widget backend and official-executable fallback. No user renderer adjustment is required, and the firmware preserves the selected UI.

GPU completion, CPU synchronization and final delivery remain in the measured frame path. Failed imports use readback; failed synchronization aborts the frame. Compare UI_GPU_OUTPUT=readback with automatic output in the same binary and at unchanged geometry and quality. Complex scenes can exceed the current 22.2 ms panel budget.

## Original widget refresh is a separate effect

Ordinary factory and Helix widgets remain CPU-rendered. The current setting still couples GPU availability with 22 ms display/animation periods; the disabled baseline is 33 ms. The factory 2.0 adapter adjusts only its verified display timer.

The 2026-11-01 factory-homepage ablation measured 30.15/45.15 fps at 33/22 ms with the GPU driver absent, and 30.20/45.20 fps with it present. Four GPU-on sessions recorded no additional GPU active time during measurement. Refresh policy should be decoupled from the GPU switch; this branch consolidation does not implement that change or claim GPU widget acceleration.

## Remote screen

gui-remote uses framebuffer -> MPP H.264 -> WebRTC. Touch travels over the authenticated session's data channel. MPP is separate from Mali, so remote operation does not depend on GPU rendering. Snapshots remain for thumbnails.

Refreshing a browser page replaces its abandoned session; closing it releases its session and touch ownership. Other viewers retain their sessions. Keep the two-viewer limit and fix lifecycle leaks at their owner. Remote failures must not restart the factory UI or printing services.

## Research and release boundaries

gui-gpu stays outside gui and stock-ui. Its measured widget compositor was slower than CPU despite matching tested pixels. Continue whole-widget GPU work only for an identified expensive page with a complete-frame benefit. Automatic reconstruction remains a fallback requiring portable resources, generated views and state/interaction replay.

The existing Feature firmware workflow builds the combined GPU feature. Per-commit build evidence remains separate from image deployment and device acceptance. User-accepted interaction checks are not repeated merely because branches were consolidated. Long-print stability, latency and acoustic effects are not established by build success. See [GPU architecture](gpu-architecture.md) for ownership and comparison rules.
