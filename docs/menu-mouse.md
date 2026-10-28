# Mouse control in menus

Implemented 2026-10-27 after investigating the local XBEs and source at `6b2107b` (v0.1.2).
Both executables build and automated host tests pass. Isolated game probes verify the live render callback and
pointer-driven pause heading and menu-entry transitions; manual testing is ongoing.

## Controls and implementation

- Hover focuses an entry; left click selects it. A stationary pointer lets keyboard/controller navigation retain
  focus. The wheel moves through ordinary menu entries.
- Click a settings label or displayed value to focus its row, including rows whose arrows are currently hidden.
  This works in PC Options and the stock Audio, HUD and controller options. Click the arrows or use the wheel to
  change the selected value. Click the displayed
  select/confirm hint to save, or the back hint to cancel. Right click also goes back.
- Pause headings are clickable, including the multiplayer leaderboard/preferences/exit headings. Right click on
  a pause page resumes; the displayed Start/resume prompt is also clickable. Escape still pauses/resumes.
  Clicking a heading requests that page directly, without opening the intervening pages.
- Mouse navigation follows KBM's assigned player, including when KBM joins as player 2 in co-op. It does not take
  over menus owned exclusively by another controller.
- Menus draw the root `cursor.png` as a software pointer in both executables. The PNG is embedded at build time,
  so release and isolated probe copies need no loose image. It scales with the displayed picture (24 pixels at
  720p), with its arrow tip at the hit-test position. The Windows pointer is hidden over the client area; the
  software pointer requires freshly drawn interactive menu controls, keeping it hidden on loading screens and
  cutscenes, during gameplay, while unfocused, and outside the game window.
- Click the bottom mission name on the mission map to confirm the current selection and open mission details.
  The details screen's top title has no click action. Single-player and co-op maps use the native selection and
  respect disabled missions.

`src/common/menu_mouse.cpp` records actual rendered font bounds and sprite quads, including localised text,
justification, parent transforms, animation and safe-area positioning. It inverts the host picture's bars and
widescreen compression for hit testing. It uses the game's existing focus setter and object/package/handler
messages. Backdrop confirm/back hints act on the current package. A held click cannot activate the next package
or become fire/aim on resuming. Guest reads and calls stay on the UI's guest thread.

## Validation and user testing

The pause adapter uses the input manager's player-to-port table (`0x271188` / co-op `0x266618`) to find KBM's
guest port 0 and derive its FEng player mask. Package selection, drawing geometry, queued messages and deferred
pause-tab actions all use that mask. This fixes the earlier player-1-only assumption: with a controller as
player 1 and KBM as player 2, mouse navigation now works in KBM's own pause menu. Reassignment also settles held
clicks. The host adapter tests run the complete navigation suite with KBM assigned to each of players 1-4.

Build the executables and game-code DLLs:

```powershell
cmd /c "tools\dev.bat cmake --build out\build\release --target MOHRS MOHRS_code MOHRSCoop MOHRSCoop_code -- -j 12"
```

Host tests (no game launch or player save access):

```powershell
cmd /c "tools\dev.bat clang++ -std=c++20 -Isrc tests/menu_pointer_test.cpp -o re/menu_pointer_test.exe"
& re/menu_pointer_test.exe
cmd /c "tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/menu_mouse_test.cpp src/common/menu_mouse.cpp src/common/feng.cpp -o re/menu_mouse_test.exe"
& re/menu_mouse_test.exe
```

These cover presentation coordinates, widescreen compression, row bounds, button edges, hover focus, object/handler
routing, modal and focus transitions, disabled controls, controller ownership, pause headings with reused package
allocations, settings rows, arrows and wheel changes. The integration test also checks preservation of the guest
update's complete CPU/FP context around helper calls.
Pause pages gain mouse-only package responses cloned from their native left/right transitions, with the
destination filename changed to the requested tab. Their handler and overlay cleanup commands stay intact.
Clicks enqueue the response after the native update has dispatched initialization, preserving event ordering
without waiting for animations. The earlier implementation stepped through intervening pages and waited for
opening scripts; that caused slow navigation and has been removed. Tests cover direct forward/reverse requests
during opening animations, current-tab clicks, held clicks, and discarding a click when the native update changes
its package. Serialized package checks preserve every original keyboard/controller response and validate all
seven single-player/multiplayer pages' direct destinations and cleanup commands. A live probe switches tabs every
300 ms, jumps from Objectives straight to Preferences, and captures the correct contents without Notes underneath.
Row-selection regressions also cover clicking labels and values when the row's arrows are not drawn, stock Audio
labels, and disabled rows. An isolated PC Options probe clicks an unselected Widescreen label through the live
pointer/hit-test path.
An isolated front-end probe confirms that the bottom mission name opens details at unlocked 240 fps; a subsequent
click on the details heading leaves that screen unchanged. The synthetic test also covers a displayed label
differing from the current mission button during transitions and disabled mission selections.

