#include "kernel/memory.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "core/snapshot.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace mem {

static uint8_t* g_base = nullptr;
static std::recursive_mutex g_lock;

uint8_t* Base() { return g_base; }

// physical memory shows at 0x80000000 and again at 0xF0000000 (write combined view)
static bool InPhysical(uint32_t va) {
    return (va >= kContiguousBase && va < kContiguousBase + kPhysicalSize) ||
           (va >= kWriteCombineBase && va < kWriteCombineBase + kPhysicalSize);
}

bool Init() {
    const uint64_t size = 1ull << 32;
#ifdef _WIN32
    HANDLE ram = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kPhysicalSize, nullptr);
    // a fixed base keeps crash dumps comparable between runs
    for (uint64_t hint : {0x100000000ull, 0x200000000ull, 0x300000000ull, 0ull}) {
        uint8_t* b = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(hint), size, MEM_RESERVE, PAGE_NOACCESS));
        if (!b) continue;
        VirtualFree(b, 0, MEM_RELEASE);
        // reserve around the two physical windows, then map the same memory into both
        const uint64_t cuts[][2] = {{0, kContiguousBase},
                                    {kContiguousBase + kPhysicalSize, kWriteCombineBase},
                                    {kWriteCombineBase + kPhysicalSize, size}};
        bool ok = ram != nullptr;
        for (auto& c : cuts)
            ok = ok && VirtualAlloc(b + c[0], size_t(c[1] - c[0]), MEM_RESERVE, PAGE_NOACCESS) == b + c[0];
        ok = ok && MapViewOfFileEx(ram, FILE_MAP_ALL_ACCESS, 0, 0, kPhysicalSize, b + kContiguousBase) == b + kContiguousBase;
        ok = ok && MapViewOfFileEx(ram, FILE_MAP_ALL_ACCESS, 0, 0, kPhysicalSize, b + kWriteCombineBase) == b + kWriteCombineBase;
        if (ok) {
            g_base = b;
            break;
        }
        for (auto& c : cuts) VirtualFree(b + c[0], 0, MEM_RELEASE);
        UnmapViewOfFile(b + kContiguousBase);
        UnmapViewOfFile(b + kWriteCombineBase);
    }
