#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// Quick save and load (app/snapshot.cpp): the whole machine, kept in memory and in a file. Each subsystem with state
// outside guest memory saves a Part of it while every guest thread is stopped and puts it back the same way, in
// this process or in a later one, which matches the save's objects to its own or makes them again.
namespace snapshot {

struct Range {
    uint32_t va, size;
    bool Holds(uint32_t a) const { return a - va < size; }
    bool operator==(const Range& o) const { return va == o.va && size == o.size; }
};

// the file's bytes, written by the same build that reads them
class Writer {
public:
    std::vector<uint8_t> out;
    void Bytes(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
    template <typename T>
    void Pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        Bytes(&v, sizeof v);
    }
    void Str(const std::string& s) {
        Pod(uint32_t(s.size()));
        Bytes(s.data(), s.size());
    }
    template <typename T>
    void Vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        Pod(uint64_t(v.size()));
        Bytes(v.data(), v.size() * sizeof(T));
    }
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), end_(p + n) {}
    bool ok = true;
    size_t Left() const { return size_t(end_ - p_); }
    void Bytes(void* to, size_t n) {
        if (!ok || size_t(end_ - p_) < n) {
            ok = false;
            std::memset(to, 0, n);
            return;
        }
        std::memcpy(to, p_, n);
        p_ += n;
    }
    template <typename T>
    void Pod(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        Bytes(&v, sizeof v);
    }
    template <typename T>
    T Pod() {
        T v{};
        Pod(v);
        return v;
    }
    std::string Str() {
        uint32_t n = Pod<uint32_t>();
        if (!ok || size_t(end_ - p_) < n) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_), n);
        p_ += n;
        return s;
    }
    template <typename T>
    void Vec(std::vector<T>& v) {
        uint64_t n = Pod<uint64_t>();
        if (!ok || uint64_t(end_ - p_) / sizeof(T) < n) {
            ok = false;
            return;
        }
        v.resize(size_t(n));
        Bytes(v.data(), size_t(n) * sizeof(T));
    }
    // a size this build wrote, checked against this build's
    void Expect(uint32_t v) {
        if (Pod<uint32_t>() != v) ok = false;
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
};

struct RestoreInfo {
    // guest memory left as it is now: other threads' stacks and blocks, memory only the host uses
    const std::vector<Range>& keep;
    bool Kept(uint32_t va) const {
        for (const Range& r : keep)
            if (r.Holds(va)) return true;
        return false;
    }
};

struct Part {
    virtual ~Part() = default;
    // before any part is put back: false (with why) leaves the machine as it is
    virtual bool Prepare(const RestoreInfo&, std::string&) { return true; }
    // with the world stopped, before Prepare: false (with why) when this run is not at a point the save can be put
    // back at yet, but may be a little later
    virtual bool Ready(std::string&) { return true; }
    virtual void Restore(const RestoreInfo& info) = 0;
    virtual void Write(Writer& w) const = 0;
};
using PartRef = std::unique_ptr<Part>;

}  // namespace snapshot

namespace mem {
snapshot::PartRef SaveMemory();  // every committed page and the allocators
snapshot::PartRef ReadMemory(snapshot::Reader& r);
// guest memory the host alone reads and writes, which a load leaves alone (the render thread's header copies)
void KeepOnRestore(uint32_t va, uint32_t size);
std::vector<snapshot::Range> KeptOnRestore();
}  // namespace mem

namespace kernel {
snapshot::PartRef SaveObjects();  // handles, dispatcher objects, file positions, timers and queued DPCs
snapshot::PartRef ReadObjects(snapshot::Reader& r);
// stops every guest thread but the caller in a kernel wait, delay or yield, and holds host threads before they run
// guest code; false after the timeout, the world running again
bool StopWorld(int timeout_ms);
void ResumeWorld();
// the guest clocks (interrupt time, tick count, performance counter, time stamp counter) in ns, which a load puts back
int64_t GuestClockNs();
void ShiftGuestClock(int64_t ns);  // the guest clocks this much later than the host's from now on
}  // namespace kernel

namespace d3d {
void Drain();                     // the render thread done with every command, so guest memory may change under it
snapshot::PartRef SaveDevice();   // the device's host state; a load sends it all to the renderer again
snapshot::PartRef ReadDevice(snapshot::Reader& r);
}  // namespace d3d

namespace dsound {
snapshot::PartRef SaveSound();    // voices, streams' packets and the callbacks due
snapshot::PartRef ReadSound(snapshot::Reader& r);
}  // namespace dsound

namespace xapi {
snapshot::PartRef SavePads();     // the controller handles the title opened
snapshot::PartRef ReadPads(snapshot::Reader& r);
}  // namespace xapi