Run `out/build/release/MOHRS.exe` for the interactive check: main menu entries, Options, PC Options including
confirm/cancel, pause headings, Preferences and resume. Check a confirmation dialog and keyboard/controller input
with a stationary pointer. Repeat in co-op and, if convenient, with widescreen off and a different window size.
`tests/software_cursor_test.cpp` also checks the embedded PNG's colour and alpha pixels at the overlay upload,
valid atlas UVs, and coexistence with the FPS font without a game window or GPU.
The initial implementation was tested with host fixtures only. A subsequent isolated game probe reproduced a
missing hit-target bug: ShellManager overrides the base FEng drawing callback. The adapter now hooks both the base
and derived callbacks. The corrected probe records rendered targets and clicks through pause > Preferences > Audio
using internal simulated pointer input without moving or clicking the PC's cursor. Player settings/saves are kept
separate in `re/menu-probes/`.
The front-end probe also confirms rendered hit targets on its mode/profile screens. Its isolated profile flow
stops at name entry, so the full main-menu path still needs the user's manual check. The probe leaves existing
game processes alone.

Set `MOHRS_MENU_MOUSE_TRACE=1` before launching for detailed package, rectangle and click lines in `logs/mohrs.log`.
Normal runs log one activation line without the detailed trace.
For reproducible internal pointer tests, `MOHRS_MENU_MOUSE_PROBE="8:Preferences text,10:Audio button"` schedules
hover/clicks at the named rendered objects, timed from the first FEng input update. Use only in an isolated probe.
`tools/menu_probe.ps1 -CopyProfiles` copies the existing save partition into that probe's own folder to get past
profile creation; it never writes the copied files back to the player's save partition.

## Initial investigation (before implementation)

The following records the original static investigation and the evidence used for the adapter.

## Finding

Mouse control looks feasible through the existing EA FEng system. The Xbox executable retains mouse polling,
hover and button-event processing, but the game's platform callbacks for polling and hit testing are stubs.
The useful work is connecting host pointer input, implementing hit testing, and routing it through the game's
existing focus/message system. Main menu entries and pause Preferences entries already have the necessary
highlight and press responses in their packages.

There is no need to replace the menu renderer. Use runtime package objects so the injected PC Options entry and
screen, localised text, animations and modal screens can participate.

## Project context

- The game is statically recompiled Xbox x86 code running through host XDK/kernel implementations. Shared host
  code is under `src/common`, with separate address hooks in `src/game/hooks.cpp` and `src/coop/hooks.cpp`.
- Both executables have working controller/keyboard navigation, raw mouse look, native PC Options, widescreen
  and frame-rate enhancements. See `README.md`, `docs/pc-options.md` and `docs/frame-timing.md`.
- `src/common/feng.{h,cpp}` parses and writes the menu packages. `options_package.cpp` injects the PC Options
  entry and screen in memory; `options_menu.cpp` dispatches their events to host settings.
- `HANDOVER.md`, referenced by the plan and probe script, is absent in this checkout. No `AGENTS.md` was found
  in the repository or its checked ancestor directories. The working tree was clean before this investigation.

## Ghidra access

Ghidra MCP is accessible. Instance discovery found project `mohrs` at
`C:\dev\vibe\ghprojs\mohrs\mohrs`, TCP port 8089, with both programs open:

- `mohrs-default-ghidra.exe`
- `mohrs-coop-ghidra.exe`

The health check, string/xref searches, memory reads and decompilation succeeded. The active program initially
was co-op, so always pass `program` explicitly. Addresses below are guest virtual addresses, not file offsets.
Descriptions are inferred roles, not recovered original function names. Some interior branch targets appear as
separate functions in Ghidra; check assembly and start decompilation at the actual entry point.

## Existing host input

`src/common/mouse_keyboard.cpp` registers raw mouse input and accumulates relative counts for gameplay.
`PressActions` suppresses LMB/RMB/MMB and wheel gameplay bindings outside play; there is no menu pointer consumer.
`OnRawInput` releases cursor clipping outside play and returns before recording movement for mouse look.
`src/common/game_info.cpp` already shows the Windows arrow outside mouse-controlled gameplay.

`input::Playing()` is a heuristic: the game called `TakeMouse()` in the last 250 ms. It is useful for cursor
behaviour today, but is insufficient as the sole menu interaction gate. A pause transition can remain classified
as play briefly, and a cinematic or loading screen can be classified as non-play. Menu handling should use an
interactive active package, with mouse/key edges settled when ownership or the active package changes.