#else
    void* p = mmap(reinterpret_cast<void*>(0x100000000ull), size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    g_base = p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
    int fd = g_base ? memfd_create("ram", 0) : -1;
    if (fd < 0 || ftruncate(fd, kPhysicalSize) ||
        mmap(g_base + kContiguousBase, kPhysicalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED ||
        mmap(g_base + kWriteCombineBase, kPhysicalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
        g_base = nullptr;
#endif
    if (!g_base) {
        LOGE("memory: could not reserve the 4 GB guest space");
        return false;
    }
    LOGI("memory: guest space at %p", static_cast<void*>(g_base));
    return true;
}

// page commit map, one byte per 4 KB page
static std::vector<uint8_t> g_committed(0x100000, 0);

std::vector<std::pair<uint32_t, uint32_t>> CommittedRanges() {
    // a crash may come with the lock held: the map is read as it is
    std::vector<std::pair<uint32_t, uint32_t>> out{{kContiguousBase, kPhysicalSize}};
    for (size_t page = 0; page < g_committed.size();) {
        if (!g_committed[page]) {
            page++;
            continue;
        }
        size_t first = page;
        while (page < g_committed.size() && g_committed[page]) page++;
        out.push_back({uint32_t(first << 12), uint32_t((page - first) << 12)});
    }
    return out;
}

bool Commit(uint32_t va, uint32_t size) {
    if (!size || InPhysical(va)) return true;
    uint32_t first = va & ~(kPage - 1);
    uint64_t end = (uint64_t(va) + size + kPage - 1) & ~uint64_t(kPage - 1);
    std::lock_guard<std::recursive_mutex> lock(g_lock);
#ifdef _WIN32
    if (!VirtualAlloc(g_base + first, size_t(end - first), MEM_COMMIT, PAGE_READWRITE)) {
        LOGE("memory: commit %08X+%X failed", first, uint32_t(end - first));
        return false;
    }
#else
    if (mprotect(g_base + first, size_t(end - first), PROT_READ | PROT_WRITE)) {
        LOGE("memory: commit %08X+%X failed", first, uint32_t(end - first));
        return false;
    }
#endif
    for (uint64_t p = first; p < end; p += kPage) g_committed[size_t(p >> 12)] = 1;
    return true;
}

void Decommit(uint32_t va, uint32_t size) {
    if (InPhysical(va)) return;
    uint32_t first = va & ~(kPage - 1);
    uint64_t end = (uint64_t(va) + size + kPage - 1) & ~uint64_t(kPage - 1);
    std::lock_guard<std::recursive_mutex> lock(g_lock);
#ifdef _WIN32
    VirtualFree(g_base + first, size_t(end - first), MEM_DECOMMIT);
#else
    madvise(g_base + first, size_t(end - first), MADV_DONTNEED);
    mprotect(g_base + first, size_t(end - first), PROT_NONE);
#endif
    for (uint64_t p = first; p < end; p += kPage) g_committed[size_t(p >> 12)] = 0;
}

bool IsCommitted(uint32_t va) { return InPhysical(va) || g_committed[va >> 12] != 0; }

bool IsCommittedRange(uint32_t va, uint32_t size) {
    const uint64_t end = uint64_t(va) + std::max<uint32_t>(size, 1);
    if (end > (1ull << 32)) return false;
    // most textures are in the physical windows: no page walk
    for (uint64_t window : {uint64_t(kContiguousBase), uint64_t(kWriteCombineBase)})
        if (va >= window && end <= window + kPhysicalSize) return true;
    for (uint64_t p = va & ~uint64_t(kPage - 1); p < end; p += kPage)
        if (!IsCommitted(uint32_t(p))) return false;
    return true;
}

// first fit allocator over a guest range
class Ranges {
public:
    void Init(uint32_t start, uint32_t size) { free_[start] = size; }

    void Remove(uint32_t start, uint32_t size) {
        // carve a fixed range out of the free list
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            uint32_t fs = it->first, fz = it->second;
            if (start >= fs && uint64_t(start) + size <= uint64_t(fs) + fz) {
                free_.erase(it);
                if (start > fs) free_[fs] = start - fs;
                if (uint64_t(start) + size < uint64_t(fs) + fz) free_[start + size] = uint32_t(uint64_t(fs) + fz - start - size);
                return;
            }
        }
    }

    uint32_t Alloc(uint32_t size, uint32_t align, uint32_t lo, uint32_t hi, bool top_down) {
        if (!align) align = 1;
        if (top_down) {
            for (auto it = free_.rbegin(); it != free_.rend(); ++it) {
                uint64_t fs = it->first, fe = fs + it->second;
                uint64_t e = std::min<uint64_t>(fe, uint64_t(hi) + 1);
                if (e < size) continue;
                uint64_t a = (e - size) & ~uint64_t(align - 1);
                if (a < fs || a < lo) continue;
                Take(uint32_t(a), size);
                return uint32_t(a);
            }
            return 0;
        }
        for (auto& [fs, fz] : free_) {
            uint64_t a = std::max<uint64_t>(fs, lo);
            a = (a + align - 1) & ~uint64_t(align - 1);
            if (a + size <= uint64_t(fs) + fz && a + size - 1 <= hi) {
                Take(uint32_t(a), size);
                return uint32_t(a);
            }
        }
        return 0;
    }

    bool IsFree(uint32_t start, uint32_t size) const {
        auto it = free_.upper_bound(start);
        if (it == free_.begin()) return false;
        --it;
        return uint64_t(start) + size <= uint64_t(it->first) + it->second;
    }

    bool Free(uint32_t a, uint32_t* size_out = nullptr) {
        auto it = used_.find(a);
        if (it == used_.end()) return false;
        uint32_t size = it->second;
        used_.erase(it);
        if (size_out) *size_out = size;
        Release(a, size);
        return true;
    }

    void Release(uint32_t a, uint32_t size) {
        auto n = free_.emplace(a, size).first;
        auto next = std::next(n);
        if (next != free_.end() && n->first + n->second == next->first) {
            n->second += next->second;
            free_.erase(next);
        }
        if (n != free_.begin()) {
            auto prev = std::prev(n);
            if (prev->first + prev->second == n->first) {
                prev->second += n->second;
                free_.erase(n);
            }
        }
    }

    void Forget(uint32_t a) { used_.erase(a); }

    uint32_t SizeOf(uint32_t a) const {
        auto it = used_.find(a);
        return it == used_.end() ? 0 : it->second;
    }

    uint64_t Used() const {
        uint64_t n = 0;
        for (auto& u : used_) n += u.second;
        return n;
    }

    void Write(snapshot::Writer& w) const {
        for (const auto* m : {&free_, &used_}) {
            w.Pod(uint64_t(m->size()));
            for (auto& [a, n] : *m) {
                w.Pod(a);
                w.Pod(n);
            }
        }
    }

    void Read(snapshot::Reader& r) {
        for (auto* m : {&free_, &used_}) {
            m->clear();
            uint64_t count = r.Pod<uint64_t>();
            for (uint64_t i = 0; i < count && r.ok; i++) {
                uint32_t a = r.Pod<uint32_t>();
                (*m)[a] = r.Pod<uint32_t>();
            }
        }
    }

private:
    void Take(uint32_t a, uint32_t size) {
        Remove(a, size);
        used_[a] = size;
    }
    std::map<uint32_t, uint32_t> free_;
    std::map<uint32_t, uint32_t> used_;
};

static Ranges g_contig, g_system, g_stacks;
static std::map<uint32_t, uint32_t> g_stack_tops;
static bool g_ranges_ready = false;

struct Reservation {
    uint32_t size;
    uint32_t protect;
    std::vector<uint8_t> commit;  // per page
};
static std::map<uint32_t, Reservation> g_reserved;
static Ranges g_user;

static void EnsureRanges() {
    if (g_ranges_ready) return;
    g_ranges_ready = true;
    g_contig.Init(kContiguousBase + kKernelDataSize, kPhysicalSize - kKernelDataSize);
    g_system.Init(kSystemBase, kSystemSize - kThreadBlocksSize);
    g_stacks.Init(kStackBase, kStackSize);
    g_user.Init(kUserLow, kUserHigh - kUserLow);
    Commit(kKernelDataBase, kKernelDataSize);
}

void ReserveImage(uint32_t base, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    EnsureRanges();
    uint32_t b = base & ~0xFFFFu;
    uint32_t e = (base + size + 0xFFFF) & ~0xFFFFu;
    g_user.Remove(b, e - b);
    Reservation r{e - b, 4, std::vector<uint8_t>((e - b) / kPage, 1)};  // 4 is PAGE_READWRITE
    g_reserved[b] = std::move(r);
    Commit(b, e - b);
}

static Reservation* FindReservation(uint32_t addr, uint32_t* start) {
    auto it = g_reserved.upper_bound(addr);
    if (it == g_reserved.begin()) return nullptr;
    --it;
    if (addr >= it->first + it->second.size) return nullptr;
    *start = it->first;
    return &it->second;
}

uint32_t AllocateVirtual(uint32_t& base, uint32_t& size, uint32_t type, uint32_t protect) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    EnsureRanges();
    if (!size) return 0xC000000D;  // STATUS_INVALID_PARAMETER
    bool reserve = type & kMemReserve;
    bool commit = type & kMemCommit;
    if (!base && commit) reserve = true;
    if (reserve) {
        uint32_t sz = (size + 0xFFFF) & ~0xFFFFu;
        uint32_t a;
        if (base) {
            a = base & ~0xFFFFu;
            sz = ((base + size + 0xFFFF) & ~0xFFFFu) - a;
            if (!g_user.IsFree(a, sz)) return 0xC0000018;  // STATUS_CONFLICTING_ADDRESSES
            g_user.Remove(a, sz);
        } else {
            a = g_user.Alloc(sz, 0x10000, kUserLow, kUserHigh - 1, type & kMemTopDown);
            if (!a) return 0xC0000017;  // STATUS_NO_MEMORY
            g_user.Forget(a);
        }
        g_reserved[a] = Reservation{sz, protect, std::vector<uint8_t>(sz / kPage, 0)};
        base = a;
        size = sz;
    }
    if (commit) {
        uint32_t start;
        Reservation* r = FindReservation(base, &start);
        if (!r) return 0xC0000045;  // STATUS_INVALID_PAGE_PROTECTION stands in for not reserved
        uint32_t first = base & ~(kPage - 1);
        uint32_t end = (base + size + kPage - 1) & ~(kPage - 1);
        if (end > start + r->size) return 0xC0000018;  // STATUS_CONFLICTING_ADDRESSES
        Commit(first, end - first);
        for (uint32_t p = first; p < end; p += kPage) r->commit[(p - start) / kPage] = 1;
        base = first;
        size = end - first;
    }
    return 0;
}

uint32_t FreeVirtual(uint32_t& base, uint32_t& size, uint32_t type) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t start;
    Reservation* r = FindReservation(base, &start);
    if (!r) return 0xC000009F;  // STATUS_MEMORY_NOT_ALLOCATED
    if (type & kMemRelease) {
        uint32_t sz = r->size;
        Decommit(start, sz);
        g_reserved.erase(start);
        g_user.Release(start, sz);
        base = start;
        size = sz;
        return 0;
    }
    uint32_t first = base & ~(kPage - 1);
    uint32_t end = size ? (base + size + kPage - 1) & ~(kPage - 1) : start + r->size;
    Decommit(first, end - first);
    for (uint32_t p = first; p < end; p += kPage) r->commit[(p - start) / kPage] = 0;
    base = first;
    size = end - first;
    return 0;
}

