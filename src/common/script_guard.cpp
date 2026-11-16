#include "common/script_guard.h"

#include <algorithm>
#include <iterator>
#include <set>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

using kernel::G;
using kernel::R32;
using kernel::W32;

namespace game {

namespace {

constexpr uint32_t kVtable = 0xC, kEntity = 0x20, kId = 0x24;
constexpr int kMostArgs = 16;

// The natives (functions reading the running native's index) whose code reads their first argument's +0x20 and calls
// through it with no test of it for null, found by their decompiled code: 80 of 592 in default.xbe, 77 of 586 in
// moh3rdc.xbe. Sorted.
constexpr uint32_t kUncheckedDefault[] = {
    0x030410, 0x030760, 0x030B20, 0x030C70, 0x030E40, 0x031040, 0x0310E0, 0x031280,
    0x031540, 0x0315B0, 0x0317D0, 0x0319D0, 0x031DE0, 0x032060, 0x0322C0, 0x0323E0,
    0x032430, 0x032510, 0x032C80, 0x032CE0, 0x033C00, 0x0358E0, 0x035950, 0x035AB0,
    0x035C90, 0x0374D0, 0x0383B0, 0x038C30, 0x039BF0, 0x03A9F0, 0x03AB00, 0x03AB80,
    0x03ABE0, 0x03B190, 0x03C050, 0x03C290, 0x03C600, 0x03C670, 0x03C6E0, 0x03C880,
    0x03C8F0, 0x03D030, 0x03D0A0, 0x03D7A0, 0x03E710, 0x03E790, 0x03E7F0, 0x03E8F0,
    0x03E960, 0x03E9C0, 0x03EDF0, 0x03EFD0, 0x03F040, 0x03F490, 0x03F520, 0x03F5B0,
    0x03F6E0, 0x03F7F0, 0x03F860, 0x03F990, 0x03FA00, 0x03FA70, 0x03FAF0, 0x03FB60,
    0x03FBD0, 0x03FC50, 0x03FD30, 0x03FDB0, 0x03FE10, 0x03FE80, 0x0401B0, 0x040390,
    0x040C20, 0x040CA0, 0x040D10, 0x041930, 0x0423C0, 0x0424B0, 0x0425A0, 0x044720,
};
constexpr uint32_t kUncheckedCoop[] = {
    0x02F4A0, 0x02F7F0, 0x02FBB0, 0x02FD00, 0x02FED0, 0x0300D0, 0x030170, 0x030310,
    0x0305D0, 0x030640, 0x030860, 0x030A60, 0x030E70, 0x0310F0, 0x031350, 0x031470,
    0x0314C0, 0x0315A0, 0x031D10, 0x031D70, 0x0331F0, 0x034F10, 0x034F80, 0x0350E0,
    0x0352F0, 0x036D20, 0x037EA0, 0x038720, 0x039740, 0x03A6C0, 0x03A7D0, 0x03A850,
    0x03A8B0, 0x03AE60, 0x03BD30, 0x03BF70, 0x03C3F0, 0x03C460, 0x03C4D0, 0x03C670,
    0x03C6E0, 0x03CF20, 0x03CF90, 0x03D6B0, 0x03E9C0, 0x03EA40, 0x03EAA0, 0x03EBA0,
    0x03EC10, 0x03EC70, 0x03F3C0, 0x03F5A0, 0x03F610, 0x03F710, 0x03F7A0, 0x03F830,
    0x03F960, 0x03FA70, 0x03FAE0, 0x03FE60, 0x03FED0, 0x03FF40, 0x040120, 0x040280,
    0x040680, 0x040850, 0x0410E0, 0x041160, 0x0411D0, 0x042570, 0x042640, 0x0447E0,
    0x045100, 0x0451F0, 0x0452E0, 0x0478F0, 0x049FA0,
};

bool Unchecked(uint32_t fn, bool coop) {
    return coop ? std::binary_search(std::begin(kUncheckedCoop), std::end(kUncheckedCoop), fn)
                : std::binary_search(std::begin(kUncheckedDefault), std::end(kUncheckedDefault), fn);
}

bool HeapObject(uint32_t v) {
    return v >= 0x80000000u && v < 0xC0000000u && !(v & 3) && mem::IsCommittedRange(v, kId + 4);
}

// heap objects only: an argument may be a number
bool Destroyed(uint32_t v, const ScriptLayout& l) {
    if (!HeapObject(v)) return false;
    return R32(v + kVtable) == l.destroyed_vtable && R32(v + kId) == 0xFFFFFFFFu && R32(v + kEntity) == 0;
}

}  // namespace

bool NativeOnDestroyed(X86Context& ctx, const ScriptLayout& l) {
    uint32_t pc = R32(l.pc);
    int32_t index = G<int16_t>(pc + 2);
    uint32_t desc = uint32_t(index) * 0x10 + R32(l.table);
    int32_t args = G<int16_t>(desc + 0xC), popped = G<int16_t>(desc + 0xA);
    if (args <= 0 || args > kMostArgs) return false;
    uint32_t top = R32(l.stack);
    uint32_t first = top - uint32_t(args) * 4 + 4;
    uint32_t fn = R32(desc), object = 0;
    const char* why = "destroyed";
    for (int32_t i = 0; i < args && !object; i++)
        if (Destroyed(R32(first + uint32_t(i) * 4), l)) object = R32(first + uint32_t(i) * 4);
    if (!object && Unchecked(fn, l.coop)) {
        uint32_t v = R32(first);
        if (HeapObject(v) && R32(v + kEntity) == 0) object = v, why = "entity-less";
    }
    if (!object) return false;
    static std::set<uint32_t> logged;  // game thread
    if (logged.insert(fn).second)
        LOGW("script: native %06X (index %d) skipped, given %s object %08X (class %08X)", fn, index, why, object,
             R32(object));
    // as the native would leave the stack: its arguments popped, a result in place of what it leaves
    uint32_t after = top - uint32_t(popped) * 4;
    W32(l.stack, after);
    if (popped < args) W32(after, 0);
    W32(l.pc, pc + 4);
    kernel::Return(ctx, ctx.eax, 0);
    return true;
}

uint32_t RunningNative(const ScriptLayout& l) {
    int32_t index = G<int16_t>(R32(l.pc) + 2);
    return R32(uint32_t(index) * 0x10 + R32(l.table));
}

bool SlotLookupWithoutOwner(X86Context& ctx) {
    if (ctx.ecx) return false;
    static std::set<uint32_t> logged;  // game thread
    uint32_t caller = R32(ctx.esp);
    if (logged.insert(caller).second)
        LOGW("script: slot %u looked up with no owner (from %06X), none found", R32(ctx.esp + 4), caller);
    kernel::Return(ctx, 0, 1);
    return true;
}

}  // namespace game
