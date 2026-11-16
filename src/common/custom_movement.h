#pragma once
// Custom Movement (docs/menus.md "Custom Movement"): the on-foot player's velocity from Quake's player movement rules
// instead of the game's own, the game keeping its collision, stepping and animation.
//
// The game's player update (default 0xDB530, co-op 0xD4BF0; thiscall (player, ticks), ret 4) sets the local velocity
// (+0x240 strafe, +0x244 forward, +0x248 up) from move actions 6 and 7 every frame on the ground (no acceleration) and
// adds to it in the air, then the world velocity (+0x230) from the orientation rows (+0x50 right, +0x60 forward,
// +0x70 up). The move step (0xE0E50 / 0xD8DD0, thiscall (player, ticks), ret 4) takes gravity (0x1E84C4 / 0x1E1098,
// 9.8 units/s^2: the game's unit is a meter, its velocity per tick of 1/59.94 s) off the world z velocity unless the
// last collision found a floor (+0xD44 bit 1, contact normal z > 0.642), then moves by it.
//
// Here the update runs as usual and its velocity is replaced afterwards by one kept across frames (in Quake's
// units, an inch: 0.0254 units) and run through Quake's rules: ground friction, acceleration, air acceleration
// with its speed cap of 30, the jump and gravity; Quake's ground friction doubled for the game's scale, and a held jump
// hopping on every landing. Walls are taken from the move itself: when the player moved less than the velocity asked
// for, the part of the velocity into what stopped it is taken away, as Quake's clip against a plane does; a move
// that rose instead (a stair step, a slope) keeps it.
// The ground is the update's own test (a ray down by the collision shape's length and 5 cm, or a floor touched), off
// when going up faster than 180 inches/s. In the air the move step's gravity is given back before
// it takes it; on the ground it stays, settling the player onto the floor. The jump action (10) the update reads is
// the one the rules decided on, so the game's jump sound and animation go with the rules' jumps. Up a slope or over
// a stair's edge the move rises along the face touched (the contact handler's contact points: list at pair +0x1C,
// next +0x2C, normal +0 +4 +8 away from the pair's first object, touching when the depth +0x20 is below 0, floor when
// the normal's z is above 0.642): pushed down into it, the collision would push the player back out along its normal
// and on a steep one undo the move (movement::ClimbAlong). The rules read the
// jump held (the action's +4; the query's +8 is set only on the frame of the press), and a press keeps for 0.15 s. On
// the ground the move also goes down as fast as across (to 300 inches/s), so slopes and stairs are followed down,
// and for 0.06 s off a step edge walked off the player is still on the ground. Edge friction (Quake's trace
// ahead for a drop) is not done.
//
// A player's own grenades and rockets do no damage to them and push them away as Quake's do (a rocket jump). The
// game applies an explosion to each object in reach with 0x14B020 / co-op 0x144360 (thiscall (explosion, target),
// ret 4; explosion +0x20 its centre, +0x34 its damage, +0x3C who fired it: the projectile's owner, the player object
// for the player's weapons), after its line-of-sight test (0x14ABE0 / 0x143F20, thiscall (explosion, target): true
// when blocked). The game itself never pushes a player with an explosion.
#include <cstdint>

#include "common/cheats.h"
#include "cpu/x86_context.h"

namespace game {

enum class Movement : uint32_t { Off, Quake, Count };

// the menu's choice (any thread); kept in MOHRS_MOVEMENT for the instance a level relaunches into
Movement CustomMovement();
void SetCustomMovement(Movement movement);
const char* MovementName(Movement movement);
// the first player's speed across in Quake's units a second (inches), for the overlay's counter (any thread): false
// while off, or when no update set it in the last 250 ms (paused, the shell, not on foot)
bool MovementSpeed(float& speed);

struct MovementLayout {
    uint32_t player_index;  // player: input index byte, +0x10DC / +0x11BC
    uint32_t stun;          // player: float, the velocity zeroed while above 0, +0xDC0 / +0xEA4
    uint32_t gravity;       // the move step's gravity per tick^2
    uint32_t blast_blocked; // thiscall (explosion, target): true when the explosion's line of sight to it is blocked
    bool coop;              // co-op's states that move the player without velocity: +0xE44, +0xE45, +0xD4E bits 6-7
    const CheatLayout& cheats;
};

inline constexpr MovementLayout kDefaultMovement{0x10DC, 0xDC0, 0x1E84C4, 0x14ABE0, false, kDefaultCheats};
inline constexpr MovementLayout kCoopMovement{0x11BC, 0xEA4, 0x1E1098, 0x143F20, true, kCoopCheats};

// around the player update (ecx the player, its ticks at [esp+4]): Begin before, End after with the same player.
// Ground in the update once its ground test is done (default 0xDB7C4, co-op 0xD4EB9: esi the player, bl set when
// its short ray down hit or the last collision touched a floor), before it reads the jump: the rules run there.
void MovementUpdateBegin(X86Context& ctx, const MovementLayout& layout);
void MovementGround(X86Context& ctx, bool ground, const MovementLayout& layout);
void MovementUpdateEnd(uint32_t player, const MovementLayout& layout);
// before the move step (ecx the player, its ticks at [esp+4])
void MovementStep(const X86Context& ctx, const MovementLayout& layout);
// the player's contact pair handler (default 0xDEFA0, co-op 0xD3040: player vtable +0x24, thiscall player, pair at
// [esp+4]): the floors touched in the step, for a move up a slope
void MovementContact(const X86Context& ctx);
// after the action query, with its arguments: the jump the rules decided on while the update runs
void MovementQuery(X86Context& ctx, uint32_t index, uint32_t action);
// the explosion's apply to one target (ecx the explosion, the target at [esp+4]), run in place of the original: a
// player's own blast does them no damage and pushes them by Quake's rule
void MovementBlast(X86Context& ctx, uint8_t* base, void (*original)(X86Context&, uint8_t*),
                   const MovementLayout& layout);
// before the view shake start the apply calls (0xD6670 / co-op 0xCB3B0: thiscall player, amplitude at [esp+4], ramp
// up, ramp down, hold; the player's shake slot +0xD58 / +0xD70 through 0xD5F70 / 0xCAA30): during a player's own
// blast the amplitude is scaled down
void MovementShake(X86Context& ctx);

}  // namespace game
