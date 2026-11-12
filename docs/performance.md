# Renderer performance

At high frame rates the limit is CPU work per draw on the title thread (recording) and the render thread
(translation to D3D12), not the GPU or game code. Scenes draw about 950-2,150 times a frame.

## Changes

| Change | Where | Why |
|---|---|---|
| SHORT3 positions (three signed 16-bit values, six-byte stride) padded to an eight-byte row, shader supplies w=1; other aligned streams copied directly | `sdk/runtime/gpu/vertex.cpp` | D3D [element alignment](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#4.4.6%20Element%20Alignment) rejected six-byte strides, and one rejected attribute converted the whole draw to float4 on the CPU with rebuilt indices (98% of draws in 1_3) |
| Descriptor tables reused within a frame, keyed by resources and views | render thread | four `CreateShaderResourceView` calls per draw (24%) |
| P8 palettes compared with a cached copy; four-lane `FastHash` for in-process keys | render thread | a 1 KB copy and hash per stage (15%) |
| Render target lock-epoch lookup skipped while nothing is locked | render thread | locked map lookup per draw (4%) |
| `app::Widescreen` does a plain load before its atomic exchange | `src/common/widescreen.cpp` | per HUD call (3%) |
| Draw state in `gpu::Tracked` arrays: writes go through `set()`/`edit()`, which mark 64-byte pieces; only marked pieces are compared and sent | `gpu/state.h`, `Threaded::SendState` | 13 KB compared per draw (24% of the title thread) |
| `SetVertexData4f`/`2f`/`Color` (`0x1C2210`, `0x1C21D0`, `0x1C2270`; co-op `0x1BB610`, `0x1BB5D0`, `0x1BB670`) are host functions writing straight to the immediate-mode slot, taking the device lock only to read pending commands | `d3d8.cpp`, `WalkPending` | pushed methods were parsed back one value at a time, copying 64 floats per vertex |
| Immediate-mode vertices kept in draw layout (written slots only, ascending) and drawn from that buffer | `d3d8.cpp` | `EndPrimitive` repacked with a `vector::insert` per slot and vertex |
| The game's vertex helper (`0x17E4C0`, co-op `0x1776C0`) writes the slots directly | `src/*/hooks.cpp`, `d3d::ImmediateData` | three library calls per vertex (the spray particles, `0xB1110`) |
| Each binding keeps its own header copy while the header's bytes are unchanged, no map | `render_thread.cpp` | ~11,000 distinct headers in a few frames made every map lookup a miss |
| Resources read per batch kept as a list per batch in flight | `render_thread.cpp` | the last-use map was updated on every binding |
| Device references counted inline; rebinding the same resource skips the pair | `d3d8.cpp` | each was a call into the guest's `InternalAddRef`/`InternalRelease` |
| Device lock is an atomic flag its owner re-takes by counting (`d3d::DeviceLock`) | `d3d8.cpp` | `std::recursive_mutex` was 7% of the title thread |
| Render thread remembers its last target lookups, pipeline, sampler and view table, and skips re-setting unchanged pipeline, tables, viewport, scissor, blend factor, stencil reference and topology | `render_thread.cpp` | redundant command list calls |

`gpu::Hash` is unchanged because texture pack names depend on it; `FastHash` values never leave a run. Quick saves
write the tracked arrays as the plain arrays they were, under the old size as layout mark (`kStateLayout`), so older
files still load. With `XBE_PERF` off, per-draw clock reads and atomic accounting are skipped.

Tried without gain: joining consecutive indexed draws (they almost always switch vertex buffer), x86-64-v3 for the
game code.

## Shader cache

Shaders are HLSL made from the draw's state (vertex programs, fixed function, pixel combiners) and compiled with
`D3DCompile` on the render thread the first time a draw needs them. One compile takes 17-96 ms, the longest for
vertex programs of about 10 KB of HLSL (a grenade explosion's two took 94 and 96 ms, a 195 ms frame). The title
waits for the render thread meanwhile.

`gpu::ShaderCache` (`sdk/runtime/gpu/shader_cache.cpp`) keeps each compiled shader in
`shadercache\<tag>\<key>.dxbc` beside the executable. The key is `Hash` of the profile, the entry point and the
source, the same key the in-memory map uses. The tag is `Hash` of the cache layout, the compile flags and the
compiler DLL's link time and image size; opening the cache removes the other tags' folders. A file that is not a
whole DXBC container (its magic and the size in its header) is compiled again. Files are written under a name of
their own and renamed, so the two executables can share the folder. A shader costs its compile the first time it is
ever drawn, a file read after that. `XBE_PERF` reports compiled and cached shaders per second; `XBE_HITCH` lists
cache reads as shader events "from the cache".

Vertex programs are also compiled before their first draw. `D3DDevice_CreateVertexShader` (and a quick load's
`ReadShader`) calls `gpu::PrecompileVertexProgram`, which predicts the draw's input registers and decode from the
declaration alone (`gpu::PredictInputs`, `vertex.cpp`: every declared input bound, the declaration's stream
strides, the same raw-readable test as `Assemble`), writes the program's HLSL and queues it. One to three threads
(a quarter of the hardware threads), below normal priority, take the shader from the disk cache or compile and
store it. `Compile` asks the queue first: a finished shader is used, one being compiled is waited for, one not
started is dropped from the queue and compiled by the draw. A draw whose inputs differ from the prediction, or
that draws the program as a quad (`QuadHlsl`), has another key and compiles as before; the first 32 are logged as
`shader precompile: program ... drawn with inputs ...`. State programs (type `0x7378`) are not precompiled.
Fixed function vertex shaders and pixel shaders depend on draw state and are compiled at the draw.

Pipeline states (`CreateGraphicsPipelineState`) took 2 ms at most and are not cached.

## Measurements

RTX 4090, release build, uncapped, vsync off, no input, mean of the one-second reports after loading:

| Scene | Start | Now |
|---|---:|---:|
| 1_3 opening, 2560x1440, 100% render scale, FoV 90 | 290 FPS | 612 FPS |
| 1_11 boat turret quick save, 2560x1440 at 50%, FoV 85 | 180-191 FPS | about 311 FPS (`-NoPerf`) |

With `fps_cap=240` the 1_3 opening holds 239-240 FPS. These are short scene probes, not a guarantee across the
campaign.

## Where it stands

The two threads are about even: the render thread is busy about 2.8 ms of a 3.1 ms frame and the title thread
never waits, so speeding up one alone hardly shows. Render thread: vertex assembly and upload (0.55 ms a frame),
stage texture binding (`BindStageTexturesAndTargets`, 0.62 ms), keys, constants and the driver (about 1.4 ms). Title
thread: the spray particle function (`0xB1110`, 33% with its vertex emission) and the rest of the game code.
Profile-guided optimisation of the game code would need a profile runtime in the freestanding game DLL, which a
release compiles on the player's PC.

## Reproduce

```powershell
powershell -NoProfile -File tools/fps_probe.ps1 -Cap 0 -Unlocked -Level 1_3 -Width 2560 -Height 1440 -Fov 90 -Seconds 35
powershell -NoProfile -File tools/fps_probe.ps1 -Cap 240 -Unlocked -Level 1_3 -Width 2560 -Height 1440 -Fov 90 -Seconds 35
```

`XBE_PERF=1` (on by default in probes, off with `-NoPerf`) logs per-second Present interval percentiles, the count
above 4.167 ms, and per-thread busy, wait and recording times. `-Build profile` runs a build folder configured with
`-gline-tables-only` for a sampling profiler; `-RenderPercent`, `-Coop` and `-Visible` (for `tools/screenshot.ps1`)
are also available. See [debugging.md](debugging.md).

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -O2 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/vertex_packing_test.cpp sdk/runtime/gpu/vertex.cpp -o re/vertex_packing_test.exe"
& re/vertex_packing_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -O2 -Isdk/runtime tests/state_tracked_test.cpp -o re/state_tracked_test.exe"
& re/state_tracked_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/shader_cache_test.cpp sdk/runtime/gpu/shader_cache.cpp sdk/runtime/core/log.cpp -ld3dcompiler -o re/shader_cache_test.exe"
& re/shader_cache_test.exe
```

The vertex packing test covers signed SHORT3 positions, independently packed three-byte colours, aligned UV
streams, nonindexed strips, indexed base vertices, quads, sparse indices and a last vertex against a protected
page. The tracked-state test checks the marking.
