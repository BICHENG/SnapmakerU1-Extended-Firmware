# Native UI drawing diagnostics

These tools investigate the original Snapmaker 2.1.0.157 renderer. They are excluded from firmware packaging. No replacement UI or widget GPU backend is installed. The accepted runtime remains the `gpu` branch.

The original executable must be 6,563,736 bytes with SHA-256 `2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472`. Both the supervisor and probe check identity. The probe replaces `main` before executing only the required LVGL functions. Never preload a probe into the production GUI service.

| Tool | Purpose | Verified boundary |
| --- | --- | --- |
| `capture` | Observe one original render cycle, task descriptors and initial/final buffers | Original evaluation/dispatch preserved; observer removed after completion or timeout |
| `resources` | Copy decoded image/glyph pixels and replay native callbacks without source pointers | Eight isolated cases; incomplete capture still completes original drawing |
| `compose` | Compare native CPU drawing with an ordered GLES batch | Fifteen synthetic default-font scenes, including exact integer alpha through framebuffer fetch |

The capture remains metadata-only. Its raw descriptor pointers are not portable assets. The resource module is not connected to the live capture or native queue: it requires an idle software worker and an explicitly owned task. A complete page and its interactions cannot be reconstructed from these probes alone.

## Results and decision

See [measured results](results.json). The current real homepage has 31 tasks: seven fills, twelve labels and twelve images, all in the root layer. With the bounded repaint test corrected, two complete five-second windows drew 45.0 and 45.2 frames/s; CPU-path refresh p95 was 6.81 and 6.85 ms. This includes layout, synchronization, rasterization and delivery, but not panel scanout.

The optimized GPU compositor matched all pixels in the fifteen tested scenes. Its 204-glyph scene took 2.37 ms versus 1.37 ms for native CPU drawing plus delivery. The warm GPU atlas excludes upload/layout preparation, while the native CPU measurement includes label layout. Even with that advantage, the candidate was slower. It is not promoted into `gpu`, `gui` or `stock-ui`.

One current homepage and synthetic scenes do not establish all-page or all-font coverage. Ordinary labels/buttons remain on CPU. GPU acceleration remains useful for the measured G-code workloads. Full automatic reconstruction of navigation and printer behavior has not been demonstrated.

## Build and host checks

Use Linux/WSL with a C compiler, Python 3, the AArch64 GNU toolchain and EGL/GLES headers:

```sh
make test all check-compose
make compose MALI_LIB=/path/to/matching/aarch64/libmali.so
```

`CPPFLAGS` can select an SDK include directory. Vendor binaries, fonts, captured pixels and device credentials are not included. Host checks run ASan/UBSan; they do not substitute for the U1 pixel comparisons.

## Isolated device checks

Run one probe at a time while the original UI is idle and all heater targets are zero. The device-side supervisors create private mount/network namespaces, hide framebuffer/input/control nodes, drop to UID 65534 and verify that the production GUI PID/start time is unchanged. Only the compositor receives Mali/render/DMA nodes; it also records memory samples and the kernel-log interval. No touches or printer commands are issued.

For the resource probe, stage `libnative-resources-probe.so` in a new `/userdata/ui-validation/native-resources-<unique-id>` directory. Create `hashes.json` as an object mapping that filename to its SHA-256. Run the device supervisor with that directory as its argument:

```sh
python3 isolate_probe.py /userdata/ui-validation/native-resources-<unique-id>
```

Use `resources/isolate_probe.py` for this command. It writes `result.json`, `stdout.txt` and `stderr.txt` in that directory.

For the compositor, stage `libnative-compose-probe.so` in a new directory under `/userdata/ui-validation`. Create `source.json` containing `{"binary":{"name":"libnative-compose-probe.so","sha256":"<actual hash>"}}`. Use `compose/isolate_probe.py`:

```sh
python3 isolate_probe.py /userdata/ui-validation/native-compose-<unique-id> 20
```

Results are under `compose/` in that directory. `fetch_all_pixels_exact` must be true. Standard GL blending is deliberately measured as a non-equivalent comparison. Timings include completion, cache synchronization and delivery; atlas preparation is reported separately.

## One-frame live observation

The experimental branch's existing `stock-display` main-loop adapter can resolve `stock_draw_tick`. It calls this optional observer after releasing the LVGL lock. The diagnostic library is never installed or preloaded by the firmware build.

An operator-owned temporary session can load `libstock-draw-capture.so` with that adapter. It must preserve other preloads, retain an automatic restoration path and restore the published libraries afterwards. The capture verifies the native unit callbacks, 480×320 DIRECT double buffering, XRGB format and 1,920-byte stride before observing anything.

Create `/tmp/stock-draw-capture.request` with exactly `1\n`. `/tmp/stock-draw-capture` must not already exist. The next natural render is captured, or the next main-loop tick after three seconds stops the request. A full-screen repaint is required for a whole-current-page task count. This observer sends no input and requests no repaint itself.

The output contains `report.json`, `tasks.jsonl`, `descriptors.bin`, `initial.bgra` and `final.bgra`. Validate it with `python3 capture/check_capture.py <output-directory>`. Copies and observation affect the captured interval, so it is excluded from performance windows. XRGB's fourth byte is unused. Fonts, image sources and child-layer pointers in raw descriptors must never be dereferenced by another process.
