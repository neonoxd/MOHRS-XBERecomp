#include "app/snapshot.h"

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
#include <cwchar>
#include <cwctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "core/snapshot.h"
#include "kernel/kernel.h"

extern const char kImageSha1[];  // the xbe recompiled into this executable, written by CMake

namespace snapshot {

namespace {

constexpr int kStopMs = 2000;
constexpr char kMagic[8] = {'X', 'B', 'E', 'S', 'N', 'A', 'P', 0};
constexpr uint32_t kFormat = 2;

int64_t HostNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

double MsSince(int64_t start) { return double(HostNs() - start) / 1e6; }

// where a guest thread's blocks are: a later run must have them in the same places, as the memory put back points there
struct ThreadLayout {
    uint32_t id, stack_top, stack_size, tls, kpcr, ethread;
    bool operator==(const ThreadLayout& o) const { return std::memcmp(this, &o, sizeof o) == 0; }
};

std::vector<ThreadLayout> Layout() {
    std::vector<ThreadLayout> out;
    for (auto list : {kernel::Threads(), kernel::HostThreads()})
        for (const kernel::ThreadRef& t : list) out.push_back({t->id, t->stack_top, t->stack_size, t->tls, t->kpcr, t->ethread});
    std::sort(out.begin(), out.end(), [](const ThreadLayout& a, const ThreadLayout& b) { return a.id < b.id; });
    return out;
}

// The host's call chain from site (the function that called Save or Load) down to the thread's start, each return
// address as its module's name hash and offset: the same at the same point in any run of this build, wherever the
// modules and the stack are.
std::vector<uint64_t> HostChain(const void* site) {
    std::vector<uint64_t> chain;
#ifdef _WIN32
    void* frames[256];
    USHORT n = RtlCaptureStackBackTrace(1, 256, frames, nullptr);
    bool found = false;
    for (USHORT i = 0; i < n; i++) {
        DWORD64 base = 0;
        if (!found) {
            // the first return into site: Save and Load are called from different places above it
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(DWORD64(frames[i]), &base, nullptr);
            found = f && base + f->BeginAddress == DWORD64(site);
            if (!found) continue;
        }
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(frames[i]), &module);
        wchar_t name[MAX_PATH] = {};
        GetModuleFileNameW(module, name, MAX_PATH);
        uint64_t hash = 1469598103934665603ull;
        for (const wchar_t* c = std::wcsrchr(name, L'\\') ? std::wcsrchr(name, L'\\') + 1 : name; *c; c++)
            hash = (hash ^ uint64_t(towlower(*c))) * 1099511628211ull;
        uint64_t offset = uint64_t(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module));
        chain.push_back(hash << 32 ^ offset);
    }
#else
    chain.push_back(uint64_t(reinterpret_cast<uintptr_t>(site)));
#endif
    return chain;
}

struct Saved {
    X86Context ctx;
    std::vector<uint64_t> chain;  // HostChain
    uint32_t thread = 0;
    std::vector<ThreadLayout> layout;
    int64_t clock_ns = 0;
    std::vector<PartRef> parts;  // memory first: the others are put back over it
    bool from_file = false;
};

using PartReader = PartRef (*)(Reader&);
constexpr PartReader kReaders[] = {mem::ReadMemory, kernel::ReadObjects, dsound::ReadSound, d3d::ReadDevice, xapi::ReadPads};

std::mutex g_lock;
std::shared_ptr<const Saved> g_saved;
std::mutex g_file_lock;  // one file write at a time, the newest last

std::filesystem::path FilePath() {
    return app::ExeDir() / "quicksave" / (std::wstring(app::kGame.name) + L".qsv");
}

