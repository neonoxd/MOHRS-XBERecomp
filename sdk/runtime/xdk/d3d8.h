#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "gpu/state.h"

namespace d3d {

// The device's lock: nearly always the title's thread, uncontended, a dozen times a draw. An atomic flag: one exchange
// to take it, a store to give it back; its owner taking it again only counts. Another thread that finds it taken
// (the vertical blank, a fence wait holding it) yields until it is free. (std::recursive_mutex was 7% of the title's
// thread.)
class DeviceLock {
public:
    void lock() {
        const void* me = Me();
        if (owner_.load(std::memory_order_relaxed) == me) {
            depth_++;
            return;
        }
        for (unsigned spins = 0; taken_.exchange(true, std::memory_order_acquire); spins++)
            if (spins > 64) std::this_thread::yield();
        owner_.store(me, std::memory_order_relaxed);
        depth_ = 1;
    }
    bool try_lock() {
        const void* me = Me();
        if (owner_.load(std::memory_order_relaxed) == me) {
            depth_++;
            return true;
        }
        if (taken_.exchange(true, std::memory_order_acquire)) return false;
        owner_.store(me, std::memory_order_relaxed);
        depth_ = 1;
        return true;
    }
    void unlock() {
        if (--depth_) return;
        owner_.store(nullptr, std::memory_order_relaxed);
        taken_.store(false, std::memory_order_release);
    }

private:
    static const void* Me() {
        static thread_local char token;
        return &token;
    }
    std::atomic<bool> taken_{false};
    std::atomic<const void*> owner_{nullptr};
    uint32_t depth_ = 0;
};

gpu::State& State();
DeviceLock& Lock();

// Immediate-mode vertex data for a title's own vertex helper that a game overrides natively: as SetVertexData4f(slot,
// v) and SetVertexDataColor(slot, argb), after the commands pushed before them. Slot 0, the position, ends a vertex.
void ImmediateData(uint32_t slot, const float* v);
void ImmediateColor(uint32_t slot, uint32_t argb);

}  // namespace d3d
