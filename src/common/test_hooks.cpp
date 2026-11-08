#include "common/test_hooks.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

using kernel::R32;
using kernel::W32;

namespace game {

namespace {

std::string GuestString(uint32_t va, size_t max = 64) {
    std::string s;
    for (char c; s.size() < max && (c = char(kernel::g_base[va])) != 0; va++) s += c;
    return s;
}

}  // namespace

void PickStartLevel(X86Context& ctx) {
    static bool first = true;
    if (!first) return;
    first = false;
    int mission = 0, level = 0;
    const char* v = std::getenv("MOHRS_START_LEVEL");
    // thiscall: mission and level are the stack arguments after the return address
    if (v && std::sscanf(v, "%d_%d", &mission, &level) == 2 && R32(ctx.esp + 4) == 1 && R32(ctx.esp + 8) == 1) {
        LOGR("MOHRS_START_LEVEL: starting %d_%d instead of 1_1", mission, level);
        W32(ctx.esp + 4, uint32_t(mission));
        W32(ctx.esp + 8, uint32_t(level));
    }
}

void SkipMovie(uint32_t key, uint32_t buffer, uint32_t size) {
    const Options& o = Opt();
    if (size < 5 || (!o.skip_intros && !o.skip_cutscenes)) return;
    std::string k = GuestString(key);
    bool intro = k == "legal_movie" || k == "legal_intro_movie";
    bool cutscene = k == "intro_movie" || k == "outro_movie";
    if ((intro && o.skip_intros) || (cutscene && o.skip_cutscenes)) {
        LOG_FIRST(32, "movies: skipping %s %s", k.c_str(), GuestString(buffer).c_str());
        std::memcpy(kernel::g_base + buffer, "none", 5);
    }
}

void FrameWait(uint32_t& blanks) {
    if (Opt().unlocked_fps && blanks < 2) blanks = 2;
    else if (Opt().fps60 && blanks == 1) blanks = 2;
}

void TraceAnimEvent(const X86Context& ctx) {
    static const bool on = std::getenv("MOHRS_EVENT_TRACE") != nullptr;
    if (!on) return;
    uint32_t record = R32(ctx.esp + 4);
    // return addresses further up the guest stack, code section values
    std::string up;
    for (uint32_t at = ctx.esp + 4, n = 0; at < ctx.esp + 0x400 && n < 8; at += 4) {
        uint32_t v = R32(at);
        if (v < 0x11000 || v >= 0x1BB7DC) continue;
        char text[12];
        std::snprintf(text, sizeof(text), " %06X", v);
        up += text;
        n++;
    }
    LOGR("event: opcode %04X arg %08X record %08X listener %08X from %08X%s", kernel::R16(record + 2),
         R32(record + 4), record, ctx.ecx, R32(ctx.esp), up.c_str());
}

void TraceAnim(const X86Context& ctx) {
    static const std::vector<uint32_t> tables = [] {
        std::vector<uint32_t> out;
        for (const char* p = std::getenv("MOHRS_ANIM_TRACE"); p && *p;) {
            char* end = nullptr;
            out.push_back(uint32_t(std::strtoul(p, &end, 16)));
            if (end == p) break;
            p = *end == ',' ? end + 1 : end;
        }
        return out;
    }();
    static uint32_t lines = 0;
    static std::map<uint32_t, uint32_t> seen;
    if (tables.empty() || lines > 60000) return;
    uint32_t c = ctx.ecx, table = R32(c + 0xDC);
    if (std::find(tables.begin(), tables.end(), table) == tables.end()) return;
    lines++;
    if (seen[c] != table) {
        seen[c] = table;
        std::string words, events;
        for (uint32_t w = 0; w < 0x60; w += 4) {
            char s[12];
            std::snprintf(s, sizeof s, " %08X", R32(c + w));
            words += s;
        }
        for (uint32_t i = 0; i < R32(c + 0xD8) && i < 24; i++) {
            char s[24];
            std::snprintf(s, sizeof s, " %u:%04X/%X", kernel::R16(table + i * 12), kernel::R16(table + i * 12 + 2),
                          kernel::R16(table + i * 12 + 4));
            events += s;
        }
        LOGR("anim: channel %08X listener field %08X table %08X events%s; words%s", c, R32(c + 0x2C), table,
             events.c_str(), words.c_str());
    }
    LOGR("anim: channel %08X table %08X t %.4f scanned %.4f started %u +3C %.4f +40 %.4f +44 %.4f +48 %u", c, table,
         kernel::G<float>(c + 0x38), kernel::G<float>(c + 0xE0), kernel::G<uint8_t>(c + 0xE4),
         kernel::G<float>(c + 0x3C), kernel::G<float>(c + 0x40), kernel::G<float>(c + 0x44), R32(c + 0x48));
}

void TraceAnimStart(const X86Context& ctx) {
    static const char* v = std::getenv("MOHRS_ANIM_TRACE");
    static uint32_t lines = 0;
    if (!v || lines > 4000) return;
    uint32_t table = R32(ctx.esp + 8);
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08X", table);
    if (!std::strstr(v, hex)) return;
    lines++;
    std::string up;
    for (uint32_t at = ctx.esp, n = 0; at < ctx.esp + 0x600 && n < 14; at += 4) {
        uint32_t w = R32(at);
        if (w < 0x11000 || w >= 0x1BB7DC) continue;
        char s[12];
        std::snprintf(s, sizeof s, " %06X", w);
        up += s;
        n++;
    }
    LOGR("anim start: channel %08X table %08X from%s", ctx.ecx, table, up.c_str());
}

void TraceGraphSet(const X86Context& ctx) {
    static const bool on = std::getenv("MOHRS_GRAPH_TRACE") != nullptr;
    static uint32_t lines = 0;
    if (!on || lines > 200000) return;
    lines++;
    char name[64] = {};
    uint32_t text = R32(ctx.esp + 4);
    for (uint32_t i = 0; i + 1 < sizeof name && text; i++) {
        char c = char(kernel::G<uint8_t>(text + i));
        if (!c) break;
        name[i] = c;
    }
    LOGR("graph: %08X %s = %08X (%08X %08X) from %06X", ctx.ecx, name, R32(ctx.esp + 8), R32(ctx.esp + 12),
         R32(ctx.esp + 16), R32(ctx.esp));
}

void TraceGraphPlay(const X86Context& ctx) {
    static const bool on = std::getenv("MOHRS_GRAPH_TRACE") != nullptr;
    static uint32_t lines = 0;
    if (!on || lines > 200000) return;
    lines++;
    std::string up;
    for (uint32_t at = ctx.esp, n = 0; at < ctx.esp + 0x200 && n < 8; at += 4) {
        uint32_t w = R32(at);
        if (w < 0x11000 || w >= 0x1BB7DC) continue;
        char s[12];
        std::snprintf(s, sizeof s, " %06X", w);
        up += s;
        n++;
    }
    LOGR("play: %08X state %08X arg %08X from%s", ctx.ecx, R32(ctx.esp + 4), R32(ctx.esp + 8), up.c_str());
}

void TraceNative(const X86Context& ctx) {
    static const char* v = std::getenv("MOHRS_NATIVE_TRACE");
    static const bool find = v && std::string(v) == "find";
    static const uint32_t owner = v && !find ? uint32_t(std::strtoul(v, nullptr, 16)) : 0;
    static uint32_t lines = 0;
    if (!v || lines > 100000) return;
    uint32_t who = R32(0x2557A0), fn = R32(ctx.eax + ctx.edx), index = ctx.eax >> 4;
    if (find ? fn != 0x303A0 : who != owner) return;
    lines++;
    LOGR("native: owner %08X #%u %06X pc %08X", who, index, fn, R32(0x2557BC));
}

void TraceMessage(const X86Context& ctx) {
    static const bool on = std::getenv("MOHRS_MESSAGE_TRACE") != nullptr;
    static uint32_t lines = 0;
    if (!on || lines > 400000) return;
    lines++;
    LOGR("message: to %08X msg %X arg %08X sender %08X flag %X from %06X", R32(ctx.esp + 4), R32(ctx.esp + 8) & 0xFFFF,
         R32(ctx.esp + 12), R32(ctx.esp + 16), R32(ctx.esp + 20), R32(ctx.esp));
}

void TraceContact(const X86Context& ctx) {
    static const bool on = std::getenv("MOHRS_CONTACT_TRACE") != nullptr;
    if (!on) return;
    static std::map<uint32_t, std::string> seen;
    uint32_t pair = R32(ctx.esp + 4);
    uint32_t other = R32(pair) == ctx.ecx ? R32(pair + 4) : R32(pair);
    char text[160];
    std::snprintf(text, sizeof text, "vt %08X at %.3f %.3f %.3f", other ? R32(other) : 0,
                  other ? kernel::G<float>(other + 0x80) : 0, other ? kernel::G<float>(other + 0x84) : 0,
                  other ? kernel::G<float>(other + 0x88) : 0);
    if (seen[other] == text) return;
    seen[other] = text;
    std::string words;
    for (uint32_t w = 0; w < 0x24; w += 4) {
        char s[12];
        std::snprintf(s, sizeof s, " %08X", R32(pair + w));
        words += s;
    }
    LOGR("contact: player %08X at %.3f %.3f %.3f touches %08X %s; pair%s", ctx.ecx, kernel::G<float>(ctx.ecx + 0x80),
         kernel::G<float>(ctx.ecx + 0x84), kernel::G<float>(ctx.ecx + 0x88), other, text, words.c_str());
}

void TraceStep(X86Context& ctx, bool before) {
    struct Area {
        bool on = false;
        float x = 0, y = 0, r = 0;
    };
    static const Area area = [] {
        Area a;
        if (const char* v = std::getenv("MOHRS_STEP_TRACE")) a.on = std::sscanf(v, "%f,%f,%f", &a.x, &a.y, &a.r) == 3;
        return a;
    }();
    static uint32_t player = 0, lines = 0;
    static float bx, by, bz;
    if (!area.on || lines > 40000) return;
    if (before) player = ctx.ecx;
    uint32_t p = player;
    float x = kernel::G<float>(p + 0x80), y = kernel::G<float>(p + 0x84), z = kernel::G<float>(p + 0x88);
    if (before) {
        bx = x, by = y, bz = z;
        return;
    }
    if ((bx - area.x) * (bx - area.x) + (by - area.y) * (by - area.y) > area.r * area.r) return;
    lines++;
    auto F = [&](uint32_t o) { return kernel::G<float>(p + o); };
    LOGR("step: dt %.4f %.4f %.4f %.4f -> %.4f %.4f %.4f ok %u flags %02X %02X %02X v %.4f %.4f %.4f w %.4f %.4f %.4f",
         F(0x10C8), bx, by, bz, x, y, z, ctx.eax & 0xFF, kernel::G<uint8_t>(p + 0xD4D), kernel::G<uint8_t>(p + 0xD44),
         kernel::G<uint8_t>(p + 0xD45), F(0x230), F(0x234), F(0x238), F(0x240), F(0x244), F(0x248));
}

void TraceMove(X86Context& ctx, bool before) {
    struct Area {
        bool on = false;
        float x = 0, y = 0, r = 0;
    };
    static const Area area = [] {
        Area a;
        if (const char* v = std::getenv("MOHRS_STEP_TRACE")) a.on = std::sscanf(v, "%f,%f,%f", &a.x, &a.y, &a.r) == 3;
        return a;
    }();
    static uint32_t player = 0, lines = 0;
    static float bx, by, bz, dt;
    if (!area.on || lines > 40000) return;
    if (before) player = ctx.ecx, dt = kernel::G<float>(ctx.esp + 4);
    uint32_t p = player;
    float x = kernel::G<float>(p + 0x80), y = kernel::G<float>(p + 0x84), z = kernel::G<float>(p + 0x88);
    if (before) {
        bx = x, by = y, bz = z;
        return;
    }
    if ((bx - area.x) * (bx - area.x) + (by - area.y) * (by - area.y) > area.r * area.r) return;
    lines++;
    auto F = [&](uint32_t o) { return kernel::G<float>(p + o); };
    LOGR("move: dt %.4f %.4f %.4f %.4f -> %.4f %.4f %.4f flags D44 %02X D45 %02X D46 %02X D4C %02X D4E %02X "
         "v %.4f %.4f %.4f last %.4f %.4f %.4f",
         dt, bx, by, bz, x, y, z, kernel::G<uint8_t>(p + 0xD44), kernel::G<uint8_t>(p + 0xD45),
         kernel::G<uint8_t>(p + 0xD46), kernel::G<uint8_t>(p + 0xD4C), kernel::G<uint8_t>(p + 0xD4E), F(0x230),
         F(0x234), F(0x238), F(0x260), F(0x264), F(0x268));
}

// MOHRS_HANG_TRACE=<seconds>: every that many seconds, each guest thread's stack pointer and the code addresses on its
// guest stack (return addresses, some stale), from a host thread of its own (test runs; racy by design)
void StartHangTrace() {
    static const char* v = std::getenv("MOHRS_HANG_TRACE");
    static bool started = false;
    if (!v || started) return;
    started = true;
    double every = std::max(0.5, std::atof(v));
    std::thread([every] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::duration<double>(every));
            for (const kernel::ThreadRef& th : kernel::Threads()) {
                X86Context* c = th->ctx;
                if (!c) continue;
                // only inside the thread's own stack: below its top, above its base
                uint32_t esp = c->esp, top = th->stack_top, bottom = th->stack_top - th->stack_size;
                if (esp < bottom || esp >= top) {
                    LOGR("hang: thread %X esp %08X outside its stack %08X-%08X", th->id, esp, bottom, top);
                    continue;
                }
                std::string up;
                for (uint32_t at = esp, n = 0; at + 4 <= top && at < esp + 0x1000 && n < 20; at += 4) {
                    uint32_t w = R32(at);
                    if (w < 0x11000 || w >= 0x1BB7DC) continue;
                    char s[12];
                    std::snprintf(s, sizeof s, " %06X", w);
                    up += s;
                    n++;
                }
                LOGR("hang: thread %X esp %08X eax %08X ecx %08X esi %08X:%s", th->id, esp, c->eax, c->ecx, c->esi,
                     up.c_str());
            }
            // MOHRS_HANG_HANDLES=<handle>[,...] (hex): each handle's object type and signal state
            for (const char* h = std::getenv("MOHRS_HANG_HANDLES"); h && *h;) {
                char* end = nullptr;
                uint32_t handle = uint32_t(std::strtoul(h, &end, 16));
                if (end == h) break;
                kernel::ObjectRef o = kernel::FromHandle(handle);
                LOGR("hang handle %X: %s type %d signal %d limit %d notification %d guest %08X", handle,
                     o ? "object" : "none", o ? int(o->type) : -1, o ? o->signal : 0, o ? o->limit : 0,
                     o ? int(o->notification) : 0, o ? o->guest : 0);
                h = *end == ',' ? end + 1 : nullptr;
            }
            // MOHRS_HANG_DUMP=[*]<addr>:<bytes>[,...] (hex; * reads the address from that pointer first)
            for (const char* d = std::getenv("MOHRS_HANG_DUMP"); d && *d;) {
                bool deref = *d == '*';
                char* end = nullptr;
                uint32_t at = uint32_t(std::strtoul(d + (deref ? 1 : 0), &end, 16));
                if (*end != ':') break;
                uint32_t bytes = uint32_t(std::strtoul(end + 1, &end, 16));
                if (deref) at = R32(at);
                if (at >= 0x10000 && at < 0x84000000)
                    for (uint32_t o = 0; o < bytes; o += 0x20) {
                        std::string words;
                        for (uint32_t w = 0; w < 0x20 && o + w < bytes; w += 4) {
                            char s[12];
                            std::snprintf(s, sizeof s, " %08X", R32(at + o + w));
                            words += s;
                        }
                        LOGR("hang dump %08X +%04X:%s", at, o, words.c_str());
                    }
                d = *end == ',' ? end + 1 : nullptr;
            }
        }
    }).detach();
}

