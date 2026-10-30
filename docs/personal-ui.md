# Original U1 screen with GPU G-code preview

The stock-ui branch retains Snapmaker's original /usr/bin/gui and its file selection, material mapping, printing and settings screens. It targets vendor **2.0.0.205**, with Fan Curves, Balanced-only reduced motor current and the ALSA network microphone. The fork's develop branch tracks upstream independently. No upstream pull request is part of this work.

## Rendering

Fluidd parses G-code in a browser worker and draws paths as SVG. The factory screen displays slicer thumbnails. The personal extension adds **路径预览** to the factory model thumbnail. It uses an LVGL image and native buttons inside the existing GUI process. A worker parses extrusion paths and renders them through Mali-G52 OpenGL ES. The user can rotate, zoom, switch between 3D and single-layer 2D, and select a layer. Returning closes the preview and restores the factory page.

This profile builds and selects the original screen. The upstream optional HelixScreen component remains in the general firmware source; its separate custom GPU build has been removed.

## Compatibility and ownership

The vendor GUI statically links LVGL and does not export its APIs. The adapter supports only the 7,010,568-byte executable with SHA-256:

    bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4

Both checks pass before any private entry point is called. Unsupported GUI versions run without the extension. This is a binary compatibility adapter, not an official Snapmaker plugin interface. Vendor updates require a new adapter and device verification.

The usleep interposition acts only at two verified call sites in the original main loop. All LVGL calls run there under the existing LVGL mutex. Parsing and GPU work run in one worker that never calls LVGL. The original executable is neither modified nor replaced.

The parser supports G0/G1 extrusion moves, relative/absolute coordinates and extrusion, G92, millimetres/inches, T0–T3 and XY arcs using relative I/J or R. Unsupported arc modes and oversized geometry display an error. Limits are 1,000,000 vertices, 20,000 layers and 512 MB per file. Slicer layer markers exclude preceding purge moves. Travel moves do not become printed geometry.

The preview supports readable .gcode files whose factory thumbnail resolves inside /userdata/gcodes. Other sources retain the factory thumbnail. Layer selection is manual and does not yet follow live print progress.

## GPU and refresh

The GPU overlay enables the original disabled Mali-G52 device-tree node, uses the existing vdd_gpu regulator, builds the matching bifrost_kbase module and installs the pinned Mali g24p0 library and licence. The GPU is capped at 400 MHz, with runtime power management. Panel timings and voltage tables are unchanged.

GPU startup uses [components] mali_gpu = true. The preview is enabled in this personal profile when /dev/mali0 exists; [components] stock_preview = false disables it. The profile explicitly selects gui = snapmaker.

The factory refresh period changes from 33 ms to 22 ms. The 480 × 320 panel is approximately 45 Hz. Ordinary factory widgets retain LVGL software rendering; the G-code viewport uses the GPU. Rendering readback and framebuffer capture each involve a copy.

A temporary guard protects initial extension startup and an open preview. If the GUI exits unexpectedly during either, its next launch runs the factory UI without the adapter. Closing the preview releases GPU resources and geometry. The guard clears after a normal return, or on reboot.

## WebRTC

The fb-mpp process reads /dev/fb0 and hardware-encodes H.264 through MPP. The stream-screen-webrtc process carries video and touch through WebRTC. Encoding stops without viewers. The authenticated /screen/webrtc endpoint handles signalling, and /screen/ provides interaction.

The snapshot endpoint remains for thumbnail clients. Interactive streaming does not poll PNG screenshots. Direct ICE connectivity is supported; no TURN relay is bundled.

## Build and verification

The Original UI firmware workflow builds the firmware directly, checks the parser with AddressSanitizer and UndefinedBehaviorSanitizer, and uploads a checksummed artifact. It creates neither releases nor pull requests.

Development checks on 2026-10-31 established:

- The Chinese factory material configuration page and a native G-code preview run in the original GUI.
- SHOULDER_PETG_21m46s.gcode produces 70,370 vertices and 157 extrusion layers. GUI RSS is about 73 MB with the preview open.
- Mali-G52 renders the viewport, and the factory refresh timer reads 22 ms.
- A browser rotation test observed approximately 32 changed frames per second. The sustained WebRTC stream returned to 45 fps, decoding 1,609 frames with zero dropped frames.
- Returning from the preview restores the factory material configuration page.

These are development checks. The deployment record must separately identify the final image, active slot, reboot acceptance and preserved printer configuration.
