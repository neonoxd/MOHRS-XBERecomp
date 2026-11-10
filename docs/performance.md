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

## Immediate-mode vertices and per-draw headers, 2026-11-08

A quick save on the 1_11 boat turret dropped below 200 FPS at 2560x1440 with a 50% render scale. The title's thread
was the limit (busy 100%, the render thread 66%). A sampling profile of a release build with line tables showed a
particle system (`0xB1110`, the spray) taking 41% of it: each quad's four vertices go through the library's vertex
data calls, `D3DDevice_SetVertexData4f`, `2f` and `Color` (default.xbe `0x1C2210`, `0x1C21D0`, `0x1C2270`), which push
a method and its values that `Walk` then parsed back one value at a time, copying all 64 attribute floats per vertex,
and `EndPrimitive` repacked them with a `vector::insert` per slot and vertex.

| Change | Where |
|---|---|
| The three vertex data calls are host functions: the attribute goes straight to its immediate-mode slot | `d3d8.cpp` |
| They take the device lock only to read commands still pending: per call it was a fifth of the thread | `WalkPending` |
| `EndPrimitive` copies the written slots with one 16-byte copy each | `d3d8.cpp` |
| Each of a draw's 26 bindings keeps its last header copy, compared before any lookup, and its last use | `render_thread.cpp` |

The game binds about 11,000 distinct buffer headers in a few frames, so the 1,024-slot caches in front of the header
and use maps missed about five times a draw; most of those bindings repeat from the previous draw, which the
per-binding copies now catch. The symbols are named in both configs (co-op `0x1BB610`, `0x1BB5D0`, `0x1BB670`).

Same save, 2560x1440 window at 50%, FoV 85, uncapped, `XBE_PERF=1`, mean of the one-second reports after loading,
the same draws (about 2,140 a frame) and the same pictures at 10, 17 and 27 seconds:

| Scene | Before | After |
|---|---:|---:|
| 1_11 boat turret quick save | 180-191 FPS (3 runs) | 232-233 FPS (2 runs) |
| 1_3 opening, 2560x1440 at 100%, FoV 90 | 557 FPS | 612 FPS |

Not done: `SendState` still compares about 2.5 KB of untracked state per draw (19% of the thread), and the game's
own code is most of the rest. Profile-guided optimisation of the game code would need a profile runtime in the
freestanding game DLL, which a release compiles on the player's PC.

## Recording without lookups, compact immediate mode, 2026-11-08 (second save)

A second save on the 1_11 turret, timed with `-NoPerf` (per-draw clocks off), mean of the one-second reports after
loading, alternating builds: **about 241 FPS before, about 311 after** (+29%), with the same draws and scenes.

| Change | Where | Why |
|---|---|---|
| Header copies made per binding without a map: a binding keeps its copy while the header's bytes stay the same | `render_thread.cpp` | ~11,000 distinct headers in a few frames: every map lookup was a cache miss |
| Resources read per batch kept as a list per batch in flight, searched only for the title's rare busy questions | `render_thread.cpp` | the last-use map was updated on every binding of every draw |
| Immediate-mode vertices held in their draw layout (only the slots written, ascending), drawn from that buffer | `d3d8.cpp` | particle vertices copied 64 floats each and were packed again |
| The game's vertex helper (`0x17E4C0`, co-op `0x1776C0`) writes the slots directly | `src/*/hooks.cpp`, `d3d::ImmediateData` | three library calls per vertex |
| Device references counted inline; a resource bound again where it already is skips the pair | `d3d8.cpp` | each was a call into the guest's InternalAddRef / InternalRelease |

Later in the same round: the device lock is an atomic flag its owner takes again by counting (`d3d::DeviceLock`; `std::recursive_mutex` was 7% of the title's thread), and the render thread remembers its last target lookups, pipeline, sampler and view table, and skips setting the same pipeline, tables, viewport, scissor, blend factor, stencil reference and topology on the command list again (89% to 82% busy). The two together measured about +3% (288 against 280 FPS over the same early seconds), as the title's thread still sets the pace: `SendState`'s compares of the untracked state (19%) and the spray's particle function (`0xB1110`, 33%, its vertex emission included) are the largest parts left.

Before those, the game's thread was 100% busy; the render thread then was (89%), in per-draw work: binding stage textures
(`BindStageTexturesAndTargets`), vertex assembly, sampler and pixel key hashes, the pipeline key, the constant
upload and the driver. The next step is reusing those results when consecutive draws leave that state alone.
Tried and dropped: joining consecutive indexed draws (consecutive draws almost always use another vertex buffer),
x86-64-v3 for the game code (no change).

## Tracked state, and where both threads stand, 2026-11-08

The rest of the draw state is tracked too (`rs`, `tss`, `psc`, `scissors`, `transforms`, `material`, `lights`):
`gpu::Tracked` takes elements of any size (one straddling pieces is sent whole), writers mark what they write, the
per-draw copy of the deferred states marks only values that changed (`update`), and `SendState` compares only the
few plain fields left. Quick saves keep their layout: the tracked arrays are written as the plain arrays they were,
under the old size as the layout mark (`kStateLayout`), so earlier files still load.

It measured no faster (294 against 296 FPS), because with the counters on the render thread was busy 2.8 ms of a
3.1 ms frame and the title's thread never waited: the two threads are now about even, and halving one alone hardly
shows. The render thread's per-draw work is vertex assembly and upload (0.55 ms a frame), stage texture binding
(0.62 ms) and the rest of the draw (keys, constants, the driver, about 1.4 ms).

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
