#include "common/stream_lock.h"

#include "kernel/kernel.h"

using kernel::R32;

namespace game {

namespace {
constexpr uint32_t kStreamTag = 0x4D525453;  // 'STRM'
}

void LockedStreamTake(X86Context& ctx, uint8_t* base, StreamTake original, const StreamCalls& calls) {
    // the take's own checks: a channel, its stream, the stream's tag
    uint32_t channel = R32(ctx.esp + 4);
    uint32_t stream = channel ? R32(channel) : 0;
    if (!stream || R32(stream) != kStreamTag) {
        original(ctx, base);
        return;
    }
    kernel::CallGuest(ctx, calls.lock, {stream + 4});
    original(ctx, base);
    // eax holds the taken packet; CallGuest keeps the registers
    kernel::CallGuest(ctx, calls.unlock, {stream + 4});
}

}  // namespace game