Capture host pointer state on the window/input side; consume it and make guest calls on the game thread.
Never traverse guest menu objects or call guest handlers from the window procedure.

## Verified shared engine routines

| Role | default.xbe | moh3rdc.xbe | Evidence |
|---|---:|---:|---|
| FEng update | `0x95220` | `0x87B80` | polls controllers; optionally polls mouse; iterates packages; processes message queues |
| Per-package controller handling | `0x94330` | `0x86C90` | package input mask, focused button, press/release/navigation messages |
| Per-package mouse handling | `0x94E50` | `0x877B0` | pointer coordinates, button edges, hit-test callback, hover focus changes |
| Set package focus | `0x96900` | `0x892A0` | queues old/new highlight messages and writes package `+0x80` |
| Queue FEng message | `0x93260` | `0x85BC0` | allocates message, records source/package/target/mask, appends engine queue |
| Button position helper | `0x88590` | `0x7AF10` | reads current object attributes and applies parent transform when present |

The co-op mouse, focus, queue and update routines were decompiled separately to confirm the matching roles.
The position helper's co-op address was found by a unique matching 24-byte prefix.

In default.xbe, the update has callers `0x1262B0` in the shell manager and `0xF68A0` in the pause UI path.
This provides a shared investigation/prototype point for the two requested menu contexts.

### Native mouse path and its missing callbacks

The update calls mouse handling only if engine byte `+3` is set. In that branch it calls interface vtable slot
`+0x48` to fill an eight-byte sample, then updates the engine mouse state through `0x91DB0`.
The sample is four 16-bit values: x, y, a third coordinate/value, and button bits. The third value's intended
meaning still needs verification; it must not be assumed to implement wheel navigation.

The default.xbe platform interface vtable at `0x1ECAD0` contains:

| Slot | Target | Observed behaviour |
|---|---:|---|
| `+0x48` | `0x6AD80` | empty function; does not fill the mouse sample |
| `+0x4C` | `0x186070` | returns zero; used as the object hit-test callback |
| `+0x50` | `0x186070` | another callback using the same false-returning stub |

Consequently, merely enabling engine byte `+3` is unsafe: the polling callback leaves the local sample
uninitialised, and the hit-test callback still rejects every object. Also do not globally override either shared
stub: they have other callers/slots. Implement a narrowly scoped adapter or replace the per-package mouse path.
The native mouse path is evidence of reusable behaviour, not proof that turning it on will work.

### Runtime layout and calling conventions

Observed engine fields: interface pointer `+0x10C`, package-list head `+0xE4`, input-enabled byte `+2`, mouse-path
enabled byte `+3`. Packages link through `+4`, hold their name pointer at `+0x0C`, input mask at `+0x1C`, focused
object at `+0x80`, button-pointer array at `+0x84`, and button count at `+0x88`.

Objects have GUID `+0x0C`, name hash `+0x10`, type `+0x18`, flags `+0x1C`, parent transform index `+0x20`, and current
attribute pointer `+0x2C`. The native hover scan skips objects with flag `0x04000000`; establish the remaining
visibility/input checks before using this as a complete eligibility test.

`SetFocus` is thiscall, `ECX = package`, stack arguments `(engine, object, emit_messages)`, `ret 0xC`. It queues
unhighlight/highlight to the object and package handler before setting `package+0x80`. A bare pointer write would
skip those effects. `QueueMessage` is thiscall, `ECX = engine`, stack arguments
`(message_hash, source_object, package, target, input_mask)`. Confirm ABI in assembly when wiring host calls;
decompiler output often omits the implicit receiver at call sites.

Verified message hashes, using `feng::Hash`:

| Message | Hash |
|---|---:|
| `Button_Pressed` | `0x0C407210` |
| `Button_Released` | `0x936A6A7F` |
| `Button_Highlight` | `0xABC08912` |
| `Button_Unhighlight` | `0x55D1E635` |
| `Pad_Accept` | `0x406415E3` |
| `Pad_Back` | `0x911AB364` |
| `Pad_Left` | `0x9120409E` |
| `Pad_Right` | `0xB5971BF1` |

The controller and native mouse paths both send `Button_Pressed` when the focused object handles it, with a
`Pad_Accept` fallback otherwise. They also notify the package handler with target sentinel `0xFFFFFFFB`.
Preserve the normal object/handler routing rather than calling a screen handler directly.

## Package inspection and exceptions

Extracted the shell and pause archives from the local ISO into ignored `re/viv/`, then inspected their object
fields and message lists. No game data or decompiled functions were added to tracked source.