bool QueryVirtual(uint32_t addr, RegionInfo& out) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t start;
    Reservation* r = FindReservation(addr, &start);
    std::memset(&out, 0, sizeof(out));
    if (!r) {
        out.base = addr & ~(kPage - 1);
        auto it = g_reserved.upper_bound(addr);
        uint32_t next = it == g_reserved.end() ? kUserHigh : it->first;
        out.size = next > out.base ? next - out.base : kPage;
        out.state = 0x10000;  // MEM_FREE
        out.protect = 1;      // PAGE_NOACCESS
        return addr < kUserHigh;
    }
    uint32_t page = (addr - start) / kPage;
    uint8_t c = r->commit[page];
    uint32_t n = page;
    while (n < r->commit.size() && r->commit[n] == c) n++;
    out.base = start + page * kPage;
    out.alloc_base = start;
    out.alloc_protect = r->protect;
    out.size = (n - page) * kPage;
    out.state = c ? kMemCommit : kMemReserve;
    out.protect = c ? r->protect : 0;
    out.type = 0x20000;  // MEM_PRIVATE
    return true;
}

uint32_t AllocContiguous(uint32_t size, uint32_t lowest, uint32_t highest, uint32_t align) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    EnsureRanges();
    size = (size + kPage - 1) & ~(kPage - 1);
    if (!size) size = kPage;
    align = std::max<uint32_t>(align, kPage);
    uint32_t lo = kContiguousBase | (lowest & (kPhysicalSize - 1));
    uint32_t hi = highest >= kPhysicalSize - 1 ? kContiguousBase + kPhysicalSize - 1 : kContiguousBase | highest;
    // highest fit first: low memory stays free for requests limited to it (D3D report pages, first 16 MB)
    uint32_t a = g_contig.Alloc(size, align, lo, hi, true);
    if (!a) {
        LOGE("memory: contiguous allocation of %X bytes failed (%X..%X align %X), %llu KB in use", size, lowest, highest, align,
             (unsigned long long)(g_contig.Used() >> 10));
        return 0;
    }
    Commit(a, size);
    std::memset(g_base + a, 0, size);
    return a;
}

