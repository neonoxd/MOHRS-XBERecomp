// Hitch recorder (XBE_HITCH), see hitch.h
#include "core/hitch.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace dispatch {
bool InCode(uint32_t va);  // cpu/dispatch.cpp
}

namespace hitch {

namespace {

constexpr size_t kRing = 32768;                 // events kept, several seconds at hundreds of frames a second
constexpr int64_t kMs = 1000000;                // ns
constexpr int64_t kMarkWindow = 6000 * kMs;     // what the mark key writes
constexpr size_t kIntervals = 256;              // recent frames the median comes from
constexpr int kStackSamples = 6;                // per overdue present

const char* const kNames[] = {"frame", "title frame", "shader", "pipeline", "texture", "target", "upload", "buffer",
                              "open", "read", "title wait", "swap", "gpu wait", "read-back", "stack", "precompile"};
static_assert(std::size(kNames) == size_t(Kind::Count));

// shorter ones are not recorded
constexpr int64_t kMin[] = {0, 0, 0, 0, 0, 0, kMs / 5, 0, 0, 0, kMs / 2, kMs, kMs, kMs / 2, 0, 0};
static_assert(std::size(kMin) == size_t(Kind::Count));

struct Event {
    int64_t start = 0, end = 0;
    Kind kind = Kind::Frame;
    char thread[15] = {};
    char text[112] = {};
};

std::mutex g_lock;
std::vector<Event> g_ring;
uint64_t g_count = 0;  // events ever pushed

const int64_t g_t0 = Now();
std::atomic<int64_t> g_last_present{0};
std::atomic<int64_t> g_threshold{50 * kMs};
std::atomic<int64_t> g_median{0};
std::once_flag g_sampler;

thread_local char t_label[15];

const char* Label() {
    if (!t_label[0]) {
        if (kernel::Thread* t = kernel::Current())
            std::snprintf(t_label, sizeof t_label, "guest %X", t->id);
        else
#ifdef _WIN32
            std::snprintf(t_label, sizeof t_label, "host %lu", GetCurrentThreadId());
#else
            std::snprintf(t_label, sizeof t_label, "host");
#endif
    }
    return t_label;
}

void Push(Kind k, int64_t start, int64_t end, const char* thread, const char* text) {
    std::lock_guard lock(g_lock);
    if (g_ring.empty()) g_ring.resize(kRing);
    Event& e = g_ring[g_count++ % kRing];
    e.start = start;
    e.end = end;
    e.kind = k;
    std::snprintf(e.thread, sizeof e.thread, "%s", thread);
    std::snprintf(e.text, sizeof e.text, "%s", text ? text : "");
}

// the events that ended at or after from, oldest first; pushes come in end order, give or take a scope's length
std::vector<Event> Since(int64_t from) {
    std::vector<Event> out;
    std::lock_guard lock(g_lock);
    uint64_t n = std::min<uint64_t>(g_count, g_ring.size());
    for (uint64_t i = 0; i < n; i++) {
        const Event& e = g_ring[(g_count - 1 - i) % kRing];
        if (e.end < from - 100 * kMs) break;
        if (e.end >= from) out.push_back(e);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

double Ms(int64_t ns) { return double(ns) / 1e6; }
double Sec(int64_t t) { return double(t - g_t0) / 1e9; }

// every guest thread's stack: the code addresses found on it, as a Stack event each
void SampleStacks(int64_t now) {
    for (const kernel::ThreadRef& t : kernel::Threads()) {
        X86Context* ctx = t->ctx;
        if (!ctx) continue;
        uint32_t esp = ctx->esp;
        char text[112];
        int len = std::snprintf(text, sizeof text, "%sesp %08X:", t->waiting ? "[wait] " : "", esp);
        for (uint32_t i = 0; i < 256 && len + 10 < int(sizeof text); i++) {
            uint32_t a = esp + i * 4;
            if (!mem::IsCommitted(a)) break;
            uint32_t v = kernel::R32(a);
            if (dispatch::InCode(v)) len += std::snprintf(text + len, sizeof text - len, " %08X", v);
        }
        char label[15];
        std::snprintf(label, sizeof label, "guest %X", t->id);
        Push(Kind::Stack, now, now, label, text);
    }
}

// samples the stacks while a present is overdue
void Sampler() {
    NameThread("sampler");
    int64_t stall = 0, last_sample = 0;
    int samples = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        int64_t last = g_last_present.load();
        if (!last) continue;
        if (last != stall) {
            stall = last;
            samples = 0;
            last_sample = 0;
        }
        int64_t now = Now();
        int64_t threshold = g_threshold.load();
        if (now - last < threshold || samples >= kStackSamples) continue;
        if (last_sample && now - last_sample < std::max<int64_t>(threshold / 2, 8 * kMs)) continue;
        SampleStacks(now);
        samples++;
        last_sample = now;
    }
}

using Out = std::function<void(const char*)>;

void Line(const Out& out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void Line(const Out& out, const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    out(b);
}

// one slow frame: totals by kind, the longest events, the first stack sample; events lists the frame's and more
void Describe(const Out& out, int64_t start, int64_t end, const std::vector<Event>& events, bool brief) {
    double count[size_t(Kind::Count)] = {}, total[size_t(Kind::Count)] = {};
    double title = 0;
    std::vector<const Event*> in;
    int64_t first_stack = 0;
    for (const Event& e : events) {
        if (e.end < start - kMs || e.start > end) continue;
        if (e.kind == Kind::Frame) continue;
        if (e.kind == Kind::TitleFrame) {
            title = std::max(title, Ms(e.end - e.start));
            continue;
        }
        if (e.kind == Kind::Stack) {
            if (!first_stack) first_stack = e.end;
            if (e.end != first_stack) continue;
        }
        count[size_t(e.kind)]++;
        total[size_t(e.kind)] += Ms(e.end - e.start);
        in.push_back(&e);
    }
    std::string sum;
    for (size_t k = 0; k < size_t(Kind::Count); k++) {
        if (!count[k] || Kind(k) == Kind::Stack) continue;
        char b[64];
        std::snprintf(b, sizeof b, "%s%s %.0fx %.2f ms", sum.empty() ? "" : ", ", kNames[k], count[k], total[k]);
        sum += b;
    }
    Line(out, "hitch: %.2f ms frame at %.3f s (median %.2f, threshold %.1f; longest title frame %.2f ms): %s",
         Ms(end - start), Sec(end), Ms(g_median.load()), Ms(g_threshold.load()), title,
         sum.empty() ? "nothing recorded" : sum.c_str());
    if (brief) return;
    std::vector<const Event*> longest;
    for (const Event* e : in)
        if (e->kind != Kind::Stack) longest.push_back(e);
    std::sort(longest.begin(), longest.end(),
              [](const Event* a, const Event* b) { return a->end - a->start > b->end - b->start; });
    if (longest.size() > 12) longest.resize(12);
    for (const Event* e : longest)
        Line(out, "hitch:   +%6.2f ms %-10s %-10s %7.2f ms  %s", Ms(e->start - start), e->thread, kNames[size_t(e->kind)],
             Ms(e->end - e->start), e->text);
    for (const Event* e : in)
        if (e->kind == Kind::Stack && !std::strstr(e->text, "[wait]"))
            Line(out, "hitch:   +%6.2f ms %-10s stack %s", Ms(e->start - start), e->thread, e->text);
}

}  // namespace

bool On() {
    static const bool on = [] {
        const char* v = std::getenv("XBE_HITCH");
        if (!v) return false;
        double ms = std::atof(v);
        if (ms >= 2) g_threshold = int64_t(ms * kMs);
        return true;
    }();
    return on;
}

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void NameThread(const char* name) { std::snprintf(t_label, sizeof t_label, "%s", name); }

void Record(Kind k, int64_t start, int64_t end, const char* fmt, ...) {
    if (!On() || end - start < kMin[size_t(k)]) return;
    char text[112];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    Push(k, start, end, Label(), text);
}

Scope::~Scope() {
    if (!t_) return;
    int64_t end = Now();
    if (end - t_ >= kMin[size_t(k_)]) Push(k_, t_, end, Label(), text_);
}

void Scope::Text(const char* fmt, ...) {
    if (!t_) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text_, sizeof text_, fmt, ap);
    va_end(ap);
}

void Present() {
    if (!On()) return;
    std::call_once(g_sampler, [] { std::thread(Sampler).detach(); });
    static int64_t last = 0;
    static double intervals[kIntervals];
    static size_t n = 0;
    static int64_t second = 0;
    static int in_second = 0;
    int64_t now = Now();
    if (last) {
        Push(Kind::Frame, last, now, Label(), "");
        int64_t ns = now - last;
        intervals[n++ % kIntervals] = double(ns);
        static const bool fixed = std::atof(std::getenv("XBE_HITCH")) >= 2;
        if (n % 32 == 0 && n >= kIntervals / 4) {
            size_t have = std::min(n, kIntervals);
            std::vector<double> v(intervals, intervals + have);
            std::nth_element(v.begin(), v.begin() + have / 2, v.end());
            int64_t median = int64_t(v[have / 2]);
            g_median = median;
            if (!fixed) g_threshold = std::max({10 * kMs, median * 7 / 4, median + 5 * kMs});
        }
        if (n >= kIntervals / 4 && ns > g_threshold.load()) {
            // a loading screen is slow frame after slow frame: their details only for the first few a second
            if (now - second > 1000 * kMs) {
                second = now;
                in_second = 0;
            }
            bool brief = ++in_second > 4;
            if (in_second <= 20) {
                std::vector<Event> events = Since(last - kMs);
                Describe([](const char* s) { LOGR("%s", s); }, last, now, events, brief);
            }
        }
    }
    last = now;
    g_last_present = now;
}

void TitlePresent() {
    if (!On()) return;
    static thread_local int64_t last = 0;
    int64_t now = Now();
    if (last) Push(Kind::TitleFrame, last, now, Label(), "");
    last = now;
}

void Mark() {
    if (!On()) return;
    int64_t now = Now();
    SampleStacks(now);
    std::vector<Event> events = Since(now - kMarkWindow);
    namespace fs = std::filesystem;
    std::time_t wall = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &wall);
#else
    localtime_r(&wall, &tm);
#endif
    char name[64];
    std::strftime(name, sizeof name, "hitch_%Y%m%d_%H%M%S.txt", &tm);
    fs::path dir = logx::Path().empty() ? fs::path(".") : fs::path(logx::Path()).parent_path();
    fs::path path = dir / name;
    FILE* f = std::fopen(path.string().c_str(), "w");
    if (!f) {
        LOGE("hitch: cannot write %s", path.string().c_str());
        return;
    }
    Out out = [f](const char* s) { std::fprintf(f, "%s\n", s); };
    Line(out, "hitch mark at %.3f s; slow frame threshold %.1f ms, median frame %.2f ms; the last %.0f s", Sec(now),
         Ms(g_threshold.load()), Ms(g_median.load()), Ms(kMarkWindow) / 1000);
    out("");
    out("== slow frames ==");
    int64_t threshold = g_threshold.load();
    for (const Event& e : events)
        if (e.kind == Kind::Frame && e.end - e.start > threshold) {
            Describe(out, e.start, e.end, events, false);
            out("");
        }
    out("== events (seconds before the mark, by start) ==");
    std::vector<Event> by_start = events;
    std::stable_sort(by_start.begin(), by_start.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
    for (const Event& e : by_start) {
        if (e.kind == Kind::Frame || e.kind == Kind::TitleFrame) continue;
        Line(out, "%8.3f %-10s %-10s %7.2f ms  %s", -Ms(now - e.start) / 1000, e.thread, kNames[size_t(e.kind)],
             Ms(e.end - e.start), e.text);
    }
    for (Kind k : {Kind::Frame, Kind::TitleFrame}) {
        out("");
        Line(out, "== %s intervals (ms), oldest first ==", kNames[size_t(k)]);
        std::string row;
        int in_row = 0;
        for (const Event& e : events) {
            if (e.kind != k) continue;
            char b[16];
            std::snprintf(b, sizeof b, "%7.2f", Ms(e.end - e.start));
            row += b;
            if (++in_row == 20) {
                out(row.c_str());
                row.clear();
                in_row = 0;
            }
        }
        if (!row.empty()) out(row.c_str());
    }
    std::fclose(f);
    LOGR("hitch: marked at %.3f s, wrote %s", Sec(now), path.string().c_str());
#ifdef _WIN32
    MessageBeep(MB_OK);
#endif
}

}  // namespace hitch