void WriteFile(std::shared_ptr<const Saved> s) {
    std::lock_guard<std::mutex> lock(g_file_lock);
    int64_t start = HostNs();
    Writer head;
    head.Bytes(kMagic, sizeof kMagic);
    head.Pod(kFormat);
    head.Str(kImageSha1);
    head.Pod(uint32_t(sizeof(X86Context)));
    head.Pod(s->ctx);
    head.Vec(s->chain);
    head.Pod(s->thread);
    head.Vec(s->layout);
    head.Pod(s->clock_ns);
    head.Pod(uint32_t(s->parts.size()));
    std::filesystem::path path = FilePath(), tmp = path;
    tmp += L".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(head.out.data()), std::streamsize(head.out.size()));
    uint64_t total = head.out.size();
    for (const PartRef& p : s->parts) {
        Writer w;
        p->Write(w);
        uint64_t n = w.out.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof n);
        f.write(reinterpret_cast<const char*>(w.out.data()), std::streamsize(n));
        total += n + sizeof n;
    }
    f.close();
    if (!f) {
        LOGR("snapshot: could not write %s", tmp.string().c_str());
        return;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOGR("snapshot: could not replace %s: %s", path.string().c_str(), ec.message().c_str());
        return;
    }
    LOGR("snapshot: wrote %s, %.1f MB in %.0f ms", path.string().c_str(), double(total) / (1 << 20), MsSince(start));
}

std::shared_ptr<const Saved> ReadFile() {
    std::filesystem::path path = FilePath();
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    int64_t start = HostNs();
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Reader r(bytes.data(), bytes.size());
    char magic[sizeof kMagic];
    r.Bytes(magic, sizeof magic);
    if (std::memcmp(magic, kMagic, sizeof magic) || r.Pod<uint32_t>() != kFormat || r.Str() != kImageSha1) {
        LOGR("snapshot: %s is from another build or game", path.string().c_str());
        return nullptr;
    }
    auto s = std::make_shared<Saved>();
    r.Expect(uint32_t(sizeof(X86Context)));
    r.Pod(s->ctx);
    r.Vec(s->chain);
    r.Pod(s->thread);
    r.Vec(s->layout);
    r.Pod(s->clock_ns);
    if (r.Pod<uint32_t>() != std::size(kReaders)) r.ok = false;
    for (PartReader read : kReaders) {
        uint64_t n = r.Pod<uint64_t>();
        if (!r.ok || n > bytes.size()) {
            r.ok = false;
            break;
        }
        std::vector<uint8_t> part(static_cast<size_t>(n));
        r.Bytes(part.data(), part.size());
        Reader pr(part.data(), part.size());
        PartRef p = r.ok ? read(pr) : nullptr;
        if (!p || !pr.ok) {
            r.ok = false;
            break;
        }
        s->parts.push_back(std::move(p));
    }
    if (!r.ok) {
        LOGR("snapshot: %s is damaged or from another build", path.string().c_str());
        return nullptr;
    }
    s->from_file = true;
    LOGR("snapshot: read %s, %.1f MB in %.0f ms", path.string().c_str(), double(bytes.size()) / (1 << 20), MsSince(start));
    return s;
}

// memory a load leaves alone: the host's own, and each other thread's stack (TLS at its top) and blocks
std::vector<Range> Kept(const kernel::Thread* self) {
    std::vector<Range> keep = mem::KeptOnRestore();
    for (const kernel::ThreadRef& t : kernel::Threads()) {
        if (t.get() == self) continue;
        // as SetupGuestBlocks asked AllocStack for it, which rounds to 64 KB
        uint32_t tls = t->stack_top - t->tls;
        uint32_t size = (t->stack_size + tls + 0x1000 + 0xFFFF) & ~0xFFFFu;
        keep.push_back({t->stack_top - size, size});
        keep.push_back({t->kpcr, 0x400});
        keep.push_back({t->ethread, 0x200});
    }
    return keep;
}

}  // namespace

Result Save(X86Context& ctx, const void* site) {
    std::lock_guard<std::mutex> lock(g_lock);
    kernel::Thread* self = kernel::Current();
    if (!self) return Result::Elsewhere;
    int64_t start = HostNs();
    if (!kernel::StopWorld(kStopMs)) return Result::Busy;
    double stopped = MsSince(start);
    d3d::Drain();
    auto s = std::make_shared<Saved>();
    s->ctx = ctx;
    s->chain = HostChain(site);
    if (s->chain.empty()) {
        kernel::ResumeWorld();
        LOGR("snapshot: not called from the site it names");
        return Result::Elsewhere;
    }
    s->thread = self->id;
    s->layout = Layout();
    s->clock_ns = kernel::GuestClockNs();
    s->parts.push_back(mem::SaveMemory());
    s->parts.push_back(kernel::SaveObjects());
    s->parts.push_back(dsound::SaveSound());
    s->parts.push_back(d3d::SaveDevice());
    s->parts.push_back(xapi::SavePads());
    kernel::ResumeWorld();
    g_saved = s;
    LOGR("snapshot: saved on thread %X in %.1f ms (%.1f ms stopping %zu other threads)", self->id, MsSince(start), stopped,
         s->layout.size() - 1);
    // the file is written from the copy in memory while the game goes on
    std::thread(WriteFile, s).detach();
    return Result::Done;
}