void FreeContiguous(uint32_t va) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t size;
    if (g_contig.Free(va, &size)) Decommit(va, size);
}

uint32_t AllocSystem(uint32_t size, uint32_t align) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    EnsureRanges();
    size = (size + 15) & ~15u;
    if (!size) size = 16;
    uint32_t a = g_system.Alloc(size, std::max<uint32_t>(align, 16), kSystemBase, kThreadBlocks - 1, false);
    if (!a) return 0;
    Commit(a, size);
    std::memset(g_base + a, 0, size);
    return a;
}

uint32_t AllocThreadBlocks(uint32_t index) {
    if (index >= kThreadBlocksSize / kThreadBlockSize) return 0;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    uint32_t a = kThreadBlocks + index * kThreadBlockSize;
    if (!Commit(a, kThreadBlockSize)) return 0;
    std::memset(g_base + a, 0, kThreadBlockSize);
    return a;
}

void FreeSystem(uint32_t va) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_system.Free(va);
}

uint32_t AllocStack(uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    EnsureRanges();
    size = (size + 0xFFFF) & ~0xFFFFu;
    // one uncommitted guard page below the stack
    uint32_t a = g_stacks.Alloc(size + 0x10000, 0x10000, kStackBase, kStackBase + kStackSize - 1, false);
    if (!a) return 0;
    Commit(a + 0x10000, size);
    g_stack_tops[a + 0x10000 + size] = a;
    return a + 0x10000 + size;
}