- `shell/MainMenu.fng`: six buttons; the tabs and Load Game/Credits entries have highlight, unhighlight and press
  responses. These are text objects with button flags, not pre-existing rectangular hit boxes.
- `shell/Option_overlay.fng`: ten stock buttons with the same responses. The runtime PC Options insertion moves
  subsequent rows, so hit testing should use the loaded objects rather than stock screen-coordinate constants.
- `pause/Preferences.fng`: four stock buttons; each handles highlight, unhighlight and press.
- `pause/Pause.fng`: no button-flagged objects. The pause backdrop contains the Start/resume and controller hints.
  Preferences also contains plain text objects for the Objectives/Load/Preferences/Notes headings. Clicking those
  headings or the resume hint needs additional routing; a button-array scan alone does not cover the whole pause UI.
- `pause/HUD.fng`: the Crosshairs and HUD rows are button groups that handle highlighting. Their left/right arrow
  images are decorative children without press responses. PC Options clones this arrangement for its ten rows.
  Clicking an arrow should focus its owning row and send the appropriate `Pad_Left`/`Pad_Right` event. Row clicks
  must not blindly confirm/close the whole options screen.

Current positions alone are insufficient for accurate hit testing. Resolve localised text extents, justification,
scale, parent transforms and animation; group rows may need a union of label/value/arrow regions.
The position helper supplies a useful anchor but does not compute a complete clickable rectangle.

## Pointer coordinates

`sdk/runtime/gpu/d3d12.cpp::BlitFrame` fits a centred 4:3 or 16:9 picture into the host backbuffer. Account for its
letterboxing/pillarboxing first. `src/common/widescreen.cpp::Hud::ScreenX` then compresses menu/HUD x coordinates
around the guest frame centre by `1/kWiden`, where `kWiden = 4/3`, in widescreen mode.

For a host client point, first map from the actual displayed picture rectangle to guest frame coordinates. In
widescreen mode invert that extra x transform: `original_x = centre_x + (displayed_x - centre_x) * kWiden`.
Points outside the displayed picture must not hit a menu. Then invert the actual FEng renderer transform into the
object's coordinate space; its origin, text bounds and any safe-area adjustments still need validation.
Do not assume every menu uses an untransformed 640-by-480 coordinate grid, or use render resolution as host size.

## Original implementation plan

1. Add an opt-in diagnostic at the shared FEng update/per-package input point in both builds. Record package
   names, input masks, focus, transformed button rectangles and hover candidates. Confirm modal/topmost package
   selection, disabled controls, pause tab dispatch and geometry before enabling activation.
2. Implement host absolute pointer/button snapshots and a game-thread menu adapter. Reuse the focus setter and
   normal FEng message routing. Keep stationary pointers from overriding subsequent keyboard/controller navigation;
   take mouse ownership on movement/click and settle held buttons on focus or package changes.
3. Begin with hover and one LMB action per press for ordinary entries. Add RMB/back and explicit arrow actions,
   followed by pause headings and resume. Bind a press to its package so it cannot activate a newly opened overlay
   or leak into resumed gameplay. Clear accumulated look counts during transitions.
4. Check shell tabs, Options and PC Options; pause tabs, Preferences, PC Options and resume; confirm/cancel dialogs;
   clicks on empty space and borders; alt-tab with held buttons; controller handover; 4:3/widescreen, window resize
   and render scaling; 30/60/unlocked frame rates; co-op input ownership.

The plan above has been implemented; automated checks and remaining interactive checks are listed at the top.

## Additional implementation addresses

| Role | default.xbe | moh3rdc.xbe |
|---|---:|---:|
| FEng render | `0x93EE0` | `0x86840` |
| Base FEng render object callback | `0x1164E0` | `0x10EFB0` |
| ShellManager render override (live shell/pause callback) | `0x125E10` | `0x11FE90` |
| Font drawing after positioning/justification | `0x162AE0` | `0x15BEB0` |
| ASCII font bounds (x/y/width/height outputs) | `0x162B20` | `0x15BEF0` |
| Transformed sprite quad | `0xB6B90` | `0xAB430` |
| Object message lookup | `0x958D0` | `0x88270` |
| Directional button navigation | `0x88630` | `0x7AFB0` |

The adapter uses narrowly scoped function overrides. Native mouse callbacks remain disabled, shared empty/false
stubs remain untouched, and the original game assets on disk are unchanged. `MenuFile` injects the direct tab
responses into in-memory pause packages alongside the existing PC Options package changes.
Ghidra confirmed that response kind `0x200` queues a native replace-package operation (`0x93300` / co-op `0x85C60`),
including the original input mask, parent linkage and lifecycle handling. Mouse clicks use this existing path.
