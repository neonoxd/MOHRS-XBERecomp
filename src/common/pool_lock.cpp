#include "common/pool_lock.h"

#include <mutex>

namespace game {

namespace {
// held only for a bitmap search or a bit set, which wait on nothing
std::mutex g_pools;
}  // namespace

void LockedPool(X86Context& ctx, uint8_t* base, PoolFunction original) {
    std::lock_guard<std::mutex> lock(g_pools);
    original(ctx, base);
}

}  // namespace game
