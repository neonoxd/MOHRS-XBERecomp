// Host test: the XDK timer events moved with the guest clocks after a quick load (src/common/time_events.h), and the
// thread's next wait as 0x1A78AC computes it for a periodic event.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/time_events_test.cpp
//     src/common/time_events.cpp -o re/time_events_test.exe
#include "common/time_events.h"
#include "core/log.h"
#include "kernel/kernel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
std::vector<uint8_t> memory(0x400000);

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

using kernel::G;
const game::TimeEventLayout& L = game::kDefaultTimeEvents;
constexpr uint32_t kTable = 0x80000;  // on the timer thread's stack in the game
constexpr int64_t kSecond = 10000000, kPeriod = -100000;  // 100 ns units; 10 ms, relative

uint32_t Event(uint32_t i) { return kTable + i * 0x44; }
int64_t Due(uint32_t i) {
    int64_t v;
    std::memcpy(&v, kernel::HostPtr(Event(i) + 0x34), 8);
    return v;
}
void Arm(uint32_t i, int64_t due) {
    G<uint8_t>(Event(i) + 0x30) = 1;
    G<uint8_t>(Event(i) + 0x31) = 1;  // periodic
    std::memcpy(kernel::HostPtr(Event(i) + 0x34), &due, 8);
    std::memcpy(kernel::HostPtr(Event(i) + 0x3C), &kPeriod, 8);
}
// the wait the thread sets after the event fires at `now`: due less the period, then due - now (0 when late)
int64_t NextWait(uint32_t i, int64_t now) {
    int64_t due = Due(i) - kPeriod;
    return due > now ? due - now : 0;
}
}  // namespace

namespace kernel { uint8_t* g_base = memory.data(); }
namespace logx { void Write(Level, const char*, ...) {} }

int main() {
    G<uint32_t>(L.table) = kTable;
    // saved at 400 s, played on to 600 s, loaded: the clocks go back 200 s to 400 s
    int64_t now = 600 * kSecond;
    Arm(0, now + 30000);  // the sound tick, due in 3 ms
    Arm(5, now + 2 * kSecond);
    G<uint8_t>(Event(7) + 0x30) = 0;  // free
    std::memcpy(kernel::HostPtr(Event(7) + 0x34), &now, 8);
    int64_t moved = -200 * kSecond;
    int64_t after = now + moved;
    Check(NextWait(0, after) > 199 * kSecond, "as the game has it: the next tick 200 s away");
    game::ShiftTimeEvents(moved * 100, L);
    Check(Due(0) == after + 30000 && Due(5) == after + 2 * kSecond, "events in use as far from now as before");
    Check(NextWait(0, after + 30000) == -kPeriod, "the next tick a period later");
    int64_t free;
    std::memcpy(&free, kernel::HostPtr(Event(7) + 0x34), 8);
    Check(free == now, "free events left alone");
    // a save from a longer run loaded early in a new one: the clocks go on
    game::ShiftTimeEvents(300 * kSecond * 100, L);
    Check(Due(0) == after + 30000 + 300 * kSecond, "moved on too");
    // no table yet (no timeSetEvent made)
    G<uint32_t>(L.table) = 0;
    game::ShiftTimeEvents(moved * 100, L);
    std::printf("time_events_test: all passed\n");
    return 0;
}
