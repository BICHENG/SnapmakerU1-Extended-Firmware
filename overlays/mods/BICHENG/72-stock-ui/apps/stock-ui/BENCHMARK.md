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
