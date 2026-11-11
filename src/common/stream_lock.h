#pragma once
// The EA stream library's packet take (default.xbe 0x165390, co-op 0x15E760; cdecl (channel)) reads the channel's
// byte count (+8) and current packet (+0xC) before it takes the stream's lock, which it holds only to lower the
// count. The stream's reader (0x164B90, co-op 0x15DF60) adds a packet under that lock: the count first, then, when the
// count was 0, the packet. On the single-core Xbox the take never ran between those two stores; on a PC both threads
// run at once, and a take of a fresh stream's first packet read a count with no packet yet and crashed at 0x1653BB
// (the movie player opening a level's intro movie, 0xFC170). The take is run under the stream's own lock instead:
// a channel's first word is its stream ('STRM'), whose critical section is at +8 and is taken through the library's
// lock helpers (cdecl (stream + 4)), recursive, so the take's own lock inside still works.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

struct StreamCalls {
    uint32_t lock, unlock;
};
inline constexpr StreamCalls kDefaultStreams{0x16E750, 0x16E760};
inline constexpr StreamCalls kCoopStreams{0x167B10, 0x167B20};

using StreamTake = void (*)(X86Context&, uint8_t*);
void LockedStreamTake(X86Context& ctx, uint8_t* base, StreamTake original, const StreamCalls& calls);

}  // namespace game