Result Load(X86Context& ctx, const void* site) {
    std::lock_guard<std::mutex> lock(g_lock);
    kernel::Thread* self = kernel::Current();
    if (!self) return Result::Elsewhere;
    if (!g_saved) {
        std::lock_guard<std::mutex> file(g_file_lock);
        g_saved = ReadFile();
        if (!g_saved) return Result::Missing;
    }
    std::shared_ptr<const Saved> saved = g_saved;
    // XBE_SNAPSHOT_ANY_BUILD: tests load a save made by another build; the chain is the same function chain there
    static const bool any_build = std::getenv("XBE_SNAPSHOT_ANY_BUILD") != nullptr;
    if (self->id != saved->thread || (!any_build && HostChain(site) != saved->chain)) {
        LOGR("snapshot: saved on thread %X from another host call chain (another build of the executable?), this is "
             "thread %X", saved->thread, self->id);
        return Result::Elsewhere;
    }
    if (std::vector<ThreadLayout> now = Layout(); now != saved->layout) {
        LOGR("snapshot: the game's threads are not where they were when it saved, not loading");
        auto find = [](const std::vector<ThreadLayout>& in, uint32_t id) -> const ThreadLayout* {
            for (const ThreadLayout& t : in)
                if (t.id == id) return &t;
            return nullptr;
        };
        auto show = [](const char* what, const ThreadLayout& t) {
            LOGR("snapshot:   %s thread %X: stack %08X+%X, tls %08X, kpcr %08X, ethread %08X", what, t.id, t.stack_top,
                 t.stack_size, t.tls, t.kpcr, t.ethread);
        };
        for (const ThreadLayout& t : saved->layout) {
            const ThreadLayout* here = find(now, t.id);
            if (!here) {
                show("saved only", t);
            } else if (!(*here == t)) {
                show("saved", t);
                show("this run", *here);
            }
        }
        for (const ThreadLayout& t : now)
            if (!find(saved->layout, t.id)) show("this run only", t);
        return Result::Mismatch;
    }
    int64_t start = HostNs();
    if (!kernel::StopWorld(kStopMs)) return Result::Busy;
    d3d::Drain();
    for (const PartRef& p : saved->parts) {
        std::string why;
        if (!p->Ready(why)) {
            kernel::ResumeWorld();
            static std::string last;
            if (why != last) LOGR("snapshot: not loading yet: %s", why.c_str());
            last = why;
            return Result::NotYet;
        }
    }
    const std::vector<Range> keep = Kept(self);
    RestoreInfo info{keep};
    for (const PartRef& p : saved->parts) {
        std::string why;
        if (!p->Prepare(info, why)) {
            kernel::ResumeWorld();
            LOGR("snapshot: not loading: %s", why.c_str());
            return Result::Mismatch;
        }
    }
    // the guest clocks go on from the saved moment, before the clock thread writes them over the memory put back
    kernel::ShiftGuestClock(kernel::GuestClockNs() - saved->clock_ns);
    for (const PartRef& p : saved->parts) p->Restore(info);
    void* host = ctx.host;
    uint32_t handle = ctx.thread_handle;
    ctx = saved->ctx;
    ctx.host = host;
    ctx.thread_handle = handle;
    kernel::ResumeWorld();
    LOGR("snapshot: loaded%s in %.1f ms", saved->from_file ? " from the file" : "", MsSince(start));
    return Result::Done;
}

bool Has() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_saved != nullptr || std::filesystem::exists(FilePath());
}

}  // namespace snapshot
