#include "common/frame_timing.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

#include "common/frame_clock.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace game {
namespace {
using Clock = std::chrono::steady_clock;
thread_local FrameClock clock;
thread_local TickRemainder mission, menu, popup_menu;
thread_local KeyedTickRemainder brains, lights;
thread_local LoadingAnimationClock loading, popup;
bool Diagnostics() {
    static bool on = std::getenv("MOHRS_TIMING") != nullptr;
    return on;
}
// MOHRS_TIMING=1 reports each second; a number below 1 is the report interval in seconds
double ReportSeconds() {
    static double s = [] {
        const char* v = std::getenv("MOHRS_TIMING");
        double d = v ? std::atof(v) : 1;
        return d > 0 && d < 1 ? d : 1.0;
    }();
    return s;
}
thread_local uint32_t player_calls = 0, frames = 0;
thread_local double player_ticks = 0, frame_ticks = 0;
thread_local Clock::time_point report = Clock::now();

void Animate(X86Context& ctx, LoadingAnimationClock& animation, const char* name) {
    if (!Opt().unlocked_fps && !Opt().fps60) return;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    bool first = !animation.started;
    ctx.xmm[0].f[0] = animation.Sample(ns);
    if (first && Diagnostics()) LOGR("timing: %s stars use a 1.1 second cycle (30 phase steps/s)", name);
}
}  // namespace

void FrameDelta(X86Context& ctx) {
    if (!Opt().unlocked_fps) {
        clock = {};
        mission = menu = popup_menu = {};
        return;
    }
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    ctx.xmm[0].f[0] = clock.Advance(ns);
}

void MissionTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.eax = mission.Advance(ctx.xmm[0].f[0]);
}

void MenuTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.eax = menu.Advance(ctx.xmm[0].f[0]);
}

// the profile/save popup's own FEng package (0xF68A0 -> 0x95220), updated separately from the menu manager
void PopupTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.eax = popup_menu.Advance(ctx.xmm[0].f[0]);
}

void MessageTicks(X86Context& ctx) {
    if (Opt().unlocked_fps) ctx.xmm[3].f[0] = ctx.xmm[2].f[0];
}

// Both apply in the 60 FPS mode too, where a slowed-down update (0xA4F30 scales the delta) also falls below one tick;
// the original 30 FPS mode keeps its console behavior.
void BrainTicks(X86Context& ctx) {
    if (!Opt().unlocked_fps && !Opt().fps60) return;
    ctx.eax = brains.Advance(ctx.ecx, kernel::G<float>(ctx.esp + 4));
}

void LightTicks(X86Context& ctx) {
    if (!Opt().unlocked_fps && !Opt().fps60) return;
    ctx.eax = lights.Advance(ctx.ecx, kernel::G<float>(ctx.ebp + 8));
}

void ThreatDecay(X86Context& ctx) {
    if (!Opt().unlocked_fps && !Opt().fps60) return;
    ctx.xmm[2].f[0] = PerConsoleFrame(ctx.xmm[2].f[0], kernel::G<float>(ctx.esp + 4));
}

namespace {
thread_local uint32_t floor_world = 0;
thread_local double script_total = 0, script_ran = 0;
thread_local int64_t script_frames = 0;
}

namespace {
thread_local double entity_carry = 0;
}

bool EntityStep(X86Context& ctx) {
    if (!Opt().unlocked_fps && !Opt().fps60) {
        entity_carry = 0;
        return true;
    }
    float& dt = kernel::G<float>(ctx.esp + 4);
    if (!(dt > 0)) return true;  // paused: the manager's own path
    constexpr double kShortest = 1.0 / 64;  // 0x1EAA2C
    double total = entity_carry + dt;
    if (total < kShortest) {
        entity_carry = total;
        return false;
    }
    dt = float(total);
    entity_carry = 0;
    return true;
}

namespace {
// per gameplay frame on the game thread; a post from any other thread is always in frame 0 and never repeats
thread_local RepeatedPosts posts;
thread_local bool posts_on = false;
}

