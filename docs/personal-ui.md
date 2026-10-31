# Original U1 screen with GPU G-code preview

The gui feature branch retains Snapmaker's original /usr/bin/gui and its file selection, material mapping, printing and settings screens. The stock-ui integration branch combines it with Fan Curves, Balanced-only reduced motor current and the ALSA network microphone. All active feature branches follow the vendor version pinned by upstream develop. No upstream pull request is part of this work.

## Rendering

Fluidd parses G-code in a browser worker and draws paths as SVG. The factory screen displays slicer thumbnails. Tapping the factory model image opens the personal extension's full-screen preview. During a print, the entire thumbnail becomes a live GPU viewport inside the original printing page. There is no separate preview button. The image's touch area stops scroll propagation and distinguishes a tap from a drag; dragging it cannot scroll the factory page out of view.

The extension follows the current file and layer automatically. The selected layer is opaque; all other layers form a translucent context at 24% opacity. Context is composited once so overlapping layers cannot accumulate into an opaque silhouette. The renderer indexes extrusion segments by their source byte offsets and colors the prefix reported by virtual_sdcard.file_position; remaining paths in the current layer are dimmed. **当前层** is highlighted while following the current print. Selecting another layer clears that highlight; pressing the button resumes following. Pauses retain the last path, completion selects the last layer, and unavailable status freezes progress with a connection message. No nozzle-position marker is drawn or queried.

The expanded preview belongs to LVGL's top layer and stays at the screen origin. The canvas fills the screen: back is at the upper left, zoom and 2D/3D at the upper right, layer information at the lower left, and 当前层 at the lower right. Only a thin layer slider runs along the bottom. The time beside the layer information is copied from the original screen's remaining-time label, or its file estimate before printing; no independent time estimate is computed. Scrolling and gesture propagation are disabled on the controls. A touch selects either rotation or layer browsing when pressed; dragging across their boundary cannot move the overlay or switch interaction modes.

The file cursor is the interpreter's progress and can lead physical movement queued in Klipper. This display does not confirm physical extrusion or provide step-synchronous tracking.

This profile builds and selects the original screen. The upstream optional HelixScreen component remains in the general firmware source; its separate custom GPU build has been removed.

## Compatibility and ownership

The vendor GUI statically links LVGL and does not export its APIs. The adapter selects private entry points by executable size and SHA-256:

| Vendor GUI | Bytes | SHA-256 |
| --- | ---: | --- |
| 2.0.0.205 | 7,010,568 | bd2d80e9084c6709161fd9ca4130f06c57e89b00ffa596bc6c5706c4f1aebda4 |
| 2.1.0.157 | 6,563,736 | 2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472 |

Both checks pass before any private entry point is called. Unsupported GUI versions run without the extension; firmware builds reject an unknown hash. This is a binary compatibility adapter, not an official Snapmaker plugin interface. Vendor updates require a new adapter and device verification. The 2.1 adapter was mapped against the official binary; its device acceptance is recorded separately from the 2.0 checks below.

The usleep interposition acts only at two verified call sites in the original main loop. All LVGL calls run there under the existing LVGL mutex. One worker handles parsing, GPU work and nonblocking libcurl requests; it never calls LVGL. It reuses the firmware's libcurl and Boost.JSON libraries. No extra service is installed. The original executable is neither modified nor replaced.

While printing or paused, the worker requests status about every 250 ms. Other visible previews check once per second. HTTP work is asynchronous so the vendor status endpoint's response delay cannot stall rotation or rendering. Queries stop when no supported model image or expanded preview is visible. Unchanged state does not redraw the GPU viewport.

The parser supports G0/G1 extrusion moves, relative/absolute coordinates and extrusion, G92, millimetres/inches, T0–T3 and XY arcs using relative I/J or R. Unsupported arc modes and oversized geometry display an error. Limits are 1,000,000 vertices, 20,000 layers and 512 MB per file. Slicer layer markers exclude preceding purge moves. Travel moves do not become printed geometry.

The preview supports readable .gcode files whose factory thumbnail resolves inside /userdata/gcodes. Other sources retain the factory thumbnail. Colors are fixed by T0–T3 rather than the configured filament color.

## GPU and refresh

The GPU overlay enables the original disabled Mali-G52 device-tree node, uses the existing vdd_gpu regulator, builds the matching bifrost_kbase module and installs the pinned Mali g24p0 library and licence. The GPU is capped at 400 MHz, with runtime power management. Panel timings and voltage tables are unchanged.

GPU startup uses [components] mali_gpu = true. The preview is enabled in this personal profile when /dev/mali0 exists; [components] stock_preview = false disables it. The profile explicitly selects gui = snapmaker.

The factory refresh period changes from 33 ms to 22 ms. The 480 × 320 panel is approximately 45 Hz. Ordinary factory widgets retain LVGL software rendering; the G-code viewport uses GLES 3 on the GPU. It renders at twice the viewport dimensions (960 × 640 for a full-screen view), then downsamples on the GPU before reading back native-resolution pixels. This reduces jagged edges without changing the physical panel resolution. Rendering readback and framebuffer capture each involve a copy.

A temporary guard protects initial extension startup and an active GPU viewport. If the GUI exits unexpectedly during either, its next launch runs the factory UI without the adapter. GPU resources and geometry are released when neither the live thumbnail nor the expanded preview is active. The guard clears after that normal return, or on reboot.

## WebRTC

The fb-mpp process reads /dev/fb0 and hardware-encodes H.264 through MPP. The stream-screen-webrtc process carries video and touch through WebRTC. Encoding stops without viewers. The authenticated /screen/webrtc endpoint handles signalling, and /screen/ provides interaction.

The snapshot endpoint remains for thumbnail clients. Interactive streaming does not poll PNG screenshots. Direct ICE connectivity is supported; no TURN relay is bundled.

## Build and verification

The existing firmware workflow builds feature branches and the combined stock-ui branch. GUI builds check the parser and status handling with AddressSanitizer and UndefinedBehaviorSanitizer, and upload a checksummed artifact. The workflow creates neither releases nor pull requests.

Development checks on 2026-10-31 established:

- The Chinese factory material configuration page and a native G-code preview run in the original GUI.
- SHOULDER_PETG_21m46s.gcode produces 70,370 vertices and 157 extrusion layers. GUI RSS is about 73 MB with the preview open.
- Mali-G52 renders the viewport, and the factory refresh timer reads 22 ms.
- A browser rotation test observed approximately 32 changed frames per second. The sustained WebRTC stream returned to 45 fps, decoding 1,609 frames with zero dropped frames.
- Returning from the preview restores the factory material configuration page.
- The revised renderer keeps the current layer opaque and both earlier and later layers translucent, with intermediate edge pixels from supersampling. On the same 70,370-vertex sample, 40 warm rotating renders took a median 33.81 ms and p95 36.24 ms. Stream frame rate is distinct from the rate of newly rendered views.
- Six drag sequences across the canvas, toolbar, slider and screen boundaries left the expanded preview at [0, 0, 479, 319] on the top layer.

These are development checks. The deployment record must separately identify the final image, active slot, reboot acceptance and preserved printer configuration.

Live-follow development acceptance used the real virtual SD interface and original printing screen. A controlled fixture executed only G4 waits and M25 pauses; M26 skipped every movement and extrusion line. It verified a partial layer becoming complete, automatic layer changes, pause, manual browsing and return to follow mode, job completion and WebRTC interaction. Positions, tool switch counters and heater targets remained unchanged. This establishes the live data and rendering path, not physical extrusion synchronization or long-print stability.
