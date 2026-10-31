# GUI delivery stages

The original Snapmaker executable and screen remain the product UI. The gui branch integrates three independently reviewable stages; stock-ui combines gui with Fan Curves, Balanced reduced current and the ALSA network microphone. Shared build dependencies are not runtime dependencies between stages.

| Stage | Branch | Scope | Depends on |
| --- | --- | --- | --- |
| 1 | gui-gpu | Mali kernel module, existing GPU regulator and device tree, pinned EGL/GLES runtime, opt-in startup and operating limit | Upstream develop |
| 2 | gui-remote | Framebuffer capture, MPP H.264 encoding, WebRTC video and touch, connection lifecycle | Upstream develop and its existing camera libraries |
| 3 | gui-gcode | G-code parsing and progress, GLES viewport, original UI adapter and controls | gui-gpu |

The implementation directories are 71-ui-gpu, 61-app-remote-screen and 72-stock-ui respectively. Stage 2 uses Rockchip's video encoder, not the Mali graphics processor. It works with the factory software renderer and does not require stage 1 or stage 3.

## Stage 1: provide GPU capability

Keep this stage independent of the private factory GUI ABI. Reuse the pinned kernel, existing voltage table, runtime power management and a checked 400 MHz limit. Do not alter panel timings or add another service manager. An independent EGL check must identify the actual hardware renderer, verify rendered pixels and release its resources. Runtime-suspend checks must account for other active GPU consumers.

Enabling /dev/mali0 alone does not accelerate factory widgets. The original LVGL text, buttons and lists remain software-rendered. Stage 3's 22 ms refresh timer is scheduling, not a GPU drawing backend.

## Stage 2: replace interactive screenshots

The interactive path is framebuffer -> MPP H.264 -> WebRTC video. Touch travels over the authenticated session's data channel to the existing input device. Snapshot access remains for thumbnail clients.

A refresh must replace only that browser page's abandoned session, and closing a page must release its session and touch ownership. Other viewers must retain their sessions. Keep the two-viewer limit; increasing it is not a fix for leaked clients. Reconnection, unfinished negotiation and session expiry belong to this stage. A failure of the remote service must not restart the factory GUI or printing services.

## Stage 3: draw live G-code in the factory screen

The renderer parses each file once, uploads geometry to the GPU, renders only changed views, and releases resources when hidden. The selected layer is opaque; other layers are composited once at 24% opacity. Supersampling and the original screen are retained during performance work.

The factory executable is statically linked and has no supported plugin interface in this implementation. The adapter checks exact executable hashes before calling private LVGL functions. Keep private structure offsets in the version description, document tested versions, and fall back to the original image on rendering failure. An exit guard does not isolate a running GUI from a hung GPU call.

The current frame path includes synchronous GPU readback and CPU copies into LVGL. Measure drawing, resolve/readback and pixel delivery separately before replacing it. Do not claim zero-copy or full-system GPU acceleration. The measured panel configuration is about 45 Hz, so the first performance target is a 22.2 ms frame budget at unchanged visual quality, not an unverified 60/120 Hz timing.

The file cursor tracks interpreter progress, which can lead physical movement. It is not a precise nozzle-position signal.

## Evidence and release boundaries

At the initial split, all three capabilities were already present in gui commit df33ccc and deployed through stock-ui commit 64ad82b on vendor 2.1.0.157. Earlier G-code development measurements used a 70,370-vertex model: 38.47 ms median and 46.4 ms p95 across 40 warm rotating frames with 2x supersampling. They are a baseline, not measurements of later optimization commits.

The 2.1 deployment record contains boot, configuration, MCU, audio and read-only WebRTC checks. The user accepted the preceding UI interaction behavior; no additional automated UI input was performed after they asked to stop. Long-print behavior, total UI latency and acoustic effects are not established by a successful build.

Keep per-commit build and measured results with the deployment record. Each stage uses the existing Feature firmware workflow. A passed stage build does not imply that every factory GUI version or board revision is supported. Do not create pull requests or rewrite existing published history as part of routine synchronization.