bool ScriptFrame(X86Context& ctx) {
    static const bool off = std::getenv("MOHRS_SCRIPT_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) {
        script_total = script_ran = 0;
        script_frames = 0;
        posts = {};
        posts_on = false;
        return true;
    }
    posts_on = true;
    posts.NextFrame();
    float& dt = kernel::G<float>(ctx.esp + 4);
    script_total += std::max(0.0f, dt);
    // console frames are two ticks; run once each time the total enters a new one, with all the time since the last run
    auto frame = int64_t(script_total / 2);
    if (frame <= script_frames) return false;
    script_frames = frame;
    dt = float(script_total - script_ran);
    script_ran = script_total;
    posts.Drained();  // the update drains the queue first
    return true;
}

void QueuePost(X86Context& ctx, uint8_t* base, QueueFunction original, uint32_t free_list) {
    uint32_t target = kernel::R32(ctx.esp + 4);
    uint32_t& id = kernel::G<uint32_t>(target + 0x24);
    if (id == 0xFFFFFFFFu) {  // gone: the function drops it itself
        original(ctx, base);
        return;
    }
    QueuedPost post{target,
                    kernel::R32(ctx.esp + 8),
                    kernel::R32(ctx.esp + 12),
                    kernel::R32(ctx.esp + 16) & 0xFFFF,
                    kernel::R32(ctx.esp + 20),
                    kernel::R32(ctx.esp + 24),
                    kernel::R32(ctx.esp + 28) & 0xFFFF};
    bool drop = false;
    if (!kernel::R32(free_list)) {
        drop = true;
        static int logged = 0;
        if (logged < 4) {
            logged++;
            LOGE("script messages: queue full, message %X to %08X dropped", post.message, target);
        }
    } else {
        drop = posts_on && posts.Repeat(post);
    }
    if (!drop) {
        original(ctx, base);
        return;
    }
    uint32_t saved = id;
    id = 0xFFFFFFFFu;
    original(ctx, base);  // releases a pooled argument object and returns 0xE, as for a gone target
    id = saved;
}

namespace {
thread_local std::unordered_map<uint32_t, BotThinkClock> bot_clocks;  // by bot
thread_local float bot_time = 0;
bool BotEveryFrame() {
    static const bool off = std::getenv("MOHRS_BOT_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    return off || (!Opt().unlocked_fps && !Opt().fps60);
}
}

void BotThink(X86Context& ctx, uint32_t frame_counter) {
    if (BotEveryFrame()) {
        bot_clocks.clear();
        return;
    }
    if (bot_clocks.size() > 256) bot_clocks.clear();  // bots are made once per match; a new match starts over
    uint32_t bot = ctx.esi;
    float ticks = kernel::G<float>(ctx.esp + 0x10);  // the update's time, below three pushes
    bool due = bot_clocks[bot].Due(ticks, kernel::R32(bot + 0x1938) & 1, bot_time);
    // the next instruction XORs ECX with the frame counter and tests bit 0: set means an AI update
    ctx.ecx = kernel::R32(frame_counter) ^ (due ? 1u : 0u);
}

void BotThinkTime(X86Context& ctx) {
    if (!BotEveryFrame()) ctx.xmm[0].f[0] = bot_time;
}

namespace {
struct ShakeClock {
    double total = 0, ran = 0;
    int64_t frames = 0;
};
thread_local std::unordered_map<uint32_t, ShakeClock> shakes;  // by player
}

bool ShakeFrame(X86Context& ctx) {
    static const bool off = std::getenv("MOHRS_SHAKE_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) {
        shakes.clear();
        return true;
    }
    float& dt = kernel::G<float>(ctx.esp + 12);
    ShakeClock& c = shakes[ctx.ecx];
    c.total += std::max(0.0f, dt);
    // as ScriptFrame: once per two-tick console frame, with all the time since this player's last run
    auto frame = int64_t(c.total / 2);
    if (frame <= c.frames) return false;
    c.frames = frame;
    dt = float(c.total - c.ran);
    c.ran = c.total;
    return true;
}

namespace {
struct KickClock {
    double total = 0;
    int64_t frames = 0;
    float run = 0;  // the time of the run in progress
};
thread_local std::unordered_map<uint32_t, KickClock> kicks;  // by player
}

bool MountedKickSkip(const X86Context& ctx) {
    static const bool off = std::getenv("MOHRS_SHAKE_EVERY_FRAME") != nullptr;
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) {
        kicks.clear();
        return false;
    }
    KickClock& c = kicks[ctx.esi];
    c.total += std::max(0.0f, kernel::G<float>(ctx.ebp + 8));
    auto frame = int64_t(c.total / 2);
    if (frame <= c.frames) return true;
    // whole console frames of two ticks: with a little less the oscillator would miss a flip and the gun drift
    c.run = float(2 * std::min<int64_t>(frame - c.frames, 4));
    c.frames = frame;
    return false;
}