void FreeStack(uint32_t top) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    auto it = g_stack_tops.find(top);
    if (it == g_stack_tops.end()) return;
    uint32_t size;
    if (g_stacks.Free(it->second, &size)) Decommit(it->second, size);
    g_stack_tops.erase(it);
}

uint32_t AllocationSize(uint32_t va) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (uint32_t s = g_contig.SizeOf(va)) return s;
    if (uint32_t s = g_system.SizeOf(va)) return s;
    uint32_t start;
    if (Reservation* r = FindReservation(va, &start)) return r->size - (va - start);
    return 0;
}

uint32_t PhysicalFromVirtual(uint32_t va) {
    if (InPhysical(va)) return va & (kPhysicalSize - 1);
    return va;
}

uint32_t VirtualFromPhysical(uint32_t pa) {
    if (pa < kPhysicalSize) return kContiguousBase | pa;
    return pa;
}

Stats Query() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Stats s{};
    s.total_pages = kPhysicalSize / kPage;
    uint64_t vc = 0;
    for (auto& [a, r] : g_reserved)
        for (uint8_t c : r.commit) vc += c;
    s.virtual_committed = uint32_t(vc);
    s.contiguous_used = uint32_t(g_contig.Used() / kPage);
    s.system_used = uint32_t(g_system.Used() / kPage);
    uint64_t used = vc + s.contiguous_used + s.system_used;
    s.available_pages = used >= s.total_pages ? 0 : uint32_t(s.total_pages - used);
    return s;
}

// quick save (core/snapshot.h)

namespace {

std::vector<snapshot::Range> g_kept;

struct SavedMemory : snapshot::Part {
    std::vector<uint8_t> committed;
    Ranges contig, system, stacks, user;
    std::map<uint32_t, uint32_t> stack_tops;
    std::map<uint32_t, Reservation> reserved;
    std::vector<snapshot::Range> runs;  // the committed runs in bytes, physical memory first
    std::vector<uint8_t> bytes;
    std::vector<snapshot::Range> kept;  // the host's own memory when saved

    bool Prepare(const snapshot::RestoreInfo&, std::string& why) override {
        // the host's own memory stays where it is: the save's allocators must have it in the same place
        if (KeptOnRestore() != kept) {
            why = "the host's own guest memory is elsewhere in this run";
            return false;
        }
        return true;
    }

    void Write(snapshot::Writer& w) const override {
        w.Vec(committed);
        for (const Ranges* r : {&contig, &system, &stacks, &user}) r->Write(w);
        w.Pod(uint64_t(stack_tops.size()));
        for (auto& [top, a] : stack_tops) {
            w.Pod(top);
            w.Pod(a);
        }
        w.Pod(uint64_t(reserved.size()));
        for (auto& [a, res] : reserved) {
            w.Pod(a);
            w.Pod(res.size);
            w.Pod(res.protect);
            w.Vec(res.commit);
        }
        w.Vec(kept);
        w.Vec(runs);
        // page by page, a zero page as its flag alone
        static const uint8_t zero[kPage] = {};
        for (size_t at = 0; at < bytes.size(); at += kPage) {
            bool empty = std::memcmp(bytes.data() + at, zero, kPage) == 0;
            w.Pod(uint8_t(empty ? 0 : 1));
            if (!empty) w.Bytes(bytes.data() + at, kPage);
        }
    }

