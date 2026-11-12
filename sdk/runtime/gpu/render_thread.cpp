// The Direct3D 12 backend on a thread of its own. The title's render thread records each call with what the console
// takes at call time (state changes, resource headers, indices, rects) and goes on; vertex, texture and surface memory
// are read when the draw runs, as the console's GPU reads them after the call returns. The XDK's fences, busy checks
// and locks wait for that.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <smmintrin.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/diag.h"
#include "core/hitch.h"
#include "core/log.h"
#include "core/snapshot.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace gpu {

namespace {

// commands handed over at once, so the render thread keeps close behind and a wait is short
constexpr uint32_t kBatchCommands = 64;

// resource headers the render thread reads: common, data, lock, format, size
constexpr uint32_t kHeaderBytes = 20, kHeaderSlot = 32;
constexpr uint32_t kHeaderRing = 1u << 20;  // guest memory for header copies; a copy is renewed after half a ring

enum class WaitFor : uint8_t { ReadBack, Resource, Fence, Flush, Copy, Frame, Count };

enum class Op : uint8_t { State, Draw, Clear, Copy, Present, Sync, RecordLate, Flush, Gamma, ClearReport, GetReport };

struct Cmd {
    Op op;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    float f = 0;
    uint32_t data = 0, size = 0;  // bytes in Batch::bytes
    int32_t vs = -1, ps = -1;     // index in Batch::vs / ps: the shader changed
    Surface s1, s2;
};

struct Batch {
    uint64_t id = 0;
    std::vector<Cmd> cmds;
    std::vector<uint8_t> bytes;
    std::vector<std::shared_ptr<VertexShader>> vs;
    std::vector<std::shared_ptr<PixelShader>> ps;
};

// the plain data in State, around its shader pointers and the resource headers (sent as copies); of the transforms only
// view, projection, texture and world are drawn with. simple and vsc, the biggest, go by their Tracked marks instead.
struct Range {
    size_t off, len;
};
size_t At(const State& s, const void* p) { return size_t(static_cast<const uint8_t*>(p) - reinterpret_cast<const uint8_t*>(&s)); }
std::vector<Range> StateRanges() {
    static const State s;
    // the small fields between the tracked arrays, compared on every draw
    return {
        {At(s, &s.index_buffer), At(s, &s.vs) - At(s, &s.index_buffer)},
        {At(s, &s.viewport), sizeof(s.viewport)},
        {At(s, &s.scissor_count), sizeof(s.scissor_count)},
        {At(s, &s.light_enable), sizeof(State) - At(s, &s.light_enable)},
    };
}

// equal bytes, 16 at a time: most of the state is compared on every draw
bool Same(const uint8_t* a, const uint8_t* b, size_t n) {
    size_t i = 0;
    __m128i diff = _mm_setzero_si128();
    for (; i + 16 <= n; i += 16)
        diff = _mm_or_si128(diff, _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i)),
                                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i))));
    if (!_mm_testz_si128(diff, diff)) return false;
    return i == n || !std::memcmp(a + i, b + i, n - i);
}

using Clock = std::chrono::steady_clock;
Clock::time_point PerfStart() { return diag::Perf() ? Clock::now() : Clock::time_point{}; }
uint64_t Ns(Clock::time_point t) { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count()); }

