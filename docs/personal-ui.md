# Personal U1 display build

This branch retains vendor 2.0.0.205, Fan Curves, Balanced-only reduced motor current and the ALSA network microphone. The fork's `develop` branch tracks upstream independently. No upstream pull request is part of this deployment.

## Display and G-code

Fluidd 1.37.7 downloads G-code into a browser worker, parses moves/layers/tools and draws the selected paths as SVG. Its progress follows Moonraker's `virtual_sdcard.file_position`. That view does not render on the physical LCD.

The installed stock `/usr/bin/gui` is an LVGL application using the software renderer and `/dev/fb0`. Its model image is a slicer thumbnail. Binary inspection establishes its rendering backend; the stock application's full source is not available here.

The personal build uses HelixScreen 1.0.7 (`faed4e35010e970ba5612e9b2e109c52a11965a6`) for the system touchscreen. Its G-code viewer uses an offscreen OpenGL ES framebuffer and reads the rendered image into LVGL. Ordinary widgets retain LVGL software drawing. The LCD backend remains fbdev; this is not a claim that the whole UI has become GPU rendered or that the path is zero copy.

The panel is 480 × 320, with a measured/configured 45 Hz timing (7,840,125 / 505 / 345). The build uses a 22 ms LVGL refresh period. This sets a refresh ceiling, not a promise that every complex screen renders at 45 fps.

## GPU

The original device tree disables the Mali-G52. The personal overlay enables that node, connects its existing `vdd_gpu` regulator, builds the matching `bifrost_kbase` module and installs the pinned Mali g24p0 userspace library with its licence. No clock/voltage table is overclocked. The init script caps the GPU at 400 MHz; runtime power management can suspend it when unused.

The library is pinned to `tsukumijima/libmali-rockchip` commit `ec78b74adc038f97cf173f4061fe3856efc0dc74`, SHA-256 `bc6be2fd5d8b46bb9d4cb22118a3ff77af1ca46cee1ac12bc78e27f89ba3ea3d`.

GPU startup is opt-in through `[components] mali_gpu = true` in the existing extended configuration. Without it, the release UI remains available with its 2D G-code renderer. Leave this setting off until the individual printer passes runtime and reboot validation. A successful first rendered frame alone is insufficient: during this development session, network access disappeared after the temporary GPU test, and the cause had not yet been established.

## Remote screen

`fb-mpp` maps the framebuffer, copies pixels into a reusable MPP input buffer and encodes H.264. `stream-screen-webrtc` sends it through the existing libdatachannel implementation. Encoding stops when there are no viewers. The authenticated `/screen/webrtc` endpoint handles signalling. Browser pointer events travel through the WebRTC data channel and are written to the touchscreen input device; disconnect/timeout releases a held touch.

The single-image snapshot endpoint remains for clients that need a thumbnail. The interactive screen no longer polls PNG screenshots. WebRTC uses direct ICE connectivity; no TURN relay is bundled.

## Build and validation

The `Personal UI firmware` workflow builds the pinned GPU-enabled screen first, verifies its artifact checksum, then builds the firmware. It uploads build artifacts and does not create releases or pull requests. GPU/UI configuration is kept in the personal `BICHENG` profile.

On-device development checks on 2026-10-31 established Mali-G52 OpenGL ES 3.2 with correct pixel readback, normal GPU runtime suspension, a functioning Chinese Helix home screen with all four tools, and browser WebRTC video/touch. A 20-second 45 fps stream decoded 909 frames with zero dropped frames or lost packets, but some short freezes occurred over Wi-Fi. These checks alone do not constitute firmware reboot acceptance; the deployment record must separately identify the flashed image and reboot results.
