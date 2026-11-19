#include "common/checkpoint_weapons.h"

#include "kernel/kernel.h"

using kernel::G;
using kernel::R32;

namespace game {

namespace {

constexpr uint32_t kSlotSize = 12, kSlotsPerPlayer = 8;
constexpr uint32_t kCountA = 4, kCountB = 6, kUsable = 8;  // within a slot
constexpr uint32_t kHeldWeapon = 0x2BC, kNextHolder = 0x2B4, kHolderFlags = 0x3C0;
constexpr uint8_t kHolderHas = 4;
constexpr uint32_t kMaxHolders = 64;

bool Pointer(uint32_t address) { return address >= 0x10000; }

// this: the game state's holder, [this] the game state
uint32_t FirstSlot(uint32_t self, uint32_t index, const CheckpointLayout& l) {
    return R32(self) + l.slots + index * kSlotsPerPlayer * kSlotSize;
}

// the slot holding this id, or 0
uint32_t Find(uint32_t self, uint32_t id, uint32_t index, const CheckpointLayout& l) {
    uint32_t first = FirstSlot(self, index, l), slots = CheckpointSlots(index, l);
    for (uint32_t i = 0; i < slots; i++)
        if (R32(first + i * kSlotSize) == id) return first + i * kSlotSize;
    return 0;
}

// the player's holder of this weapon id, or 0
uint32_t Holder(uint32_t index, uint32_t id, const CheckpointLayout& l) {
    uint32_t player = R32(l.players + index * 4);
    if (!Pointer(player)) return 0;
    uint32_t head = R32(player + l.ring), at = head;
    for (uint32_t i = 0; Pointer(at) && i < kMaxHolders; i++) {
        uint32_t weapon = R32(at + kHeldWeapon);
        if (Pointer(weapon) && R32(weapon + l.weapon_id) == id) return at;
        at = R32(at + kNextHolder);
        if (at == head) break;
    }
    return 0;
}

}  // namespace

uint32_t CheckpointSlots(uint32_t index, const CheckpointLayout& l) {
    return index == 0 && R32(l.player_count) == 1 ? 2 * kSlotsPerPlayer : kSlotsPerPlayer;
}

void CheckpointSaveStart(uint32_t player, const CheckpointLayout& l) {
    uint32_t index = R32(player + l.index);
    if (index > 1) return;
    uint32_t first = FirstSlot(l.state, index, l);
    for (uint32_t i = 0; i < CheckpointSlots(index, l) * kSlotSize; i += 4) G<uint32_t>(first + i) = 0;
}

void CheckpointStore(X86Context& ctx, const CheckpointLayout& l) {
    uint32_t self = ctx.ecx, id = R32(ctx.esp + 4), index = R32(ctx.esp + 0x10);
    uint16_t a = G<uint16_t>(ctx.esp + 8), b = G<uint16_t>(ctx.esp + 0xC);
    uint32_t slot = Find(self, id, index, l);
    if (!slot && !a && !b) {
        // no ammo and not the player's: a missing weapon loads as none with no ammo, so it needs no slot. The
        // grenades (+0xE74, in no holder) are kept whatever they hold.
        uint32_t holder = Holder(index, id, l);
        if (holder && !(G<uint8_t>(holder + kHolderFlags) & kHolderHas)) return kernel::Return(ctx, ctx.eax, 4);
    }
    if (!slot) slot = Find(self, 0, index, l);
    if (slot) {
        G<uint32_t>(slot) = id;
        G<uint16_t>(slot + kCountA) = a;
        G<uint16_t>(slot + kCountB) = b;
    }
    kernel::Return(ctx, ctx.eax, 4);
}

void CheckpointCounts(X86Context& ctx, const CheckpointLayout& l) {
    uint32_t self = ctx.ecx, id = R32(ctx.esp + 4), a_out = R32(ctx.esp + 8), b_out = R32(ctx.esp + 0xC);
    uint32_t index = R32(ctx.esp + 0x10);
    G<uint32_t>(a_out) = 0;
    G<uint32_t>(b_out) = 0;
    if (uint32_t slot = Find(self, id, index, l)) {
        G<uint32_t>(a_out) = G<uint16_t>(slot + kCountA);
        G<uint32_t>(b_out) = G<uint16_t>(slot + kCountB);
    }
    kernel::Return(ctx, ctx.eax, 4);
}

void CheckpointSetUsable(X86Context& ctx, const CheckpointLayout& l) {
    uint32_t self = ctx.ecx, id = R32(ctx.esp + 4), index = R32(ctx.esp + 0xC);
    uint8_t usable = G<uint8_t>(ctx.esp + 8) & 1;
    if (uint32_t slot = Find(self, id, index, l)) G<uint8_t>(slot + kUsable) = (G<uint8_t>(slot + kUsable) & ~1) | usable;
    kernel::Return(ctx, ctx.eax, 3);
}

void CheckpointUsable(X86Context& ctx, const CheckpointLayout& l) {
    uint32_t self = ctx.ecx, id = R32(ctx.esp + 4), index = R32(ctx.esp + 8);
    uint32_t slot = Find(self, id, index, l);
    kernel::Return(ctx, slot ? G<uint8_t>(slot + kUsable) & 1 : 0, 2);
}

}  // namespace game