void MountedKickStep(X86Context& ctx, float step) {
    auto c = kicks.find(ctx.esi);
    if (c != kicks.end()) ctx.xmm[0].f[0] = step * c->second.run;
}

bool MuzzlePuffFrame(uint32_t weapon) {
    static const bool off = std::getenv("MOHRS_PUFF_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) return true;
    static std::unordered_map<uint32_t, int64_t> last;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto frame = int64_t(std::chrono::duration<double>(now).count() * 30);
    auto it = last.find(weapon);
    if (it != last.end() && it->second == frame) return false;
    last[weapon] = frame;
    return true;
}

namespace {
thread_local float weapon_time[8];
thread_local int weapon_depth = 0;
}

void WeaponUpdate(const X86Context& ctx) {
    if (weapon_depth < int(std::size(weapon_time))) weapon_time[weapon_depth] = kernel::G<float>(ctx.esp + 4);
    weapon_depth++;
}

void WeaponUpdateDone() {
    if (weapon_depth > 0) weapon_depth--;
}

void RefireTime(float& refire) {
    static const bool off = std::getenv("MOHRS_REFIRE_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) return;
    if (weapon_depth < 1 || weapon_depth > int(std::size(weapon_time))) return;
    float dt = weapon_time[weapon_depth - 1];
    // the console's firing frame took two ticks the timer never counted; this frame took dt. Slower frames than the
    // console's keep the game's own timing.
    if (dt >= 0 && dt < 2) refire += 2 - dt;
}

void ParticleDrag(float& keep, float seconds) {
    static const bool off = std::getenv("MOHRS_PARTICLE_DRAG_EVERY_FRAME") != nullptr;  // test runs: the old behavior
    if (off || (!Opt().unlocked_fps && !Opt().fps60)) return;
    if (!(keep > 0) || !(seconds > 0)) return;  // no drag base to scale (a full stop or worse): as the console
    keep = float(std::pow(double(keep), double(seconds) * 30.0));
}

void FloorRayWorld(const X86Context& ctx) { floor_world = ctx.edi; }

void FloorRayRetry(X86Context& ctx, const FloorRaySite& s) {
    if (ctx.edi != 1 || !floor_world || (!Opt().unlocked_fps && !Opt().fps60)) return;
    // a seam is narrow: the nearest offsets first, along and across the floor
    static constexpr float kOffsets[][2] = {{0.05f, 0}, {-0.05f, 0}, {0, 0.05f},  {0, -0.05f},
                                            {0.15f, 0}, {-0.15f, 0}, {0, 0.15f},  {0, -0.15f}};
    uint32_t in = ctx.esp + s.input, segment = ctx.esp + s.segment, hit = ctx.esp + s.hit;
    // the input holds the start (x y z w) and, 0x10 on, the end of the ray
    float x = kernel::G<float>(in), y = kernel::G<float>(in + 4);
    uint32_t ecx = ctx.ecx, cast = kernel::R32(kernel::R32(floor_world) + 0x24);
    for (const auto& o : kOffsets) {
        kernel::G<float>(in) = kernel::G<float>(in + 0x10) = x + o[0];
        kernel::G<float>(in + 4) = kernel::G<float>(in + 0x14) = y + o[1];
        ctx.ecx = segment;
        kernel::CallGuest(ctx, s.make_segment, {in});
        ctx.ecx = hit;
        kernel::CallGuest(ctx, s.free_hit, {});
        kernel::CallGuest(ctx, s.init_hit, {});
        kernel::CallGuest(ctx, s.flag, {1});
        ctx.ecx = floor_world;
        uint32_t result = kernel::CallGuest(ctx, cast, {segment, hit, 0});
        kernel::CallGuest(ctx, s.flag, {0});
        if (result != 1) {
            if (Diagnostics())
                LOGR("timing: floor ray missed at %.3f %.3f, floor found %+.2f %+.2f from it", x, y, o[0], o[1]);
            ctx.edi = result;
            break;
        }
    }
    kernel::G<float>(in) = kernel::G<float>(in + 0x10) = x;
    kernel::G<float>(in + 4) = kernel::G<float>(in + 0x14) = y;
    ctx.ecx = ecx;
}

// equal frames reach the wrap branch only when the scan's started flag (+0xE4) is clear: the original 30 FPS steps
// always move the first scan a frame on, so this is the forward scan the console ran there
bool AnimEventFirstScan(const X86Context& ctx) { return ctx.ebx == ctx.edx; }

void ResetLoadingAnimation() { loading = {}; }

void LoadingAnimation(X86Context& ctx) {
    Animate(ctx, loading, "loading");
}

void ResetPopupAnimation() { popup = {}; }
void PopupAnimation(X86Context& ctx) { Animate(ctx, popup, "profile/save popup"); }

void PlayerTick(float delta) {
    if (!Diagnostics()) return;
    player_calls++;
    player_ticks += delta;
}

namespace {
// MOHRS_ENTITY_DUMP=<seconds>[:<radius>]: once, that long after the first frame, every entity of the manager at
// 0x270630 within radius (8) units of the player, with its vtable and first words (test runs)
void DumpEntities(uint32_t players) {
    // several times: MOHRS_ENTITY_DUMP=<seconds>[,<seconds>...][:<radius>]
    static double radius = 8;
    static std::vector<double> times = [] {
        std::vector<double> out;
        const char* v = std::getenv("MOHRS_ENTITY_DUMP");
        if (!v) return out;
        if (const char* c = std::strchr(v, ':')) radius = std::atof(c + 1);
        for (const char* p = v; p && *p && *p != ':';) {
            out.push_back(std::atof(p));
            p = std::strpbrk(p, ",:");
            if (p && *p == ',') p++;
            else break;
        }
        return out;
    }();
    static const Clock::time_point first = Clock::now();
    if (times.empty() || std::chrono::duration<double>(Clock::now() - first).count() < times.front()) return;
    times.erase(times.begin());
    uint32_t p = kernel::R32(players);
    if (!p) return;
    float px = kernel::G<float>(p + 0x80), py = kernel::G<float>(p + 0x84), pz = kernel::G<float>(p + 0x88);
    LOGR("entities: player %08X at %.3f %.3f %.3f", p, px, py, pz);
    // MOHRS_DUMP=<addr>:<bytes>[,...] (hex): raw dwords at the same moment
    for (const char* d = std::getenv("MOHRS_DUMP"); d && *d;) {
        char* end = nullptr;
        uint32_t at = uint32_t(std::strtoul(d, &end, 16));
        if (*end != ':') break;
        uint32_t bytes = uint32_t(std::strtoul(end + 1, &end, 16));
        for (uint32_t o = 0; o < bytes; o += 0x20) {
            std::string words;
            for (uint32_t w = 0; w < 0x20 && o + w < bytes; w += 4) {
                char s[12];
                std::snprintf(s, sizeof s, " %08X", kernel::R32(at + o + w));
                words += s;
            }
            LOGR("dump %08X +%04X:%s", at, o, words.c_str());
        }
        d = *end == ',' ? end + 1 : nullptr;
    }
    for (uint32_t field : {0x44u, 0x40u, 0x3Cu}) {
        uint32_t o = kernel::R32(p + field);
        LOGR("player +%02X = %08X vt %08X", field, o, o ? kernel::R32(o) : 0);
    }
    // the player's contact lists (0x1432E0 from 0xD8260: this = player + 0x290, entries of 16 bytes)
    for (uint32_t list : {0x04u, 0x1Cu}) {
        uint32_t base = kernel::R32(p + 0x290 + list), n = kernel::R32(p + 0x290 + list + 8);
        LOGR("contacts +%02X: %u at %08X", list, n, base);
        for (uint32_t i = 0; i < n && i < 64; i++) {
            uint32_t c = base + i * 16;
            LOGR("  %2u: %08X %08X %08X %08X = %.4f %.4f %.4f %.4f", i, kernel::R32(c), kernel::R32(c + 4),
                 kernel::R32(c + 8), kernel::R32(c + 12), kernel::G<float>(c), kernel::G<float>(c + 4),
                 kernel::G<float>(c + 8), kernel::G<float>(c + 12));
        }
    }
    // the entity manager's contact pairs (0xFAB00: pointer +0x138, count +0x130, 0x24 bytes, the two objects first)
    uint32_t pairs = kernel::R32(0x270630 + 0x138), npairs = kernel::R32(0x270630 + 0x130);
    LOGR("pairs: %u at %08X", npairs, pairs);
    for (uint32_t i = 0; i < npairs && i < 256; i++) {
        uint32_t q = pairs + i * 0x24;
        uint32_t a = kernel::R32(q), b = kernel::R32(q + 4);
        std::string words;
        for (uint32_t w = 0; w < 0x24; w += 4) {
            char s[12];
            std::snprintf(s, sizeof s, " %08X", kernel::R32(q + w));
            words += s;
        }
        uint32_t va = a ? kernel::R32(a) : 0, vb = b ? kernel::R32(b) : 0;
        LOGR("  pair %u: vt %08X / %08X:%s", i, va, vb, words.c_str());
        for (uint32_t o : {a, b}) {
            if (!o || o == p) continue;
            LOGR("    %08X at %.3f %.3f %.3f:", o, kernel::G<float>(o + 0x80), kernel::G<float>(o + 0x84),
                 kernel::G<float>(o + 0x88));
        }
    }
    uint32_t count = 0;
    for (uint32_t e = kernel::R32(0x270630); e && count < 4096; e = kernel::R32(e + 0x1C), count++) {
        float x = kernel::G<float>(e + 0x80), y = kernel::G<float>(e + 0x84), z = kernel::G<float>(e + 0x88);
        float dx = x - px, dy = y - py, dz = z - pz;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(d < radius)) continue;
        std::string words;
        for (uint32_t i = 0; i < 0x80; i += 4) {
            char w[12];
            std::snprintf(w, sizeof w, " %08X", kernel::R32(e + i));
            words += w;
        }
        LOGR("entity %08X vt %08X at %.3f %.3f %.3f dist %.3f:%s", e, kernel::R32(e), x, y, z, d, words.c_str());
    }
    LOGR("entities: %u in the list", count);
}
// MOHRS_WATCH=<start>-<end>:<addr>:<bytes>[,<addr>:<bytes>...] (seconds after the first frame, hex addresses): each
// dword's changes over the window, logged once at its end: how many frames changed it, and its total change as an
// integer and as a float. A field counted per frame changes by frames, one counted in time by elapsed time.
void Watch(float delta) {
    struct Range {
        uint32_t at, bytes;
        std::vector<uint32_t> last;
        std::vector<uint32_t> changes;
        std::vector<int64_t> ints;
        std::vector<double> floats;
    };
    static double from = -1, to = -1;
    static std::vector<Range> ranges = [] {
        std::vector<Range> out;
        const char* v = std::getenv("MOHRS_WATCH");
        if (!v || std::sscanf(v, "%lf-%lf", &from, &to) != 2) return out;
        const char* p = std::strchr(v, ':');
        while (p && *p) {
            char* end = nullptr;
            Range r;
            r.at = uint32_t(std::strtoul(p + 1, &end, 16));
            if (*end != ':') break;
            r.bytes = uint32_t(std::strtoul(end + 1, &end, 16)) & ~3u;
            out.push_back(std::move(r));
            p = *end == ',' ? end : nullptr;
        }
        return out;
    }();
    static const Clock::time_point first = Clock::now();
    static uint32_t frames = 0;
    static double ticks = 0;
    static bool done = false;
    if (ranges.empty() || done) return;
    double now = std::chrono::duration<double>(Clock::now() - first).count();
    if (now < from) return;
    if (now > to) {
        done = true;
        LOGR("watch: %u frames, %.3f ticks over %.2f s", frames, ticks, to - from);
        for (Range& r : ranges)
            for (uint32_t i = 0; i < r.bytes / 4; i++)
                if (r.changes.size() && r.changes[i] >= frames / 4)
                    LOGR("watch: %08X changed %u times, int %+lld, float %+.4f, now %08X", r.at + i * 4, r.changes[i],
                         (long long)r.ints[i], r.floats[i], r.last[i]);
        return;
    }
    frames++;
    ticks += delta;
    // MOHRS_SERIES=<addr>:<dwords>[,...] (hex): those floats every frame of the window, up to 16 per range
    static const std::vector<std::pair<uint32_t, uint32_t>> series = [] {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        for (const char* v = std::getenv("MOHRS_SERIES"); v && *v;) {
            char* end = nullptr;
            uint32_t at = uint32_t(std::strtoul(v, &end, 16));
            if (*end != ':') break;
            out.push_back({at, std::min<uint32_t>(uint32_t(std::strtoul(end + 1, &end, 16)), 16)});
            v = *end == ',' ? end + 1 : nullptr;
        }
        return out;
    }();
    if (!series.empty()) {
        std::string values;
        for (const auto& [at, n] : series)
            for (uint32_t i = 0; i < n; i++) {
                char s[24];
                std::snprintf(s, sizeof s, " %.7g", kernel::G<float>(at + i * 4));
                values += s;
            }
        LOGR("series %.4f dt %.3f:%s", now, delta, values.c_str());
    }
    for (Range& r : ranges) {
        uint32_t n = r.bytes / 4;
        if (r.last.empty()) {
            r.last.resize(n);
            r.changes.assign(n, 0);
            r.ints.assign(n, 0);
            r.floats.assign(n, 0);
            for (uint32_t i = 0; i < n; i++) r.last[i] = kernel::R32(r.at + i * 4);
            continue;
        }
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v = kernel::R32(r.at + i * 4);
            if (v == r.last[i]) continue;
            r.changes[i]++;
            r.ints[i] += int64_t(int32_t(v)) - int64_t(int32_t(r.last[i]));
            float a, b;
            std::memcpy(&a, &v, 4);
            std::memcpy(&b, &r.last[i], 4);
            if (std::isfinite(a) && std::isfinite(b)) r.floats[i] += double(a) - double(b);
            r.last[i] = v;
        }
    }
}
}  // namespace

void TimingFrame(float delta, uint32_t players, uint32_t state) {
    if (!Diagnostics()) return;
    DumpEntities(players);
    Watch(delta);
    frames++;
    frame_ticks += delta;
    auto now = Clock::now();
    double seconds = std::chrono::duration<double>(now - report).count();
    if (seconds < ReportSeconds()) return;
    uint32_t p = kernel::R32(players), save = kernel::R32(state);
    LOGR("timing: %.1f frames/s, %.3f frame ticks/s; %.1f player updates/s, %.3f player ticks/s; "
         "mission ticks %u; player %08X xyz %.3f %.3f %.3f", frames / seconds, frame_ticks / seconds,
         player_calls / seconds, player_ticks / seconds, save ? kernel::R32(save + 0x31760) : 0, p,
         p ? kernel::G<float>(p + 0x80) : 0, p ? kernel::G<float>(p + 0x84) : 0,
         p ? kernel::G<float>(p + 0x88) : 0);
    report = now;
    frames = player_calls = 0;
    frame_ticks = player_ticks = 0;
}

}  // namespace game
