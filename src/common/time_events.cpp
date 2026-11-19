#include "common/time_events.h"

#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {
constexpr uint32_t kEvents = 0x3F, kEventSize = 0x44;
constexpr uint32_t kInUse = 0x30, kDue = 0x34;
}  // namespace

void ShiftTimeEvents(int64_t moved_ns, const TimeEventLayout& l) {
    uint32_t table = R32(l.table);
    if (!table || !moved_ns) return;
    int64_t moved = moved_ns / 100;  // interrupt time units
    uint32_t shifted = 0;
    for (uint32_t i = 0; i < kEvents; i++) {
        uint32_t event = table + i * kEventSize;
        if (!G<uint8_t>(event + kInUse)) continue;
        int64_t due;
        std::memcpy(&due, kernel::HostPtr(event + kDue), 8);
        due += moved;
        std::memcpy(kernel::HostPtr(event + kDue), &due, 8);
        shifted++;
    }
    if (shifted) LOGR("quick load: %u timer events moved %.1f s with the guest clocks", shifted, moved_ns / 1e9);
}

}  // namespace game
