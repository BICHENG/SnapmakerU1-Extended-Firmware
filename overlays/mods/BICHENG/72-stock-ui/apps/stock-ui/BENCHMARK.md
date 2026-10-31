# Offscreen GPU benchmark

`make bench-gpu` builds a standalone diagnostic executable. It is not installed
by the firmware build. The normal library does not compile the profiling fields,
clock calls or extra `glFinish` calls. The `gpu_render` ABI stays unchanged, so
the existing ctypes quality checks can still load the normal library.

Run only while the printer is idle, heater targets are zero, and no other GPU
benchmark or live G-code viewport is active. The executable reads one G-code
file and renders offscreen. It does not send printer commands, inject touch,
change the original GUI, or write the framebuffer. It still consumes GPU and CPU
time. It does not implement a printer-status guard; the caller must check first.

```sh
./bench-gpu /userdata/gcodes/SHOULDER_PETG_21m46s.gcode 153 40 > render-profile.json
UI_GPU_OUTPUT=readback ./bench-gpu /userdata/gcodes/SHOULDER_PETG_21m46s.gcode 153 40 > readback-profile.json
```

The layer argument is one-based and defaults to the last layer. Each pass has
five warmup frames followed by the requested number of measured frames, capped
at 240. A synthetic three-layer scene checks current-layer opacity, translucent
context and antialias coverage before measuring. The baseline and segmented
passes must finish with byte-identical pixels.

The JSON reports median and p95 wall-clock milliseconds:

- `baseline.render_ms`: the unchanged render path, without added GPU waits.
- `segmented.draw_finish_ms`: draw submission plus GPU completion, measured
  with `glFinish` at the boundary before resolve.
- `segmented.resolve_readback_ms`: GPU downsampling and output synchronization,
  or synchronous readback in the fallback mode.
- `segmented.cpu_prepare_ms`: vertical flip/copy and CPU slider drawing.
- `delivery_copy_ms`: two full-size CPU copies matching the shared-frame and
  LVGL-image buffer sizes. This is an isolated copy benchmark, not a measurement
  inside the original GUI.
- `total_ms`: render plus those two copies in that pass.

Extra synchronization can change GPU scheduling and tile flush costs. Use the
baseline to compare end-to-end performance; use the segmented pass to identify
where time is spent. Do not treat its timings as GPU hardware timestamps or add
its medians to predict baseline latency. LVGL composition, framebuffer capture,
MPP encoding, display scanout and WebRTC transport are outside this benchmark.

## Shared-output measurement, 2026-10-31

Vendor 2.1.0.157, Mali capped at 400 MHz, 70,370 vertices / 157 layers,
layer 153, 480x320 output with 960x640 supersampling, five warmups and 40
rotating frames per pass. The printer was idle with all heater targets at zero.
GPU runtime status was suspended before each run. No remote refresh test or
second GPU benchmark ran concurrently. Both modes used the same executable,
alternating readback, shared output, shared output, readback.

| Output mode | Render + two copies median | Total p95 | Resolve/output median |
| --- | ---: | ---: | ---: |
| Readback A | 39.23 ms | 46.15 ms | 10.90 ms |
| Shared A | 28.59 ms | 36.32 ms | 1.16 ms |
| Shared B | 28.80 ms | 35.52 ms | 1.15 ms |
| Readback B | 39.39 ms | 46.65 ms | 10.52 ms |

For this workload, shared output reduces the measured frame path by about 27%.
Six full and compact 2D/3D views were byte-identical to the accepted renderer.
The synthetic opacity and antialias checks passed, including forced DMA-heap
failure with automatic readback fallback. The common module also passed a
100-frame GLES 2 pixel comparison, odd-stride and resource-lifetime test.

The retained renderer uses one supersampled color buffer, a depth buffer,
2x supersampling, context compositing at 24% opacity and an opaque current
layer. It resolves into a shared linear output texture, then synchronizes
CPU access and copies into the existing image buffers. This is not end-to-end
zero-copy. Complex sample frames still exceed the panel's 22.2 ms budget;
the result does not establish 45 new G-code views per second, 60/120 Hz support,
whole-UI GPU rasterization or improved print performance.
