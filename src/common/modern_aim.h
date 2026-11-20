#pragma once
// Modern Aiming (Controller Options' Modern Aiming row, setting modern_aiming; docs/input.md "Modern Aiming"): the
// player walks and jumps while aiming, at half speed on the ground, instead of the game's lean.
//
// The player update (default 0xDB530, co-op 0xD4BF0) sets the aim bit (+0xD44 bit 4) from the aim action (15) only
// on the ground (its ground test in bl, a floor touched, or still aiming from the last frame: bit 5). While it is set
// the update takes the look stick at the zoom's slower rates, then (unless in the air, +0xD45 bit 6) zeroes the walk
// velocity (+0x240 strafe, +0x244 forward) and calls the lean (0xD9A20 / 0xCF1A0): the strafe stick moves the
// player up to a unit sideways from where the aim started (+0xDE0 / +0xEC0), +0xD45 bit 3 set while out, and the
// player is put back there when the aim ends. The jump is dropped, and the walk code (actions 6 and 7) is skipped.
// Crouched and aiming, forward raises the eye over cover; standing with +0xD4C bit 5, back lowers it.
//
// With the setting on and the player on foot (not on a vehicle or gun, mounting, scripted or on a ladder; in co-op
// not in its states that move the player without velocity) instruction hooks in the update:
// - the aim may start off the ground (the ground test is passed over);
// - the eye stays where it is (the stick's raise and lower read as 0);
// - the aim block keeps the look rates it worked out and passes over the stop, the lean and the dropped jump;
// - the walk code runs as when not aiming; after it the look rates are the aim's again, and on the ground the walk
//   velocity is halved (in the air it is left as when not aiming).
// While the walk code runs for an aiming player the lean's anchor follows it, as the game's aim block does in the air.
//
// The lean key (src/common/mouse_keyboard.h, also the aim) gives KBM's player the game's aim rules back while on the
// ground (+0xD45 bit 6 clear), the lean with them: the hooks above leave that update to the game. Taken mid-aim the
// aim goes on, the anchor is where the player stands; let go of with the aim still held, a lean that is out is put
// back at the anchor (as the game does when the stick centres) and the walk goes on. In the air the key only aims.
// Custom Movement (src/common/custom_movement.h) keeps its rules while aiming with the setting on, its ground speed
// halved, but not while the lean key has the game's rules.
//
// The lean (0xD9A20 / 0xCF1A0) is overridden for KBM's player on the ground with the lean key held (Modern Aiming on
// or off): it moves at three quarters speed (kLeanSpeed), and a lean that is out stays where it is when the strafe keys are let
// go of (the game puts it back at the anchor). It goes back when the lean key is let go of: the game's aim end, or
// ModernAimHold, or the game's lean with the strafe centred while the aim is still held.
// The eye raise and lower (crouched forward raises the eye over cover, standing with +0xD4C bit 5 back lowers it) is
// overridden the same way: the game steps the eye (+0x270) most of the way to the stick's height each update; with
// the lean key forward moves it up and back down at kEyeSpeed, and with neither it stays. Let go of the lean key, the
// game's rules take it back to rest.
// The weapon's walk bob comes from the move event (0x84 to +0x220: 2 above 0.0534 a tick, 1 above 0.00667, 0 below)
// the update sends from the ground speed (+0x230, +0x234). With the lean key leaning the player that speed reads 0,
// so the lean's own velocity does not start the bob.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct AimLayout {
    uint32_t yaw_rate, pitch_rate;  // the update's look rates, esp offsets
    uint32_t ground;                // its ground byte, an esp offset
    bool coop;                      // co-op's states that move the player without velocity: +0xE44, +0xE45, +0xD4E bits 6-7
    uint32_t anchor;                // player: where the aim started, the lean's anchor
    uint32_t players, input_manager;  // globals: the players by input manager entry, the input manager (KBM's player)
    uint32_t eye_height;            // constant: the eye raise's full height (1.1)
    uint32_t move_speed;            // the update's ground speed for the move event, an esp offset
};
inline constexpr AimLayout kDefaultAim{0x2C, 0x28, 0x58, false, 0xDE0, 0x270108, 0x271188, 0x1E8F04, 0x14};
inline constexpr AimLayout kCoopAim{0x38, 0x1C, 0x5C, true, 0xEC0, 0x265278, 0x266618, 0x1E1AC4, 0x10};

// the share of the walk velocity kept while aiming on the ground
inline constexpr float kAimWalk = 0.5f;
// the share of the lean's velocity kept with the lean key
inline constexpr float kLeanSpeed = 0.75f;
// the eye's speed with the lean key, per second: the game's step limit (0.04 an update, 0x201EF0) at 30 updates a
// second, times kLeanSpeed. The full height takes about 1.2 s.
inline constexpr float kEyeSpeed = 0.04f * 30 * kLeanSpeed;
inline constexpr float kEyeDeadzone = 0.25f;

using GuestFunction = void (*)(X86Context&, uint8_t*);

// the player (esi) walks while aiming: the hooks that pass over the game's aim rules
bool ModernAim(const X86Context& ctx, const AimLayout& layout);
// at the walk code's aim test: ModernAim, but not for the player the aim block left to the game's lean this update
bool ModernAimWalk(const X86Context& ctx, const AimLayout& layout);
// at the aim block's velocity part: true (to pass over it) when ModernAim, keeping the look rates
bool ModernAimHold(X86Context& ctx, const AimLayout& layout);
// where the walk code and the aim's path meet: the kept look rates back, the walk slowed
void ModernAimWalked(X86Context& ctx, const AimLayout& layout);
// the player's last aim block was left to the game's lean (Custom Movement keeps out then)
bool ModernAimLeaning(uint32_t player);
// the lean (0xD9A20 / 0xCF1A0, ecx the player): with the lean key, slower and held out when the strafe centres
void Lean(X86Context& ctx, uint8_t* base, GuestFunction original, const AimLayout& layout);
// where the eye raise and lower meet, before the step's limit (esi the player, [ebp+8] the update's ticks): with the
// lean key, the step from the forward action at kEyeSpeed, 0 with the stick centred
void EyeHold(X86Context& ctx, const AimLayout& layout);
// after the update stores the ground speed for the move event (esi the player): 0 while the lean key leans
void LeanStill(X86Context& ctx, const AimLayout& layout);

}  // namespace game
