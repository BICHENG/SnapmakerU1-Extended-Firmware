# Original U1 screen with GPU G-code preview

The stock-ui branch retains Snapmaker's original /usr/bin/gui and its file selection, material mapping, printing and settings screens. It targets vendor **2.0.0.205**, with Fan Curves, Balanced-only reduced motor current and the ALSA network microphone. The fork's develop branch tracks upstream independently. No upstream pull request is part of this work.

## Rendering

Fluidd parses G-code in a browser worker and draws paths as SVG. The factory screen displays slicer thumbnails. The personal extension adds **路径预览** to the factory model thumbnail. During a print, that thumbnail becomes a live GPU viewport inside the original printing page. Tapping it opens a larger view with rotation, zoom and 3D/2D controls. Returning restores the factory page and its controls.

The extension follows the current file and layer automatically. It indexes extrusion segments by their source byte offsets and colors the prefix reported by virtual_sdcard.file_position; remaining paths are dimmed. A green marker uses motion_report.live_position after applying the reported G-code origin. Selecting a layer manually leaves follow mode; **跟随打印** returns to the current progress. Pauses retain the last path, completion shows the whole model, and unavailable status freezes progress with a connection message.

The file cursor is the interpreter's progress and can lead physical movement queued in Klipper. The nozzle marker is a separate live position sample. This display does not confirm physical extrusion or provide step-synchronous tracking.

This profile builds and selects the original screen. The upstream optional HelixScreen component remains in the general firmware source; its separate custom GPU build has been removed.

## Compatibility and ownership

The vendor GUI statically links LVGL and does not export its APIs. The adapter supports only the 7,010,568-byte executable with SHA-256:

    bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4

Both checks pass before any private entry point is called. Unsupported GUI versions run without the extension. This is a binary compatibility adapter, not an official Snapmaker plugin interface. Vendor updates require a new adapter and device verification.

The usleep interposition acts only at two verified call sites in the original main loop. All LVGL calls run there under the existing LVGL mutex. One worker handles parsing, GPU work and nonblocking libcurl requests; it never calls LVGL. It reuses the firmware's libcurl and Boost.JSON libraries. No extra service is installed. The original executable is neither modified nor replaced.

While printing or paused, the worker requests status about every 250 ms. Other visible previews check once per second. HTTP work is asynchronous so the vendor status endpoint's response delay cannot stall rotation or rendering. Queries stop when no supported model image or expanded preview is visible. Unchanged state does not redraw the GPU viewport.

The parser supports G0/G1 extrusion moves, relative/absolute coordinates and extrusion, G92, millimetres/inches, T0–T3 and XY arcs using relative I/J or R. Unsupported arc modes and oversized geometry display an error. Limits are 1,000,000 vertices, 20,000 layers and 512 MB per file. Slicer layer markers exclude preceding purge moves. Travel moves do not become printed geometry.

The preview supports readable .gcode files whose factory thumbnail resolves inside /userdata/gcodes. Other sources retain the factory thumbnail. Colors are fixed by T0–T3 rather than the configured filament color.

## GPU and refresh

The GPU overlay enables the original disabled Mali-G52 device-tree node, uses the existing vdd_gpu regulator, builds the matching bifrost_kbase module and installs the pinned Mali g24p0 library and licence. The GPU is capped at 400 MHz, with runtime power management. Panel timings and voltage tables are unchanged.

GPU startup uses [components] mali_gpu = true. The preview is enabled in this personal profile when /dev/mali0 exists; [components] stock_preview = false disables it. The profile explicitly selects gui = snapmaker.

The factory refresh period changes from 33 ms to 22 ms. The 480 × 320 panel is approximately 45 Hz. Ordinary factory widgets retain LVGL software rendering; the G-code viewport uses the GPU. Rendering readback and framebuffer capture each involve a copy.

A temporary guard protects initial extension startup and an active GPU viewport. If the GUI exits unexpectedly during either, its next launch runs the factory UI without the adapter. GPU resources and geometry are released when neither the live thumbnail nor the expanded preview is active. The guard clears after that normal return, or on reboot.

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

Live-follow development acceptance used the real virtual SD interface and original printing screen. A controlled fixture executed only G4 waits and M25 pauses; M26 skipped every movement and extrusion line. It verified a partial layer becoming complete, automatic layer changes, pause, manual browsing and return to follow mode, job completion and WebRTC interaction. Positions, tool switch counters and heater targets remained unchanged. This establishes the live data and rendering path, not physical extrusion synchronization or long-print stability.
