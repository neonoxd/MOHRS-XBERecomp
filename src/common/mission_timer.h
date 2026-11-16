#pragma once
#include <cstdint>
#include <string>

// The level's time, which the game keeps and never shows: the game state's +0x31760, in 1/60 s ticks, zeroed at
// level start (default.xbe 0xA17F2, moh3rdc.xbe 0x941B2) and added to by each unpaused gameplay frame's whole ticks
// (0x9F310 / 0x91C90). The end-of-level stats show it (src/common/options_menu.h), and with mission_timer the overlay
// shows it in the picture's top right corner during play.
namespace game {

inline constexpr uint32_t kMissionTicks = 0x31760;  // in the game state

// the game state pointer's and the front-end byte's addresses in each image
struct MissionTimerLayout {
    uint32_t state, front_end;
};
inline constexpr MissionTimerLayout kDefaultMissionTimer{0x2447D8, 0x2463B1};
inline constexpr MissionTimerLayout kCoopMissionTimer{0x23C9D0, 0x23E5B1};

// the level's ticks, 0 without a game state
uint32_t MissionTicksNow(uint32_t state);
// m:ss.mmm, h:mm:ss.mmm from an hour; the milliseconds step by 1/60 s
std::string FormatMissionTime(uint32_t ticks);

// at the gameplay frame's entry, paused too (game thread): the time for the overlay, outside the front end
void MissionTimerFrame(const MissionTimerLayout& layout);
// the time for the overlay (any thread): false when no gameplay frame published one in the last quarter second
// (loading, movies, the end-of-level screens, the front end)
bool MissionTimerTicks(uint32_t& ticks);

}  // namespace game
