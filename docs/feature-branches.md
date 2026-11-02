# Personal feature branches

Maintain one GPU rendering feature, including its runtime and the factory/Helix G-code consumers. Changes go to the owner's fork; opening a pull request requires a separate decision.

| Branch | Scope | Build profile |
| --- | --- | --- |
| fancurves | Fan curve configuration, editor and Klipper integration | extended |
| balanced-low-current | Allow Balanced with reduced X/Y current; keep Aggressive blocked | extended |
| printers-sonic | Phone/network microphone presented through ALSA, including snd-aloop | extended-BICHENG |
| gpu | Mali runtime and shared output, live G-code in the factory UI, and the version-matched Helix G-code adapter | extended-BICHENG |
| gui-remote | Framebuffer MPP/WebRTC video and remote touch; independent of Mali | extended |
| gui | Integration of gpu and gui-remote | extended-BICHENG |
| stock-ui | Integration of gui, Fan Curves, Balanced reduced current and the network microphone | extended-BICHENG |
| gui-gpu | Factory widget drawing and automated reconstruction experiments, based on gpu | Research only |
| develop | Unmodified upstream develop | Upstream defaults |

The former gui-gcode branch was merged into gpu on 2026-11-02. Its published history remains available as a frozen reference; it is no longer an active owner or automatic firmware build target. Helix adaptation was already in gpu. Consolidation adds the existing factory G-code implementation without rewriting either renderer or moving installed files.

Merge upstream develop into fancurves, balanced-low-current, printers-sonic, gpu and gui-remote. Integrate gpu and gui-remote directly into gui, then integrate the four product features into stock-ui. Update the experimental gui-gpu through gpu separately. Preserve published history; do not merge the experimental renderer into daily firmware without an explicit acceptance decision.

Inside gpu, keep module ownership separate: 71-ui-gpu owns the driver, shared output, display adapter and Helix adaptation; 72-stock-ui owns factory G-code parsing, progress, rendering and controls. A shared branch does not require a shared UI implementation. Ordinary widgets still use CPU rendering. See [GPU architecture](gpu-architecture.md) and [GUI delivery stages](gui-stages.md).

The existing stock-ui.yaml workflow builds seven delivery branches. GPU-bearing branches reuse the pinned Helix build and verify its source manifest before installation. The BICHENG profile is selected only when a branch contains BICHENG mods. Artifacts are named <branch>-firmware and contain the firmware image and SHA256SUMS. The workflow does not publish releases, open pull requests or flash a printer.

gui-gpu remains outside automatic firmware builds. Its native-ui.yaml checks diagnostics without packaging them; workflow_dispatch remains available for an accepted candidate. Its first widget compositor matched tested pixels but was slower than CPU, and it contains no replacement UI. Research belongs in tools/native-ui and docs/gui-gpu.md on that branch.

The gpu [maintenance boundary](gpu-architecture.md#maintenance-boundary) applies to the combined feature. Keep build commits, current documentation heads and deployed firmware distinct. Consolidation does not establish new performance gains or require flashing unchanged runtime code.

Balanced plus reduced current remains a separate feature: upstream 36fbd40 still rejects it in both enable orders. If upstream implements the same behavior, compare both paths and retire the duplicate patch. The printer's successful use does not establish safety under all print loads.

Historical branches, including gui-gcode, fan-curves, feature/max-speed-reduced-current, feature/snd-aloop, auxmic and personal-ui, retain earlier work. Active work belongs to the branches listed above. Existing module paths, auxmic endpoints, configuration keys and ALSA device names remain stable.

Build success is a packaging check. A stock-ui deployment additionally needs the exact original GUI hash, an idle printer with heater targets at zero, a recoverable previous slot, UI and remote checks, and restart acceptance. Hardware evidence remains tied to the image that was actually installed.
