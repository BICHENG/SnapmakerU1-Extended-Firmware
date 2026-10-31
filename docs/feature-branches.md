# Personal feature branches

The fork maintains four independent feature branches on upstream develop. Changes are pushed to the fork for review; opening a pull request requires a separate decision.

| Branch | Scope | Build profile |
| --- | --- | --- |
| fancurves | Fan curve configuration, editor and Klipper integration | extended |
| balanced-low-current | Allow Balanced with reduced X/Y current; keep Aggressive blocked | extended |
| printers-sonic | Phone/network microphone presented through ALSA, including snd-aloop | extended-BICHENG |
| gui | Original Snapmaker GUI, Mali G-code rendering, framebuffer WebRTC and touch | extended-BICHENG |
| stock-ui | Integration of the four features for the owner's printer | extended-BICHENG |
| develop | Unmodified upstream develop | Upstream defaults |

Each feature contains only its changes plus the shared firmware workflow and this branch map. Features do not depend on stock-ui. Keep changes on their owning feature branch, then merge them into stock-ui. Merge upstream develop into each feature and integrate only after their builds pass. Do not rewrite published branch history to update it.

The existing stock-ui.yaml workflow builds these branches with the upstream toolchain. It selects the BICHENG profile only when that branch contains BICHENG mods. Artifacts are named <branch>-firmware and contain the firmware image and SHA256SUMS. It does not publish releases, open pull requests or flash a printer.

Balanced plus reduced current remains a separate feature: upstream 36fbd40 still rejects it in both enable orders. If upstream later implements the same behavior, compare both paths and retire the patch instead of maintaining a duplicate. The printer's own successful use does not establish safety under all print loads.

The old fan-curves, feature/max-speed-reduced-current, feature/snd-aloop, auxmic and personal-ui branches retain historical work. New changes belong to the four branches above. The auxmic service, endpoints and ALSA device names remain stable inside printers-sonic; the branch name does not require an API migration.

Build success is a packaging check. A stock-ui deployment additionally needs the exact original GUI hash, an idle printer with heater targets at zero, a recoverable previous slot, native UI and WebRTC checks, and restart acceptance. Current firmware and hardware evidence are kept in the local deployment record.
