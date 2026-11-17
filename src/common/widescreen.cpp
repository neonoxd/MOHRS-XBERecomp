#include "common/widescreen.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "gpu/ext.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "core/log.h"
#include "kernel/kernel.h"

using kernel::G;

namespace game {

namespace {

constexpr uint32_t kTanAcross = 0x40;
constexpr uint32_t kZAlways = 0x207;  // NV2A depth function

// the playing movie's frame size (0 none), which its texture has: its draw is placed for the widened frame already
std::atomic<uint32_t> g_movie{0};

// how a vertex program places its vertices: the game's 2D is drawn in screen pixels, either passed through or through
// an orthographic matrix (the 2D renderer's, 0x17E780, and the sprite batches'), its 3D through a perspective one
struct Placement {
    bool passthrough = false;  // oPos = v0
    int matrix = -1;           // oPos.x = dot(v0, c[matrix]) and on through c[matrix + 3] for w
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
    return p;
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

    void ScreenX(const gpu::State& s, const float (*vsc)[4], const gpu::Target* rt, float* screen) override {
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
        screen[0] = 1.0f / FrameWidening();
        screen[1] = float(rt->gw) / 2;
        screen[2] = masked_ ? 1.0f : 0.0f;
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

}  // namespace game