void ProbePad(input::Pad& pad) {
    StartHangTrace();
    struct Press {
        double at, hold;
        std::string what;
        bool captured = false;
    };
    using Clock = std::chrono::steady_clock;
    static const std::vector<Press> script = [] {
        std::vector<Press> out;
        const char* v = std::getenv("MOHRS_MENU_PROBE");
        for (const char* p = v; p && *p;) {
            double at = 0, hold = 0.1;
            char what[16] = {};
            int used = 0;
            if (std::sscanf(p, " %lf/%lf:%15[a-z]%n", &at, &hold, what, &used) != 3 &&
                std::sscanf(p, " %lf:%15[a-z]%n", &at, what, &used) != 2)
                break;
            out.push_back({at, hold, what});
            p += used;
            if (*p == ',') p++;
        }
        if (!out.empty()) LOGR("MOHRS_MENU_PROBE: %zu presses", out.size());
        return out;
    }();
    static std::vector<Press> left = script;
    if (left.empty()) return;
    static const Clock::time_point start = Clock::now();
    double now = std::chrono::duration<double>(Clock::now() - start).count();
    for (Press& p : left) {
        if (now < p.at || now > p.at + p.hold) continue;
        const std::string& w = p.what;
        if (w == "up") pad.buttons |= 0x01;
        if (w == "down") pad.buttons |= 0x02;
        if (w == "left") pad.buttons |= 0x04;
        if (w == "right") pad.buttons |= 0x08;
        if (w == "start") pad.buttons |= 0x10;
        if (w == "back") pad.buttons |= 0x20;
        if (w == "a") pad.analog[0] = 255;
        if (w == "b") pad.analog[1] = 255;
        if (w == "forward") pad.ly = 32767;
        if (w == "backward") pad.ly = -32767;
        if (w == "strafeleft") pad.lx = -32767;
        if (w == "straferight") pad.lx = 32767;
        if (w == "lookleft") pad.rx = -24000;
        if (w == "lookright") pad.rx = 24000;
        if (w == "crouch") pad.analog[2] = 255;  // X
        if (w == "jump") pad.analog[0] = 255;  // A
        if (w == "capture" && !p.captured) {
            p.captured = true;
            gpu::g_capture = true;
            LOGR("MOHRS_MENU_PROBE: capture at %.1f s", now);
        }
    }
}

}  // namespace game
