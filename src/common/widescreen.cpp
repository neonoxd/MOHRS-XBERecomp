#include "common/widescreen.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/options.h"
#include "gpu/ext.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/d3d8.h"

using kernel::G;

namespace game {

namespace {

constexpr uint32_t kTanAcross = 0x40;
constexpr uint32_t kZAlways = 0x207;  // NV2A depth function
constexpr uint32_t kHudTag = 0x48554431;  // State::tag of the gameplay HUD's draws ("HUD1")

// the playing movie's frame size (0 none), which its texture has: its draw is placed for the widened frame already
std::atomic<uint32_t> g_movie{0};

// how a vertex program places its vertices: the game's 2D is drawn in screen pixels, either passed through or through
// an orthographic matrix (the 2D renderer's, 0x17E780, and the sprite batches'), its 3D through a perspective one
struct Placement {
    bool passthrough = false;  // oPos = v0
    int matrix = -1;           // oPos.x = dot(v0, c[matrix]) and on through c[matrix + 3] for w
    // then the viewport, as the XDK's screen space programs end: oPos.xyz * c[scale] / w + c[offset], -1 none
    int scale = -1, offset = -1;
};

// from the runtime's HLSL for the program: its own translation, so the patterns hold for this SDK
Placement Classify(const std::vector<uint32_t>& program) {
    Placement p;
    std::string h = gpu::VertexProgramHlsl(program, 0xFFFF);
    if (h.find("precise float4 mr = ((i.v0).xyzw);\n        oPos.xyzw = mr.xyzw;") != std::string::npos) {
        p.passthrough = true;
        return p;
    }
    size_t x = h.find("oPos.x = mr.x;");
    if (x == std::string::npos) return p;
    const std::string dot = "dot(((i.v0).xyzw), ((c[";
    size_t d = h.rfind(dot, x);
    if (d != std::string::npos && x - d < 80) p.matrix = std::atoi(h.c_str() + d + dot.size());
    // the sprite batches' program: the matrix gives clip space, the viewport (c[58], c[59]) the console's pixels
    // oPos * c[scale], then oPos * (1 / w) + c[offset] on a line of its own (other outputs can come between)
    const std::string scale = "mac_mul(((oPos).xyzw), ((c[", divide = "(mac_mul(((oPos).xyzw), ((R", offset = "+ ((c[";
    size_t s = h.find(scale, x);
    if (p.matrix >= 0 && s != std::string::npos) {
        p.scale = std::atoi(h.c_str() + s + scale.size());
        size_t d = h.find(divide, s);
        size_t o = d == std::string::npos ? d : h.find(offset, d);
        if (o != std::string::npos && h.find('\n', d) > o) p.offset = std::atoi(h.c_str() + o + offset.size());
    }
    return p;
}

// the console x range of a draw's vertices as its program places them (v0, passed through, or through the matrix and
// the viewport); false when its positions are not plain floats
bool DrawX(const gpu::Assembled& a, const Placement& p, const float (*vsc)[4], float& lo, float& hi) {
    if (!(a.regs & 1) || (a.decode & 0xF)) return false;
    const gpu::VertexElement& e = a.elements[0];
    if (e.buffer >= a.buffer_count) return false;
    uint32_t n = e.format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 4
                 : e.format == DXGI_FORMAT_R32G32B32_FLOAT  ? 3
                 : e.format == DXGI_FORMAT_R32G32_FLOAT     ? 2
                                                            : 0;
    const gpu::VertexBuffer& b = a.buffers[e.buffer];
    if (!n || !b.src || !b.stride) return false;
    lo = 1e30f;
    hi = -1e30f;
    for (uint32_t i = 0; i < a.vertex_count; i++) {
        size_t at = size_t(i) * b.stride + e.offset;
        if (at + n * 4 > b.bytes) break;
        float v[4] = {0, 0, 0, 1};
        std::memcpy(v, b.src + at, n * 4);
        float x = v[0];
        if (!p.passthrough) {
            // w is 1: the draw is flat (c[matrix + 3] is 0 0 0 1)
            const float* m = vsc[p.matrix];
            x = v[0] * m[0] + v[1] * m[1] + v[2] * m[2] + v[3] * m[3];
            if (p.scale >= 0 && p.scale < 192) x *= vsc[p.scale][0];
            if (p.offset >= 0 && p.offset < 192) x += vsc[p.offset][0];
        }
        if (!std::isfinite(x)) return false;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    return lo <= hi;
}

class Hud : public gpu::Extension {
public:
    bool Bound(gpu::Context& c, const gpu::State&, gpu::Target*, gpu::Target*, bool) override {
        c.GuestSize(frame_w_, frame_h_);
        return false;
    }

    void BeginDraw(const gpu::State&) override { masked_ = movie_ = false; }

    // a texture with an opaque black border: a full screen mask like the sniper scope's, whose corners are on the
    // screen's edges; drawn in texture mode its middle keeps its shape and the border fills the sides
    void Decoded(const gpu::TextureDesc& d, const gpu::FormatInfo& f, uint32_t face, uint32_t level,
                 const std::vector<uint8_t>& px, uint32_t pitch, uint8_t& flags) override {
        if (face || level || f.host != gpu::Host::BGRA8 || d.width < 16 || d.height < 16) return;
        auto black = [&](uint32_t x, uint32_t y) {
            const uint8_t* t = px.data() + size_t(y) * pitch + size_t(x) * 4;
            return t[0] < 8 && t[1] < 8 && t[2] < 8 && t[3] > 247;
        };
        for (uint32_t x = 0; x < d.width; x++)
            if (!black(x, 0) || !black(x, d.height - 1)) return;
        for (uint32_t y = 0; y < d.height; y++)
            if (!black(0, y) || !black(d.width - 1, y)) return;
        flags |= kMaskTexture;
    }

    void Sampled(const gpu::TextureDesc& d, const gpu::Target* t, uint8_t flags, gpu::TexInfo&) override {
        masked_ = masked_ || (flags & kMaskTexture);
        uint32_t movie = g_movie.load(std::memory_order_relaxed);
        if (!t && movie && d.width == (movie >> 16) && d.height == (movie & 0xFFFF)) {
            movie_ = true;
            if (movie != logged_) {
                logged_ = movie;
                LOGR("widescreen: movie %ux%u drawn for the %.2f frame", d.width, d.height, app::FrameAspect());
            }
        }
    }

    void ScreenX(const gpu::State& s, const gpu::Assembled& a, const float (*vsc)[4], const gpu::Target* rt,
                 float* screen) override {
        // the frame's own target with a depth test that always passes, as the HUD and menus have it: no 3D effect
        // projected on the CPU
        bool depth = s.rs[gpu::kRsZEnable] && s.simple[gpu::kMZFunc >> 2] != kZAlways;
        if (!app::Widescreen() || movie_ || !rt || rt->gw != frame_w_ || rt->gh != frame_h_ || depth) return;
        if (!s.vs || !(s.vs_handle & 1)) return;
        const Placement& p = Lookup(*s.vs);
        bool flat = p.passthrough;
        if (p.matrix >= 0 && p.matrix + 3 < 192) {
            const float* w = vsc[p.matrix + 3];
            flat = std::fabs(w[0]) < 1e-6f && std::fabs(w[1]) < 1e-6f && std::fabs(w[2]) < 1e-6f &&
                   std::fabs(w[3] - 1.0f) < 1e-6f;
        }
        if (!flat) return;
        float widen = FrameWidening();
        screen[0] = 1.0f / widen;
        screen[1] = float(rt->gw) / 2;
        screen[2] = masked_ ? 1.0f : 0.0f;
        // the gameplay HUD's pieces placed by HUD Position and HUD Offset; the default (centre, 0) is the middle
        if (s.tag != kHudTag) return;
        bool edge = std::atomic_ref<bool>(Opt().hud_edge).load(std::memory_order_relaxed);
        uint32_t offset = std::atomic_ref<uint32_t>(Opt().hud_offset).load(std::memory_order_relaxed);
        float lo, hi;
        if ((edge || offset) && DrawX(a, p, vsc, lo, hi))
            screen[1] = HudCenter(lo, hi, float(rt->gw), widen, edge, offset);
    }

private:
    const Placement& Lookup(const gpu::VertexShader& vs) {
        uint64_t key = vs.hash ? vs.hash : gpu::Hash(vs.program.data(), vs.program.size() * 4) | 1;
        auto it = programs_.find(key);
        if (it == programs_.end()) it = programs_.emplace(key, Classify(vs.program)).first;
        return it->second;
    }

    static constexpr uint8_t kMaskTexture = 1;  // the extension's bit of the texture flags
    bool masked_ = false;  // the draw samples a kMaskTexture
    bool movie_ = false;   // the draw samples the playing movie's frame
    uint32_t logged_ = 0;
    uint32_t frame_w_ = 0, frame_h_ = 0;
    std::unordered_map<uint64_t, Placement> programs_;  // render thread only
};

}  // namespace

float FrameWidening() { return app::FrameAspect() / (4.0f / 3.0f); }

void WidenView(uint32_t camera) {
    if (camera && app::Widescreen()) G<float>(camera + kTanAcross) *= FrameWidening();
}

void MovieScale(X86Context& ctx) {
    // [esp+0x14] the x scale, [esp+0x20] the y scale (both the smaller fit), [esp+0x18]/[esp+0x1C] the screen and
    // [esp+0x24]/[esp+0x2C] the movie's width and height, as floats; the position is worked out from them after
    float screen_w = G<float>(ctx.esp + 0x18), screen_h = G<float>(ctx.esp + 0x1C);
    float movie_w = G<float>(ctx.esp + 0x24), movie_h = G<float>(ctx.esp + 0x2C);
    if (!(movie_w >= 1 && movie_h >= 1 && movie_w < 65536 && movie_h < 65536 && screen_w > 0 && screen_h > 0)) return;
    float widen = FrameWidening();
    float scale_x, scale_y;
    MovieFit(screen_w, screen_h, movie_w, movie_h, widen, scale_x, scale_y);
    G<float>(ctx.esp + 0x14) = scale_x;
    G<float>(ctx.esp + 0x20) = scale_y;
    g_movie = uint32_t(movie_w) << 16 | uint32_t(movie_h);
}

void MovieEnded() { g_movie = 0; }

void StartWidescreen() { gpu::SetExtension(std::make_unique<Hud>()); }

void DrawHud(X86Context& ctx, uint8_t* base, void (*original)(X86Context&, uint8_t*)) {
    // the title's thread, which makes the draws: they carry the tag to the render thread with the rest of the state
    auto tag = [](uint32_t t) {
        std::lock_guard<d3d::DeviceLock> lock(d3d::Lock());
        d3d::State().tag = t;
    };
    tag(kHudTag);
    original(ctx, base);
    tag(0);
}

}  // namespace game
