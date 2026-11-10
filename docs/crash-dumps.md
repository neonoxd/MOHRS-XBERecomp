# Crash dumps

A crash on a guest thread writes three things beside the executable:

- **The log** (`logs\mohrs.log`): the exception and address, the host call stack with each frame named, and the
  crashed guest thread's registers and the code addresses on its stack. Game code frames are named by the DLL's
  exports, `MOHRS.dll!__imp__sub_001A9D78+0x54A (game code 001A9D78)`, which needs no symbols; host frames come from
  the executable's PDB.
- **The dump** (`CrashDumps\MOHRS.<date>_<time>.dmp`, about 100 MB): the host threads and their stacks, plus every
  committed guest page and the 64 MB of guest physical memory, at the guest base (`0x100000000` unless that was
  taken). Its comment stream says where: `guest base 0000000100000000; guest thread 14, X86Context at ...`.
- **Symbols**: `MOHRS.pdb` for the executable and `MOHRS_code.pdb` for the game code, with line tables: a frame
  names the line of `generated/game/x86_recomp.<n>.cpp`, whose comments hold the guest instructions. A release
  compiles the game code on the player's PC (`runtime/app/game_code.cpp`) with the same line tables.

`tools/crash_report.ps1` turns a dump into a report with cdb (Windows SDK Debugging Tools):

```powershell
powershell -NoProfile -File tools/crash_report.ps1 out\build\release\CrashDumps\MOHRS.<time>.dmp -Guest 83EA2A10:40
```

It prints the host call stack with game functions and lines, the guest thread's registers and stack, and the guest
memory asked for (`address:bytes`, hex). The symbols must be the ones built with that executable and DLL: by default
the folder holding `CrashDumps`, else `-Symbols <folder>`. In cdb itself, guest address `A` is at `base + A`.

Dumps written before 2026-11-08 hold no guest memory, and the release DLL had no symbols then: they give the host
stack only, with the game frames unnamed. The pad word `crash` (`MOHRS_MENU_PROBE`, `tools/fps_probe.ps1 -Pad`)
crashes on purpose to test this.
