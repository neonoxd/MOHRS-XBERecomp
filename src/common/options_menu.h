#pragma once
#include <cstdint>

#include "cpu/x86_context.h"

// PC Options in the game's own menus: an entry under Audio in the shell's options list and in the pause screen's
// preferences opens PCOptions.fng, a screen made from the HUD options screen (src/common/options_package.h) whose
// rows show and change the settings file's display, frame rate, view and mouse settings. Left and right change a
// row, confirm saves the settings file, back puts the settings back as they were on entry.
// The pause screen's preferences also get Cheats under it, Cheats.fng made the same way, whose rows switch the
// cheats (src/common/cheats.h) for the session.
namespace game {

using GuestFunction = void (*)(X86Context&, uint8_t*);

// the screen code's helpers in each image
struct MenuCalls {
    uint32_t set_text;    // stdcall (package, object name, text): a string object's text
    uint32_t run_script;  // stdcall (package, object name, script name)
    uint32_t sound;       // cdecl (sound): the menus' sounds, 0 toggle off, 1 on, 6 slider up, 7 down
};

// the archive's file lookup (cdecl: archive, path, flags; the file's data): PCOptions.fng made from the archive's
// HUD.fng, the options lists given their entry, and pause pages given direct mouse tab responses
void MenuFile(X86Context& ctx, uint8_t* base, GuestFunction original);
// the shell's screen factory (cdecl: package name; a new handler): PCOptions.fng gets the HUD screen's handler
void MenuScreen(X86Context& ctx, uint8_t* base, GuestFunction original);
// the HUD screen handler's event (thiscall: event hash, 2 more; ret 0xc): PCOptions.fng's events handled here
void MenuEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls);
// the controller options screen handler's event (thiscall: event hash, 2 more; ret 0xc): left and right on the
// Modern Aiming row added to ControllerOptions.fng switch it, confirm keeps it in the settings file
void ControllerMenuEvent(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls);
// the stats screens' fill (thiscall: package, cumulative, medals, 2 more; ret 0x14): for the level's stats (not the
// profile's cumulative ones) the mission time row added to EndOfLevelSummary.fng (src/common/options_package.h) gets
// the game's own level timer as m:ss.mmm; state is the game state pointer's address
void MenuStats(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuCalls& calls, uint32_t state);

}  // namespace game
