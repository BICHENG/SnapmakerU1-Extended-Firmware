# Personal feature branches

The fork separates shared GPU support from UI rendering development. Changes are pushed to the owner's fork; opening a pull request requires a separate decision.

| Branch | Scope | Build profile |
| --- | --- | --- |
| fancurves | Fan curve configuration, editor and Klipper integration | extended |
| balanced-low-current | Allow Balanced with reduced X/Y current; keep Aggressive blocked | extended |
| printers-sonic | Phone/network microphone presented through ALSA, including snd-aloop | extended-BICHENG |
| gpu | Mali runtime, shared output, Firmware switch, stock/Helix adapters and baseline measurement | extended-BICHENG |
| gui-remote | Framebuffer MPP/WebRTC video and remote touch; independent of Mali | extended |
| gui-gcode | GPU G-code viewport and version-checked factory UI adapter; includes gpu | extended-BICHENG |
| gui-gpu | Experimental factory widget GPU rendering and automated reconstruction feasibility; based on gpu | Research only |
| gui | Integration of gpu, gui-gcode and gui-remote | extended-BICHENG |
| stock-ui | Integration of the four features for the owner's printer | extended-BICHENG |
| develop | Unmodified upstream develop | Upstream defaults |

The former gui-gpu foundation branch was renamed gpu. Its commits and runtime code are retained. The new gui-gpu starts from gpu and is reserved for research into factory UI drawing and automated reconstruction. It is not an implemented replacement UI and stays outside routine gui and stock-ui integration. Its detailed plan lives in docs/gui-gpu.md on that experimental branch.

Keep changes on the branch that owns them. Merge upstream develop into fancurves, balanced-low-current, printers-sonic, gpu and gui-remote. Update gui-gcode and the experimental gui-gpu through gpu. Integrate gui-gcode and gui-remote into gui, then integrate the four product features into stock-ui. Do not rewrite published history. A future accepted gui-gpu change needs an explicit integration decision.

The G-code stage consumes the foundation's common output module and display tick. The remote stage remains independent. GPU use is opt-in and does not replace the factory LVGL widget renderer. See [GPU architecture](gpu-architecture.md) and [GUI delivery stages](gui-stages.md) for boundaries and acceptance evidence.

The existing stock-ui.yaml workflow builds the eight delivery branches with the upstream toolchain. On GPU-bearing branches it also calls the pinned Helix build and verifies its source manifest before installation. It selects the BICHENG profile only when that branch contains BICHENG mods. Artifacts are named <branch>-firmware and contain the firmware image and SHA256SUMS. It does not publish releases, open pull requests or flash a printer. gui-gpu is excluded from automatic firmware builds while it contains only research documentation; workflow_dispatch remains available for a future buildable candidate.

The branch reorganization changes documentation and workflow branch selection only. Its commits use [skip ci]; previously accepted runtime commits and deployed firmware keep their own build records. A new branch head is not labelled as a freshly built or deployed firmware.

Balanced plus reduced current remains a separate feature: upstream 36fbd40 still rejects it in both enable orders. If upstream later implements the same behavior, compare both paths and retire the patch instead of maintaining a duplicate. The printer's own successful use does not establish safety under all print loads.

The old fan-curves, feature/max-speed-reduced-current, feature/snd-aloop, auxmic and personal-ui branches retain historical work. Active work belongs to the branches above. Existing module paths such as 71-ui-gpu, auxmic endpoints and ALSA device names remain stable; branch naming does not require an API or installed-path migration.

Build success is a packaging check. A stock-ui deployment additionally needs the exact original GUI hash, an idle printer with heater targets at zero, a recoverable previous slot, native UI and WebRTC checks, and restart acceptance. Current firmware and hardware evidence are kept in the local deployment record.
