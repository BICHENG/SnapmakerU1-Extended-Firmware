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
- `segmented.resolve_readback_ms`: GPU downsampling plus synchronous readback.
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

## U1 measurement, 2026-10-31

Vendor 2.1.0.157, Mali capped at 400 MHz, 70,370 vertices / 157 layers,
layer 153, 480x320 output with 960x640 supersampling, five warmups and 40
rotating frames per pass. The printer was idle with all heater targets at zero.
The original preview was visible but static; GPU runtime status was suspended
before each run. No remote refresh test or second benchmark ran concurrently.

| Implementation | Render median | Render + two copies median | Total p95 |
| --- | ---: | ---: | ---: |
| Two supersampled color buffers | 40.06 ms | 40.90 ms | 45.29 ms |
| One supersampled color buffer | 39.10 ms | 39.79 ms | 45.12 ms |

This one-pair comparison does not establish a repeatable speed improvement.
The accepted change removes one 2,457,600-byte RGBA8 buffer, its texture sampling
pass and an FBO transition. It keeps the depth buffer, 2x supersampling, nearest
context compositing at 24% opacity and opaque current layer. Three captured
2D/3D views differed by at most one value per 8-bit channel; the synthetic
opacity and antialias checks passed.

The candidate's segmented medians were 26.74 ms for draw completion, 11.24 ms
for resolve/readback, 0.48 ms for CPU preparation and 0.80 ms for delivery
copies. Ordinary CPU copies are not the main measured cost. The full frame
still exceeds the 22.2 ms budget of the observed 45 Hz panel. Do not claim
45 newly rendered frames per second, 60/120 Hz support, or full LVGL GPU
acceleration from this result.
