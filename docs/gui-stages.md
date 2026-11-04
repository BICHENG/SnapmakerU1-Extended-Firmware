# GUI delivery stages

Current architecture, measurements and module boundaries are maintained in [GPU architecture](gpu-architecture.md). Branch ownership is maintained in [Personal feature branches](feature-branches.md).

| Capability | Owner | Current implementation |
| --- | --- | --- |
| GPU runtime and native widget drawing | gpu | Pinned Mali, hash-gated factory NanoVG adapter, recompiled Helix NanoVG draw unit and software recovery |
| Real-time G-code | gpu | One native engine shared by factory and Helix adapters; mutable image invalidation and current-layer controls |
| Complete-frame presentation | gpu | Independent 22 ms scheduling, bounded asynchronous presentation and two-page VBL flips |
| Remote display and operation | gui-remote | Completed snapshot to MPP/WebRTC, reliable touch edges and latest-only motion |
| Combined acceptance | gui → stock-ui | Source-bound Actions artifact, device download, slot/readback checks and real UI interactions |

The phase split describes capabilities, not extra long-lived implementation branches. `gui-gcode` and `gui-gpu` retain historical research. Automatic factory UI recreation remains a fallback; native drawing now works without replacing its UX.

GPU takeover does not establish a speedup. Current widget rendering costs more than CPU in the measured Settings pages. Keep CPU baselines, timing-policy ablations and real-menu results separate. The panel is about 45 Hz; full G-code geometry can exceed its frame budget. Do not infer 60/120 Hz operation from a configured timer.
