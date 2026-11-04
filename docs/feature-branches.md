# Personal feature branches

The maintained branches below include upstream `e086afa5068e3f724aa6ff9ca193dc742213c3c3`, checked on 2026-11-04. Publish work to the owner's fork. Opening a PR remains a separate user decision.

| Branch | Responsibility | Build profile |
| --- | --- | --- |
| fancurves | Fan curves, editor and Klipper integration | extended |
| balanced-low-current | Permit Balanced with reduced X/Y current; retain the Aggressive restriction | extended |
| printers-sonic | Network microphone through ALSA and snd-aloop | extended-BICHENG |
| gpu | Mali runtime, native factory/Helix widget drawing, shared G-code and complete-frame presentation | extended-BICHENG |
| gui-remote | Complete-frame capture, MPP/WebRTC video and DataChannel touch | extended |
| gui | Integration of gpu and gui-remote | extended-BICHENG |
| stock-ui | Daily integration of gui, fancurves, balanced-low-current and printers-sonic | extended-BICHENG |
| develop | Unmodified upstream develop | Upstream defaults |

Merge upstream into each feature branch, then merge the UI features into `gui` and product features into `stock-ui`. Preserve published history. Branch advancement, Actions success and a verified printer deployment are separate facts.

`71-ui-gpu` owns GPU startup, shared GPU output, widget renderers, the G-code library, presentation and the Helix adapter. `72-stock-ui` owns only the factory G-code controls and lifecycle adapter. It does not keep a second parser, camera or rendering engine. See [GPU architecture](gpu-architecture.md) for tested behavior, failure handling and measurements.

The existing `stock-ui.yaml` builds the seven delivery branches and verifies the pinned Helix artifact where required. Artifacts contain the firmware and SHA256SUMS. The workflow does not create PRs, publish releases or flash printers.

Balanced plus reduced current remains distinct from current upstream: both upstream enable paths still reject the combination. Remove the patch only after upstream supports both enable orders with the same intended restrictions.

`gui-gcode`, `gui-gpu`, `fan-curves`, `feature/max-speed-reduced-current`, `feature/snd-aloop`, `auxmic` and `personal-ui` are historical references. Native widget drawing has graduated into `gpu`; the old `gui-gpu` compositor and automated recreation research are not the production implementation. No historical branch needs to be merged merely to obtain the new renderer.