class Threaded final : public Backend {
public:
    explicit Threaded(std::unique_ptr<Backend> inner) : inner_(std::move(inner)), ranges_(StateRanges()) {
        ring_ = mem::AllocSystem(kHeaderRing, 64);
        // the copies are the host's: a quick load leaves them as the header cache knows them
        if (ring_) mem::KeepOnRestore(ring_, kHeaderRing);
        cur_ = NewBatch();
        std::thread([this] { Run(); }).detach();
    }
    ~Threaded() override {
        {
            std::lock_guard lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        std::unique_lock lock(m_);
        // at exit the worker may be gone mid-batch: then the inner backend is left, not freed under it
        if (!done_cv_.wait_for(lock, std::chrono::milliseconds(500), [&] { return exited_; })) (void)inner_.release();
    }

    bool Init(uint32_t width, uint32_t height) override { return inner_->Init(width, height); }

    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        auto t = PerfStart();
        SendState(s);
        Cmd c{Op::Draw};
        c.a = prim;
        c.b = start;
        c.c = count;
        if (indices) {
            c.d = 1;
            c.data = Put(indices, size_t(count) * 2);
        }
        cur_->cmds.push_back(c);
        for (size_t i = 0; i < 4; i++) UseSlot(kSlotTexture + i, s.textures[i]);
        for (size_t i = 0; i < 4; i++) UseSlot(kSlotPalette + i, s.palettes[i]);
        for (size_t i = 0; i < s.streams.size(); i++) UseSlot(kSlotStream + i, s.streams[i].vb);
        UseSlot(kSlotTarget, s.rt.header);
        UseSlot(kSlotDepth, s.zs.header);
        Written(s.rt);
        if (cur_->cmds.size() >= kBatchCommands) Submit();
        if (diag::Perf()) g_perf_capture_ns.fetch_add(Ns(t), std::memory_order_relaxed);
    }

    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        auto t = PerfStart();
        SendState(s);
        Cmd c{Op::Clear};
        c.a = count;
        c.b = flags;
        c.c = color;
        c.d = stencil;
        c.f = z;
        if (count && rects) {
            c.data = Put(rects, size_t(count) * 16);
            c.size = count * 16;
        }
        cur_->cmds.push_back(c);
        Use(s.rt.header);
        Use(s.zs.header);
        if (flags & kClearTarget) Written(s.rt);
        if (diag::Perf()) g_perf_capture_ns.fetch_add(Ns(t), std::memory_order_relaxed);
    }

    // rects and points are taken now; the copy itself, CPU or GPU, runs in order like the console's
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override {
        auto t = PerfStart();
        Cmd c{Op::Copy};
        c.a = count;
        c.b = count && src_rects ? Scratch(kernel::HostPtr(src_rects), count * 16) : 0;
        c.c = count && dst_points ? Scratch(kernel::HostPtr(dst_points), count * 8) : 0;
        c.s1 = src;
        c.s1.header = Header(src.header);
        c.s2 = dst;
        c.s2.header = Header(dst.header);
        cur_->cmds.push_back(c);
        Use(src.header);
        Use(dst.header);
        Written(dst);
        if (diag::Perf()) g_perf_capture_ns.fetch_add(Ns(t), std::memory_order_relaxed);
    }

    void Present(const Surface& front) override {
        auto t = PerfStart();
        Cmd c{Op::Present};
        c.s1 = front;
        c.s1.header = Header(front.header);
        cur_->cmds.push_back(c);
        Use(front.header);
        uint64_t id = Submit();
        if (diag::Perf()) g_perf_capture_ns.fetch_add(Ns(t), std::memory_order_relaxed);
        hitch::TitlePresent();
        // the title records one frame while the one before it draws
        Wait(last_present_, WaitFor::Frame, nullptr);
        last_present_ = id;
    }

    void Sync(uint32_t data, uint32_t) override { ReadBack(data, false); }

    void SyncLate(uint32_t data) override {
        if (!inner_->DeliverLate(data)) {
            if (diag::Perf()) LOG_FIRST(16, "render thread: no finished copy of %08X for a NOOVERWRITE lock, reading it back now", data);
            written_.insert(data);
            ReadBack(data, true);
        }
        Cmd c{Op::RecordLate};
        c.a = data;
        cur_->cmds.push_back(c);
    }

    void Flush() override {
        cur_->cmds.push_back(Cmd{Op::Flush});
        Wait(Submit(), WaitFor::Flush, "flush");
    }

    void SetGamma(const uint8_t* ramp) override {
        Cmd c{Op::Gamma};
        c.data = Put(ramp, 768);
        cur_->cmds.push_back(c);
    }

    void ClearReport() override { cur_->cmds.push_back(Cmd{Op::ClearReport}); }
    void GetReport(uint32_t report) override {
        Cmd c{Op::GetReport};
        c.a = report;
        cur_->cmds.push_back(c);
    }

    uint32_t Fence() override { return uint32_t(cur_->id); }
    bool FencePending(uint32_t fence) override { return fence > done_.load(); }
    void WaitFence(uint32_t fence) override { Wait(fence, WaitFor::Fence, "fence"); }
    bool Busy(uint32_t resource) override {
        uint64_t use = LastUse(resource);
        return use && use > done_.load();
    }
    void WaitResource(uint32_t resource) override {
        if (uint64_t use = LastUse(resource)) Wait(use, WaitFor::Resource, "resource");
    }

