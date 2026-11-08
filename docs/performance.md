# High frame rate performance

Mission 1_3 at 2560x1440 exposed a renderer CPU bottleneck. Most position streams use SHORT3 (three signed
16-bit coordinates) with a six-byte stride. The raw vertex path required a four-byte-aligned stride and rejected
these streams. Rejecting one attribute converted the entire draw to float4 vertices on the CPU and rebuilt its
indices. In the opening boat sequence roughly 98% of 800-1,050 draws per frame took that fallback.

The renderer now pads these streams to a host-compatible stride, preserving their packed attributes and using
the existing vertex shader decoding. A SHORT3 position becomes an eight-byte row, with the shader supplying w=1.
Other aligned streams remain direct copies. Native strips and indexed draws keep their original indices;
sparse draws still gather in draw order. Padding reads only the original vertex bytes, including at the end
of a guest allocation. Unsupported formats or element offsets keep the converted path.

The host fetch must meet Direct3D's [element alignment rules](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#4.4.6%20Element%20Alignment).
Padding also provides the fourth component read by the host's four-component format, without reading past the
guest's last three-component vertex.

Performance diagnostics now report in release builds when `XBE_PERF=1`. They include per-second percentiles
of render-thread Present intervals and the number above 4.167 ms. With diagnostics off, per-draw clock reads
and atomic recording/wait accounting are skipped.

## Measured comparison, 2026-10-26

Same PC (RTX 4090), release build, mission 1_3, 2560x1440 window, 100% render scale, FoV 90, widescreen,
vsync off, unlimited cap. No injected input; the opening boat movement supplies the scene changes.
Each run lasts 35 seconds. Results below use the 29 one-second reports timestamped 6-34.5 seconds,
excluding startup/loading and initial shader compilation.

| Metric | Before | Packed vertex path |
|---|---:|---:|
| Mean of one-second FPS reports | 290.2 | 450.4 |
| Range of one-second FPS reports | 259.0-326.4 | 312.9-485.0 |
| CPU vertex assembly per frame | 1.49 ms | 0.30 ms |
| Present intervals above 4.167 ms | 127 / 8,427 (1.51%) | 11 / 13,078 (0.08%) |
| Range of per-second p99 intervals | 3.43-6.68 ms | 2.29-3.84 ms |

That is approximately 55% more uncapped throughput with diagnostics enabled in both runs. A separate final
run with `fps_cap=240` reported 239.1-240.1 FPS after startup. Individual intervals still fluctuate around the
cap; counting all intervals above exactly 4.167 ms in a capped run measures pacing jitter as well as slow work.

The 720p baseline ran at almost the same rate as 1440p, with similar assembly time, supporting the CPU diagnosis.
These are repeatable opening-scene probes, not a full mission playthrough or a guarantee of 240 FPS in every scene.
The timer audit for both images and packed-vertex tests pass; interactive visual verification remains useful.
The co-op executable also passed a 12-second 1440p/240 FPS mission 1_3 boot and rendering smoke check with no
logged errors; this did not exercise a two-controller session.

Local raw logs: `re/baseline_1440_fov90.txt`, `re/packed_1440_fov90.txt`, `re/final_240_1440.txt`.
The full per-run logs and binaries are retained under `re/fps-probes/`.

## Per-draw overhead on both threads, 2026-11-04

At 1440p in the same 1_3 opening, after the packed-vertex change, both threads were saturated: the render thread was
busy for 2.0 of each 2.3 ms frame and the title thread waited only about 0.1 ms for it. The GPU was not the
limit. A sampling profile of a release build with line tables showed host overhead per draw, not game code:

| Cost per draw, render thread | Share | Change |
|---|---:|---|
| Four `CreateShaderResourceView` calls into the driver | 24% | Tables reused within a frame, keyed by resources and views |
| Hashing: a 1 KB palette copy and hash per P8 stage, keys, texture checks | 15% | Palettes compared with a cached copy; four-lane `FastHash` for in-process keys |
| A locked map lookup of the render target's lock epoch | 4% | Skipped while no surface or texture was locked |
| An atomic exchange in `app::Widescreen` per HUD call | 3% | Plain load first |

On the title thread, `Threaded::SendState` compared about 13 KB of state against the copy last sent, on every draw
(24%). The two largest arrays, the 8 KB of simple render states and the 3 KB of vertex shader constants, are now
`gpu::Tracked`: writes go through `set()`/`edit()`, which mark 64-byte pieces, and only marked pieces are compared
and sent. Writing one without marking it does not compile. The header lookup caches in front of their maps grew from 64
to 1,024 hashed slots.

`gpu::Hash` is unchanged because texture pack names depend on it. `FastHash` values are only used within one run.

Same scene and the same 957 draws per frame, mean of the one-second reports at 6-34.5 seconds, `XBE_PERF=1`:

| Metric | Before (3 runs) | After |
|---|---:|---:|
| FPS | 436-450 | 552 |
| Render thread draw time per frame | 1.94-2.00 ms | 1.32 ms |
| Render thread idle per frame | 0.10 ms | 0.33 ms |
| Title thread recording per frame | 0.64-0.68 ms | 0.43 ms |

The title thread is now the limit. Its largest remaining host costs are `SendState` (17%), mostly the ranges still
compared in full, and `Threaded::Header` (9%), the pointer chase to header copies in a map. Immediate-mode `End`, which
replays the pushed vertices, accounts for 11%. The rest is recompiled game code. A 60 FPS 720p comparison of both
builds 22 seconds into 1_3 showed the same scene, HUD and textures, and neither log has errors. `tests/state_tracked_test.cpp` checks the marking:

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -O2 -Isdk/runtime tests/state_tracked_test.cpp -o re/state_tracked_test.exe"
& re/state_tracked_test.exe
```

`tools/fps_probe.ps1 -Build profile` runs another build folder, such as a release configuration with
`-gline-tables-only` for profiling. `-Visible` shows the window for `tools/screenshot.ps1`.

## Reproduce

Ask before launching probes while the user may be using the PC. The script creates isolated settings and saves,
never sends OS input, and stops only its own game processes.

```powershell
powershell -NoProfile -File tools/fps_probe.ps1 -Cap 0 -Unlocked -Level 1_3 -Width 2560 -Height 1440 -Fov 90 -Seconds 35
powershell -NoProfile -File tools/fps_probe.ps1 -Cap 240 -Unlocked -Level 1_3 -Width 2560 -Height 1440 -Fov 90 -Seconds 35
```

Use `-NoPerf` for a normal-play timing run without the detailed renderer instrumentation, `-RenderPercent` to
vary the render scale, or `-Coop` to exercise the other executable.

The focused test covers signed SHORT3 positions, independently packed three-byte colours, aligned UV streams,
nonindexed strips, indexed base vertices, quads, sparse indices and a last vertex against a protected memory page:

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -O2 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/vertex_packing_test.cpp sdk/runtime/gpu/vertex.cpp -o re/vertex_packing_test.exe"
& re/vertex_packing_test.exe
python tools/timing_audit.py
```
