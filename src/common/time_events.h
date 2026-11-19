#pragma once
// The XDK's timeSetEvent after a quick load (docs/quick-save.md "Timer events"). Its thread (default.xbe 0x1A78AC,
// started by the first timeSetEvent, 0x1A7A94) keeps the table of events on its own stack, which a load leaves as
// it is, while the guest clocks go back (or on) to the save's moment. An event is 0x44 bytes, 0x3F of them, the table
// at [0x3A6BDC]: its kernel timer at +0, callback +0x28, argument +0x2C, in use +0x30, flags +0x31 (bit 0
// periodic), +0x34 the due interrupt time and +0x3C the period (negative, 100 ns units). Each time a periodic
// event fires, the thread adds the period to the due time and sets the timer for due - now: with the clock moved
// back by the time between the save and the load, the next one comes that much later. The sound library's 100 Hz
// tick (0x16E010, from 0x16E050) is such an event; while it stops, its tasks do not run, a checkpoint's sound bank
// (0x104F90) never finishes loading, and the level's restart (0xA3B30) waits on it for good.
#include <cstdint>

namespace game {

struct TimeEventLayout {
    uint32_t table;  // global: the thread's table of events
};
inline constexpr TimeEventLayout kDefaultTimeEvents{0x3A6BDC};

// every event in use: its due time moved with the guest clocks (moved_ns as snapshot::AfterRestore has it), so it
// is as far from now as it was at the save
void ShiftTimeEvents(int64_t moved_ns, const TimeEventLayout& l);

}  // namespace game