    void Restore(const snapshot::RestoreInfo& info) override {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        // the pages the save had committed and no others
        for (uint32_t page = 0; page < 0x100000;) {
            uint8_t want = committed[page];
            if (want == g_committed[page]) {
                page++;
                continue;
            }
            uint32_t first = page;
            while (page < 0x100000 && committed[page] == want && g_committed[page] != want) page++;
            if (want) Commit(first << 12, (page - first) << 12);
            else Decommit(first << 12, (page - first) << 12);
        }
        g_contig = contig;
        g_system = system;
        g_stacks = stacks;
        g_user = user;
        g_stack_tops = stack_tops;
        g_reserved = reserved;
        size_t at = 0;
        for (const snapshot::Range& run : runs) {
            const uint8_t* from = bytes.data() + at;
            at += run.size;
            uint32_t va = run.va, end = run.va + run.size;
            while (va < end) {
                // past a kept range over va, else copy up to the next one
                uint64_t past = va, next = end;
                for (const snapshot::Range& k : info.keep) {
                    uint64_t ke = uint64_t(k.va) + k.size;
                    if (ke <= va) continue;
                    if (k.va <= va) past = std::max<uint64_t>(past, std::min<uint64_t>(ke, end));
                    else next = std::min<uint64_t>(next, k.va);
                }
                if (past > va) {
                    va = uint32_t(past);
                    continue;
                }
                std::memcpy(g_base + va, from + (va - run.va), size_t(next - va));
                va = uint32_t(next);
            }
        }
    }
};

}  // namespace

void KeepOnRestore(uint32_t va, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_kept.push_back({va, size});
}

std::vector<snapshot::Range> KeptOnRestore() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return g_kept;
}

snapshot::PartRef ReadMemory(snapshot::Reader& r) {
    auto s = std::make_unique<SavedMemory>();
    r.Vec(s->committed);
    if (s->committed.size() != g_committed.size()) r.ok = false;
    for (Ranges* x : {&s->contig, &s->system, &s->stacks, &s->user}) x->Read(r);
    uint64_t n = r.Pod<uint64_t>();
    for (uint64_t i = 0; i < n && r.ok; i++) {
        uint32_t top = r.Pod<uint32_t>();
        s->stack_tops[top] = r.Pod<uint32_t>();
    }
    n = r.Pod<uint64_t>();
    for (uint64_t i = 0; i < n && r.ok; i++) {
        uint32_t a = r.Pod<uint32_t>();
        Reservation res;
        r.Pod(res.size);
        r.Pod(res.protect);
        r.Vec(res.commit);
        s->reserved[a] = std::move(res);
    }
    r.Vec(s->kept);
    r.Vec(s->runs);
    size_t total = 0;
    for (const snapshot::Range& run : s->runs) total += run.size;
    if (!r.ok || total % kPage || total > (size_t(1) << 32)) {
        r.ok = false;
        return nullptr;
    }
    s->bytes.assign(total, 0);
    for (size_t at = 0; at < total && r.ok; at += kPage)
        if (r.Pod<uint8_t>()) r.Bytes(s->bytes.data() + at, kPage);
    if (!r.ok) return nullptr;
    return s;
}

snapshot::PartRef SaveMemory() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    auto s = std::make_unique<SavedMemory>();
    s->kept = g_kept;
    s->committed = g_committed;
    s->contig = g_contig;
    s->system = g_system;
    s->stacks = g_stacks;
    s->user = g_user;
    s->stack_tops = g_stack_tops;
    s->reserved = g_reserved;
    s->runs.push_back({kContiguousBase, kPhysicalSize});  // the write combined view shows the same memory
    for (uint32_t page = 0; page < 0x100000;) {
        if (!g_committed[page]) {
            page++;
            continue;
        }
        uint32_t first = page;
        while (page < 0x100000 && g_committed[page]) page++;
        s->runs.push_back({first << 12, (page - first) << 12});
    }
    size_t total = 0;
    for (const snapshot::Range& run : s->runs) total += run.size;
    s->bytes.resize(total);
    size_t at = 0;
    for (const snapshot::Range& run : s->runs) {
        std::memcpy(s->bytes.data() + at, g_base + run.va, run.size);
        at += run.size;
    }
    return s;
}

}  // namespace mem
