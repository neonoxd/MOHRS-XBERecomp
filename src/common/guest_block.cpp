#include "common/guest_block.h"

#include <array>
#include <cstring>
#include <random>

#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace game {

namespace {

constexpr uint32_t kCookieSize = 16;

// "MOHRSblk" and eight bytes this instance makes up
const uint8_t* Cookie() {
    static const std::array<uint8_t, kCookieSize> cookie = [] {
        std::array<uint8_t, kCookieSize> c{'M', 'O', 'H', 'R', 'S', 'b', 'l', 'k'};
        std::random_device random;
        for (size_t i = 8; i < c.size(); i++) c[i] = uint8_t(random());
        return c;
    }();
    return cookie.data();
}

// the allocation: the bytes rounded as the system allocator rounds them, then the cookie
uint32_t Total(uint32_t size) { return ((size + 15) & ~15u) + kCookieSize; }

}  // namespace

bool GuestBlock::Ours(uint32_t size) const {
    if (!va_ || size != size_) return false;
    uint32_t total = Total(size_);
    if (mem::AllocationSize(va_) != total) return false;
    return std::memcmp(kernel::HostPtr(va_ + total - kCookieSize), Cookie(), kCookieSize) == 0;
}

uint32_t GuestBlock::Get(uint32_t size, bool* fresh) {
    if (fresh) *fresh = false;
    if (Ours(size)) return va_;
    uint32_t total = Total(size);
    va_ = mem::AllocSystem(total);
    size_ = va_ ? size : 0;
    if (!va_) return 0;
    std::memcpy(kernel::HostPtr(va_ + total - kCookieSize), Cookie(), kCookieSize);
    if (fresh) *fresh = true;
    return va_;
}

uint32_t GuestBlock::Hold(const void* data, uint32_t size, bool* fresh) {
    bool made = false;
    uint32_t va = Get(size, &made);
    if (va && made) std::memcpy(kernel::HostPtr(va), data, size);
    if (fresh) *fresh = made;
    return va;
}

}  // namespace game
