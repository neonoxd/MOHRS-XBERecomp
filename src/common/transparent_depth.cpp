#include "common/transparent_depth.h"

#include "kernel/kernel.h"

using kernel::R32;
using kernel::W32;

namespace game {

namespace {
// block fields, as 0x17D010 sends them (both images)
constexpr uint32_t kAlphaTest = 0x18;   // byte, D3DRS_ALPHATESTENABLE
constexpr uint32_t kAlphaRef = 0x1C;    // D3DRS_ALPHAREF
constexpr uint32_t kAlphaFunc = 0x20;   // D3DRS_ALPHAFUNC
constexpr uint32_t kBlend = 0x28;       // 0 off, 1 on, else left as it is
constexpr uint32_t kBlendOp = 0x30;     // D3DRS_BLENDOP
constexpr uint32_t kSrcBlend = 0x34;    // D3DRS_SRCBLEND
constexpr uint32_t kDestBlend = 0x38;   // D3DRS_DESTBLEND
constexpr uint32_t kZWrite = 0x48;      // D3DRS_ZWRITEENABLE; -1 the device's own (its +0x85, on in game)
constexpr uint32_t kDeviceZWrite = 0xFFFFFFFF;

constexpr uint32_t kBlendAdd = 0x8006;
constexpr uint32_t kOne = 1;
constexpr uint32_t kSrcAlpha = 0x302;
constexpr uint32_t kInvSrcAlpha = 0x303;
constexpr uint32_t kGreater = 0x204;

// source alpha 0 leaves the target as it was
bool ClearTexelsDrawNothing(uint32_t block) {
    uint32_t dest = R32(block + kDestBlend);
    return R32(block + kBlend) == 1 && R32(block + kBlendOp) == kBlendAdd && R32(block + kSrcBlend) == kSrcAlpha &&
           (dest == kInvSrcAlpha || dest == kOne);
}
}  // namespace

void ApplyStateBlock(X86Context& ctx, uint8_t* base, StateBlockApply original) {
    uint32_t block = ctx.ecx;
    uint8_t* test = base + block + kAlphaTest;
    uint32_t zwrite = R32(block + kZWrite);
    if (*test || (zwrite != 1 && zwrite != kDeviceZWrite) || !ClearTexelsDrawNothing(block)) {
        original(ctx, base);
        return;
    }
    // the block as the game reads it again (its caches then match the device)
    uint32_t ref = R32(block + kAlphaRef), func = R32(block + kAlphaFunc);
    *test = 1;
    W32(block + kAlphaRef, 0);
    W32(block + kAlphaFunc, kGreater);
    original(ctx, base);
    *test = 0;
    W32(block + kAlphaRef, ref);
    W32(block + kAlphaFunc, func);
}

}  // namespace game