private:
    Batch* NewBatch() {
        Batch* b;
        {
            std::lock_guard lock(m_);
            if (free_.empty()) {
                b = new Batch;
            } else {
                b = free_.back();
                free_.pop_back();
            }
        }
        b->id = ++next_id_;
        return b;
    }

    uint32_t Put(const void* p, size_t n) {
        size_t at = (cur_->bytes.size() + 3) & ~size_t(3);
        cur_->bytes.resize(at + n);
        std::memcpy(cur_->bytes.data() + at, p, n);
        return uint32_t(at);
    }

    void ReadBack(uint32_t data, bool late) {
        // only surfaces the GPU wrote since the last read-back hold anything newer than guest memory
        if (!written_.erase(data)) return;
        if (diag::Perf()) (late ? g_perf_sync_late : g_perf_sync_block).fetch_add(1, std::memory_order_relaxed);
        if (diag::Perf() && logged_.insert(uint64_t(data) << 1 | (late ? 1 : 0)).second)
            LOGI("render thread: read-back of %08X for %s", data, late ? "a NOOVERWRITE lock without a finished copy" : "a lock / BlockOnResource");
        Cmd c{Op::Sync};
        c.a = data;
        cur_->cmds.push_back(c);
        Wait(Submit(), WaitFor::ReadBack, "read-back");
    }

    // bytes the render thread reads through a guest address, in the header ring
    uint32_t Scratch(const void* p, uint32_t n) {
        uint32_t size = (n + kHeaderSlot - 1) & ~(kHeaderSlot - 1);
        if (ring_pos_ % kHeaderRing + size > kHeaderRing) ring_pos_ += kHeaderRing - ring_pos_ % kHeaderRing;
        uint32_t at = ring_ + uint32_t(ring_pos_ % kHeaderRing);
        ring_pos_ += size;
        std::memcpy(kernel::HostPtr(at), p, n);
        return at;
    }

    // A header as it is now. The console's push buffer takes a texture's format and address, and a surface's or vertex
    // buffer's address, when they are set; the title points the same header elsewhere before the GPU gets there.
    // A header's bytes now, copied into the ring the render thread reads: a binding of a draw keeps the copy it was
    // given while the header's bytes stay the same (SlotHeader), anything else is copied again. (A map of copies by
    // header missed its cache on ~11,000 headers in a few frames: two of them were a sixth of the title's thread.)
    static void Read(uint32_t header, uint8_t (&now)[kHeaderBytes]) {
        // vertex buffer, index buffer and palette headers are 12 bytes: reading 20 can run off the end of the heap
        std::memset(now, 0, kHeaderBytes);
        // a freed header left bound may sit in memory given back: its copy stays zero, which describes nothing
        if (!mem::IsCommittedRange(header, 12)) return;
        const uint32_t type = kernel::R32(header) & 0x70000;  // 0 vertex, 0x10000 index, 0x30000 palette
        uint32_t n = type < 0x20000 || type == 0x30000 ? 12 : kHeaderBytes;
        if (!mem::IsCommittedRange(header, n)) return;
        std::memcpy(now, kernel::HostPtr(header), n);
    }
    uint32_t Header(uint32_t header) {
        if (!header || !ring_) return header;
        uint8_t now[kHeaderBytes];
        Read(header, now);
        return Scratch(now, kHeaderBytes);
    }
    uint32_t SlotHeader(size_t slot, uint32_t header) {
        if (!header || !ring_) return header;
        SlotCopy& s = slots_[slot];
        uint8_t now[kHeaderBytes];
        Read(header, now);
        // a copy is renewed after half a ring, long after the render thread read it
        if (s.header == header && ring_pos_ - s.made < kHeaderRing / 2 && !std::memcmp(s.bytes, now, kHeaderBytes))
            return s.at;
        s.header = header;
        s.made = ring_pos_;
        s.at = Scratch(now, kHeaderBytes);
        std::memcpy(s.bytes, now, kHeaderBytes);
        return s.at;
    }

    // a field of the render thread's State, when it differs from what was sent
    void Delta(const void* field, const void* value, size_t n) {
        uint8_t* sent = reinterpret_cast<uint8_t*>(&sent_) + At(sent_, field);
        if (Same(sent, static_cast<const uint8_t*>(value), n)) return;
        std::memcpy(sent, value, n);
        uint32_t head[2] = {uint32_t(At(sent_, field)), uint32_t(n)};
        size_t at = cur_->bytes.size();
        cur_->bytes.resize(at + 8 + n);
        std::memcpy(cur_->bytes.data() + at, head, 8);
        std::memcpy(cur_->bytes.data() + at + 8, value, n);
    }

    // what changed since the last command, in 64 byte pieces
    void SendState(const State& s) {
        const uint8_t* now = reinterpret_cast<const uint8_t*>(&s);
        Cmd c{Op::State};
        size_t first = cur_->bytes.size();
        for (const Range& r : ranges_)
            for (size_t off = r.off, end = r.off + r.len; off < end; off += 64)
                Delta(reinterpret_cast<const uint8_t*>(&sent_) + off, now + off, std::min<size_t>(64, end - off));
        auto marked = [&](const auto& now_array, const auto& sent_array) {
            now_array.TakeMarked([&](size_t i, size_t n) {
                const uint8_t* from = reinterpret_cast<const uint8_t*>(&now_array[i]);
                const uint8_t* to = reinterpret_cast<const uint8_t*>(&sent_array[i]);
                size_t bytes = n * sizeof(now_array[0]);
                for (size_t off = 0; off < bytes; off += 64) Delta(to + off, from + off, std::min<size_t>(64, bytes - off));
            });
        };
        marked(s.simple, sent_.simple);
        marked(s.vsc, sent_.vsc);
        marked(s.rs, sent_.rs);
        marked(s.tss, sent_.tss);
        marked(s.psc, sent_.psc);
        marked(s.scissors, sent_.scissors);
        marked(s.transforms, sent_.transforms);
        marked(s.material, sent_.material);
        marked(s.lights, sent_.lights);
        std::array<uint32_t, 4> textures;
        for (size_t i = 0; i < 4; i++) textures[i] = SlotHeader(kSlotTexture + i, s.textures[i]);
        Delta(&sent_.textures, &textures, sizeof(textures));
        std::array<uint32_t, 4> palettes;
        for (size_t i = 0; i < 4; i++) palettes[i] = SlotHeader(kSlotPalette + i, s.palettes[i]);
        Delta(&sent_.palettes, &palettes, sizeof(palettes));
        auto streams = s.streams;
        for (size_t i = 0; i < streams.size(); i++) streams[i].vb = SlotHeader(kSlotStream + i, streams[i].vb);
        Delta(&sent_.streams, &streams, sizeof(streams));
        Surface rt = s.rt, zs = s.zs;
        rt.header = SlotHeader(kSlotTarget, rt.header);
        zs.header = SlotHeader(kSlotDepth, zs.header);
        Delta(&sent_.rt, &rt, sizeof(rt));
        Delta(&sent_.zs, &zs, sizeof(zs));
        if (s.vs.get() != sent_.vs.get()) {
            sent_.vs = s.vs;
            c.vs = int32_t(cur_->vs.size());
            cur_->vs.push_back(s.vs);
        }
        if (s.ps.get() != sent_.ps.get()) {
            sent_.ps = s.ps;
            c.ps = int32_t(cur_->ps.size());
            cur_->ps.push_back(s.ps);
        }
        if (cur_->bytes.size() == first && c.vs < 0 && c.ps < 0) return;
        c.data = uint32_t(first);
        c.size = uint32_t(cur_->bytes.size() - first);
        cur_->cmds.push_back(c);
    }

    // resources by guest header: the batch that last read them
    // The resources each batch reads, for the title's rare questions about one (a lock, IsBusy): a list per batch in
    // flight instead of a map updated on every binding of every draw.
    void Use(uint32_t header) {
        if (header) CurUses().push_back(header);
    }
    void UseSlot(size_t slot, uint32_t header) {
        if (!header) return;
        SlotUse& r = slot_uses_[slot];
        if (r.header == header && r.batch == cur_->id) return;  // already listed for this batch
        r = {header, cur_->id};
        CurUses().push_back(header);
    }
    std::vector<uint32_t>& CurUses() {
        if (uses_.empty() || uses_.back().id != cur_->id) {
            // lists of finished batches are reused
            uint64_t done = done_.load();
            while (!uses_.empty() && uses_.front().id <= done) {
                spare_.push_back(std::move(uses_.front().headers));
                spare_.back().clear();
                uses_.pop_front();
            }
            uses_.push_back({cur_->id, {}});
            if (!spare_.empty()) {
                uses_.back().headers = std::move(spare_.back());
                spare_.pop_back();
            }
        }
        return uses_.back().headers;
    }
    // the last batch that read a resource, 0 if none still in flight read it
    uint64_t LastUse(uint32_t header) {
        uint64_t done = done_.load();
        for (auto it = uses_.rbegin(); it != uses_.rend() && it->id > done; ++it)
            if (std::find(it->headers.begin(), it->headers.end(), header) != it->headers.end()) return it->id;
        return 0;
    }
    void Written(const Surface& s) {
        if (s.header) written_.insert(mem::VirtualFromPhysical(s.data));
    }

    uint64_t Submit() {
        uint64_t id = cur_->id;
        {
            std::lock_guard lock(m_);
            queue_.push_back(cur_);
        }
        cv_.notify_one();
        cur_ = NewBatch();
        return id;
    }

    void Wait(uint64_t id, WaitFor kind, const char* why) {
        if (!id || done_.load() >= id) return;
        if (id >= cur_->id) id = Submit();
        auto t = PerfStart();
        {
            static const char* const kinds[] = {"read-back", "resource", "fence", "flush", "copy", "next frame"};
            hitch::Scope hs(hitch::Kind::TitleWait);
            hs.Text("%s", why ? why : kinds[size_t(kind)]);
            std::unique_lock lock(m_);
            done_cv_.wait(lock, [&] { return done_.load() >= id; });
        }
        if (diag::Perf()) {
            uint64_t ns = Ns(t);
            g_perf_wait_ns.fetch_add(ns, std::memory_order_relaxed);
            g_perf_wait_kind_ns[size_t(kind)].fetch_add(ns, std::memory_order_relaxed);
            if (why) {
                g_perf_waits.fetch_add(1, std::memory_order_relaxed);
                LOG_FIRST(32, "render thread: the title waited %.2f ms for a %s", ns / 1e6, why);
            }
        }
    }

    void Run() {
        using SetDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        if (auto set = reinterpret_cast<SetDescription>(
                reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))))
            set(GetCurrentThread(), L"render");
        hitch::NameThread("render");
        for (;;) {
            Batch* b;
            {
                std::unique_lock lock(m_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (queue_.empty()) break;
                b = queue_.front();
                queue_.pop_front();
            }
            for (const Cmd& c : b->cmds) Execute(*b, c);
            uint64_t id = b->id;
            b->cmds.clear();
            b->bytes.clear();
            b->vs.clear();
            b->ps.clear();
            {
                std::lock_guard lock(m_);
                free_.push_back(b);
                done_.store(id);
            }
            done_cv_.notify_all();
        }
        std::lock_guard lock(m_);
        exited_ = true;
        done_cv_.notify_all();
    }

    void Execute(const Batch& b, const Cmd& c) {
        const uint8_t* data = b.bytes.data() + c.data;
        switch (c.op) {
            case Op::State: {
                uint8_t* s = reinterpret_cast<uint8_t*>(&state_);
                for (const uint8_t* p = data; p < data + c.size;) {
                    uint32_t head[2];
                    std::memcpy(head, p, 8);
                    std::memcpy(s + head[0], p + 8, head[1]);
                    p += 8 + head[1];
                }
                if (c.vs >= 0) state_.vs = b.vs[size_t(c.vs)];
                if (c.ps >= 0) state_.ps = b.ps[size_t(c.ps)];
                break;
            }
            case Op::Draw: inner_->Draw(state_, c.a, c.b, c.c, c.d ? data : nullptr); break;
            case Op::Clear: inner_->Clear(state_, c.a, c.size ? data : nullptr, c.b, c.c, c.f, c.d); break;
            case Op::Copy: inner_->Copy(c.s1, c.s2, c.a, c.b, c.c); break;
            case Op::Present: inner_->Present(c.s1); break;
            case Op::Sync: inner_->Sync(c.a, 0); break;
            case Op::RecordLate: inner_->RecordLate(c.a); break;
            case Op::Flush: inner_->Flush(); break;
            case Op::Gamma: inner_->SetGamma(data); break;
            case Op::ClearReport: inner_->ClearReport(); break;
            case Op::GetReport: inner_->GetReport(c.a); break;
        }
    }

    std::unique_ptr<Backend> inner_;
    std::vector<Range> ranges_;

    // the title's thread

    // a draw's bindings: four textures and palettes, the streams, the render target and depth
    static constexpr size_t kSlotTexture = 0, kSlotPalette = 4, kSlotStream = 8, kSlotTarget = 24, kSlotDepth = 25,
                            kSlots = 26;
    struct SlotCopy {
        uint32_t header = 0, at = 0;
        uint64_t made = 0;
        uint8_t bytes[kHeaderBytes] = {};
    };
    std::array<SlotCopy, kSlots> slots_{};
    struct SlotUse {
        uint32_t header = 0;
        uint64_t batch = 0;
    };
    std::array<SlotUse, kSlots> slot_uses_{};
    uint32_t ring_ = 0;
    uint64_t ring_pos_ = 0;
    Batch* cur_ = nullptr;
    State sent_;  // the state as the render thread will have it
    struct UseList {
        uint64_t id = 0;
        std::vector<uint32_t> headers;
    };
    std::deque<UseList> uses_;
    std::vector<std::vector<uint32_t>> spare_;
    std::unordered_set<uint32_t> written_;  // surfaces drawn since their last read-back, by data address
    std::unordered_set<uint64_t> logged_;   // perf diagnostic: read-backs named once, by address and kind
    uint64_t next_id_ = 0, last_present_ = 0;

    // the render thread
    State state_;

    std::mutex m_;
    std::condition_variable cv_, done_cv_;
    std::deque<Batch*> queue_;
    std::vector<Batch*> free_;
    std::atomic<uint64_t> done_{0};
    bool stop_ = false, exited_ = false;
};

}  // namespace

std::unique_ptr<Backend> CreateThreadedBackend(std::unique_ptr<Backend> inner) { return std::make_unique<Threaded>(std::move(inner)); }

}  // namespace gpu
