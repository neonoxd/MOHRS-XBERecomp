#include <unordered_set>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <map>
#include <queue>

#include "core/snapshot.h"
#include "kernel/kernel.h"

namespace kernel {

namespace {

std::mutex g_dispatch;
std::condition_variable g_cv;

std::mutex g_handles_lock;
std::unordered_map<uint32_t, ObjectRef> g_handles;
uint32_t g_next_handle = 0x1000;

std::mutex g_ptr_lock;
struct PtrEntry {
    ObjectRef obj;
    uint32_t serial;
};
std::unordered_map<uint32_t, PtrEntry> g_by_ptr;
uint32_t g_serial = 1;
constexpr uint32_t kStamp = 0x4A424F4Bu;  // written into WaitListHead.Blink of objects the host tracks

std::mutex g_names_lock;
std::unordered_map<std::string, std::weak_ptr<Object>> g_names;

// the world stop, under g_dispatch: threads counted as running guest code, and whether they must stop
std::condition_variable g_world_cv;
int g_running = 0;
bool g_stopped = false;
thread_local bool t_running = false;

void EnterLocked(std::unique_lock<std::mutex>& lock) {
    g_world_cv.wait(lock, [] { return !g_stopped; });
    g_running++;
    t_running = true;
}

void LeaveLocked() {
    g_running--;
    t_running = false;
    g_world_cv.notify_all();
}

}  // namespace

void EnterGuest() {
    std::unique_lock<std::mutex> lock(g_dispatch);
    if (!t_running) EnterLocked(lock);
}

void LeaveGuest() {
    std::lock_guard<std::mutex> lock(g_dispatch);
    if (t_running) LeaveLocked();
}

GuestScope::GuestScope() : entered(!t_running) {
    if (entered) EnterGuest();
}
GuestScope::~GuestScope() {
    if (entered) LeaveGuest();
}

BlockScope::BlockScope() : left(t_running) {
    if (left) LeaveGuest();
}
BlockScope::~BlockScope() {
    if (left) EnterGuest();
}

bool StopWorld(int timeout_ms) {
    std::unique_lock<std::mutex> lock(g_dispatch);
    if (g_stopped) return false;
    g_stopped = true;
    const int self = t_running ? 1 : 0;
    if (g_world_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return g_running == self; })) return true;
    LOGR("snapshot: %d guest threads still running after %d ms, not stopped", g_running - self, timeout_ms);
    g_stopped = false;
    g_world_cv.notify_all();
    return false;
}

void ResumeWorld() {
    std::lock_guard<std::mutex> lock(g_dispatch);
    g_stopped = false;
    g_world_cv.notify_all();
    g_cv.notify_all();
}

std::mutex& DispatchLock() { return g_dispatch; }
void SignalChanged() { g_cv.notify_all(); }

uint32_t NewHandle(ObjectRef obj) {
    std::lock_guard<std::mutex> lock(g_handles_lock);
    uint32_t h = g_next_handle;
    g_next_handle += 4;
    g_handles[h] = std::move(obj);
    return h;
}

ObjectRef FromHandle(uint32_t handle) {
    if (handle == 0xFFFFFFFE) return CurrentRef();  // NtCurrentThread pseudo handle
    std::lock_guard<std::mutex> lock(g_handles_lock);
    auto it = g_handles.find(handle);
    return it == g_handles.end() ? nullptr : it->second;
}

bool CloseHandle(uint32_t handle) {
    std::lock_guard<std::mutex> lock(g_handles_lock);
    return g_handles.erase(handle) != 0;
}

uint32_t DuplicateHandle(uint32_t handle) {
    ObjectRef o = FromHandle(handle);
    return o ? NewHandle(o) : 0;
}

// serial in WaitListHead.Flink, stamp in Blink
static void Stamp(uint32_t guest, uint32_t serial) {
    W32(guest + 8, serial);
    W32(guest + 12, kStamp);
}

void Bind(ObjectRef obj, uint32_t guest) {
    obj->guest = guest;
    std::lock_guard<std::mutex> lock(g_ptr_lock);
    uint32_t serial = g_serial++;
    g_by_ptr[guest] = {obj, serial};
    Stamp(guest, serial);
}

ObjectRef FromPointer(uint32_t guest) {
    if (!guest) return nullptr;
    std::lock_guard<std::mutex> lock(g_ptr_lock);
    auto it = g_by_ptr.find(guest);
    if (it != g_by_ptr.end() && R32(guest + 12) == kStamp && R32(guest + 8) == it->second.serial) return it->second.obj;
    // first sight, or the guest reinitialised the structure: build from the header
    uint8_t type = G<uint8_t>(guest);
    int32_t state = int32_t(R32(guest + 4));
    ObjectRef o;
    switch (type) {
        case 0:
        case 1:
            o = std::make_shared<Object>(ObjType::Event);
            o->notification = type == 0;
            o->signal = state;
            break;
        case 2:
            o = std::make_shared<Object>(ObjType::Mutant);
            o->signal = state;
            break;
        case 5:
            o = std::make_shared<Object>(ObjType::Semaphore);
            o->signal = state;
            o->limit = int32_t(R32(guest + 0x10));
            break;
        case 8:
        case 9:
            o = std::make_shared<Object>(ObjType::Timer);
            o->notification = type == 8;
            o->signal = state;
            break;
        case 6:
            if (it != g_by_ptr.end()) return it->second.obj;  // threads keep their object
            o = std::make_shared<Object>(ObjType::Other);
            break;
        default:
            LOGW("objects: unknown dispatcher type %u at %08X", type, guest);
            if (Current()) LogGuestStack(*Current()->ctx, 64);
            o = std::make_shared<Object>(ObjType::Event);
            o->notification = true;
            o->signal = state;
            break;
    }
    o->guest = guest;
    uint32_t serial = g_serial++;
    g_by_ptr[guest] = {o, serial};
    Stamp(guest, serial);
    return o;
}

ObjectRef FindNamed(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_names_lock);
    auto it = g_names.find(name);
    return it == g_names.end() ? nullptr : it->second.lock();
}

void AddNamed(const std::string& name, ObjectRef obj) {
    std::lock_guard<std::mutex> lock(g_names_lock);
    obj->name = name;
    g_names[name] = obj;
}

void SetSignal(Object& o, int32_t v) {
    o.signal = v;
    if (o.guest) W32(o.guest + 4, uint32_t(v));
}

static uint32_t Me() {
    Thread* t = Current();
    return t ? t->ethread : 1;
}

static bool Satisfied(Object& o, uint32_t me) {
    if (o.type == ObjType::Mutant) return o.signal > 0 || o.owner == me;
    return o.signal > 0;
}

static void Consume(Object& o, uint32_t me) {
    switch (o.type) {
        case ObjType::Event:
        case ObjType::Timer:
            if (!o.notification) SetSignal(o, 0);
            break;
        case ObjType::Semaphore:
            SetSignal(o, o.signal - 1);
            break;
        case ObjType::Mutant:
            if (o.owner == me) {
                o.recursion++;
            } else {
                o.owner = me;
                o.recursion = 1;
                SetSignal(o, 0);
            }
            break;
        default:
            break;
    }
}

namespace {
std::atomic<int64_t> g_clock_shift{0};
const int64_t g_clock_start = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch()).count();
}  // namespace

int64_t GuestClockNs() {
    using namespace std::chrono;
    int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return now - g_clock_start - g_clock_shift.load(std::memory_order_relaxed);
}

void ShiftGuestClock(int64_t ns) { g_clock_shift.fetch_add(ns, std::memory_order_relaxed); }

int64_t Now100ns() {
    using namespace std::chrono;
    // 1601 to 1970 in 100 ns units
    auto t = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    return t / 100 + 116444736000000000ll;
}

int64_t ToDeadline(int64_t due) {
    using namespace std::chrono;
    int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    if (due < 0) return now + (-due) * 100;  // negative due is relative, positive is absolute system time
    int64_t rel = due - Now100ns();
    return now + std::max<int64_t>(rel, 0) * 100;
}

static uint32_t WaitFor(const std::vector<ObjectRef>& waited, bool wait_all, int64_t timeout, bool alertable) {
    using namespace std::chrono;
    Thread* self = Current();
    uint32_t me = Me();
    bool infinite = timeout == INT64_MAX;
    int64_t deadline = infinite ? 0 : ToDeadline(timeout);
    // the thread's own list, which a quick load may point at the objects of the save (SavedObjects::Prepare)
    std::vector<ObjectRef> objs = waited;
    std::unique_lock<std::mutex> lock(g_dispatch);
    struct Listed {
        Thread* self;
        Listed(Thread* t, std::vector<ObjectRef>* list) : self(t) {
            if (self) self->waiting = list;
        }
        ~Listed() {
            if (self) self->waiting = nullptr;
        }
    } listed(self, &objs);
    // a running guest thread stops counting while it waits: the world may stop now. It takes nothing and returns
    // nowhere while stopped, so what it took and where it went are on the same side of a snapshot as its memory.
    const bool counted = t_running;
    if (counted) LeaveLocked();
    auto back = [&] {
        if (counted) {
            g_running++;
            t_running = true;
        }
    };
    for (;;) {
        if (counted) g_world_cv.wait(lock, [] { return !g_stopped; });
        if (wait_all) {
            bool all = std::all_of(objs.begin(), objs.end(), [&](const ObjectRef& o) { return o && Satisfied(*o, me); });
            if (all) {
                for (auto& o : objs) Consume(*o, me);
                back();
                return kWait0;
            }
        } else {
            for (size_t i = 0; i < objs.size(); i++)
                if (objs[i] && Satisfied(*objs[i], me)) {
                    Consume(*objs[i], me);
                    back();
                    return kWait0 + uint32_t(i);
                }
        }
        if (alertable && self) {
            bool pending;
            {
                std::lock_guard<std::mutex> al(self->apc_lock);
                pending = !self->apcs.empty();
            }
            if (pending) {
                back();
                self->waiting = nullptr;
                lock.unlock();
                RunApcs(*self);
                return kUserApc;
            }
        }
        if (!infinite) {
            int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
            if (now >= deadline) {
                back();
                return kTimeout;
            }
            // bounded so alertable waits notice new APCs
            g_cv.wait_for(lock, nanoseconds(std::min<int64_t>(deadline - now, 50000000)));
        } else {
            g_cv.wait_for(lock, milliseconds(50));
        }
    }
}

uint32_t Wait(const std::vector<ObjectRef>& objs, bool wait_all, int64_t timeout, bool alertable) {
    SuspendPoint();
    uint32_t r = WaitFor(objs, wait_all, timeout, alertable);
    SuspendPoint();
    return r;
}

int32_t SetEvent(Object& o) {
    std::lock_guard<std::mutex> lock(g_dispatch);
    int32_t old = o.signal;
    SetSignal(o, 1);
    g_cv.notify_all();
    return old;
}

int32_t ResetEvent(Object& o) {
    std::lock_guard<std::mutex> lock(g_dispatch);
    int32_t old = o.signal;
    SetSignal(o, 0);
    return old;
}

int32_t PulseEvent(Object& o) {
    // waiters already blocked cannot be told apart here, so a pulse behaves like set for one pass
    std::lock_guard<std::mutex> lock(g_dispatch);
    int32_t old = o.signal;
    SetSignal(o, 1);
    g_cv.notify_all();
    return old;
}

int32_t ReleaseSemaphore(Object& o, int32_t count, uint32_t& status) {
    std::lock_guard<std::mutex> lock(g_dispatch);
    int32_t old = o.signal;
    if (o.limit && old + count > o.limit) {
        status = kSemaphoreLimitExceeded;
        return old;
    }
    SetSignal(o, old + count);
    status = kSuccess;
    g_cv.notify_all();
    return old;
}

int32_t ReleaseMutant(Object& o, uint32_t& status) {
    std::lock_guard<std::mutex> lock(g_dispatch);
    uint32_t me = Me();
    if (o.owner != me) {
        status = kMutantNotOwned;
        return o.signal;
    }
    int32_t old = o.signal;
    if (--o.recursion == 0) {
        o.owner = 0;
        SetSignal(o, 1);
        g_cv.notify_all();
    }
    status = kSuccess;
    return old;
}

// timers and DPCs

namespace {

struct TimerEntry {
    int64_t deadline;
    uint64_t seq;
    ObjectRef timer;
    int32_t period_ms;
    uint32_t dpc;
    std::weak_ptr<Thread> apc_thread;
    uint32_t apc_routine, apc_context;
    bool operator>(const TimerEntry& o) const { return deadline > o.deadline; }
};

std::mutex g_timer_lock;
std::condition_variable g_timer_cv;
std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> g_timers;
std::unordered_map<Object*, uint64_t> g_timer_seq;  // latest arm per timer, older entries are stale
uint64_t g_seq = 1;
bool g_timer_thread = false;

std::mutex g_dpc_lock;
std::condition_variable g_dpc_cv;
std::deque<std::array<uint32_t, 3>> g_dpcs;
bool g_dpc_thread = false;

}  // namespace

void RunDpcs(X86Context& ctx) {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(g_dpc_lock);
            g_dpc_cv.wait(lock, [] { return !g_dpcs.empty(); });
        }
        GuestScope guest;  // taken and run on one side of a quick save (core/snapshot.h)
        std::array<uint32_t, 3> d;
        {
            std::lock_guard<std::mutex> lock(g_dpc_lock);
            if (g_dpcs.empty()) continue;
            d = g_dpcs.front();
            g_dpcs.pop_front();
        }
        uint32_t dpc = d[0];
        G<uint8_t>(dpc + 2) = 0;  // Inserted
        uint32_t routine = R32(dpc + 0x0C), context = R32(dpc + 0x10);  // KDPC.DeferredRoutine and DeferredContext
        if (routine) CallGuest(ctx, routine, {dpc, context, d[1], d[2]});
    }
}

void QueueDpc(uint32_t dpc, uint32_t arg1, uint32_t arg2) {
    std::lock_guard<std::mutex> lock(g_dpc_lock);
    W32(dpc + 0x14, arg1);  // KDPC.SystemArgument1 and 2
    W32(dpc + 0x18, arg2);
    g_dpcs.push_back({dpc, arg1, arg2});
    g_dpc_cv.notify_one();
    if (!g_dpc_thread) {
        g_dpc_thread = true;
        StartHostThread("dpc", [](X86Context& ctx) { RunDpcs(ctx); });
    }
}

static void TimerLoop() {
    using namespace std::chrono;
    std::unique_lock<std::mutex> lock(g_timer_lock);
    for (;;) {
        if (g_timers.empty()) {
            g_timer_cv.wait(lock);
            continue;
        }
        int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        TimerEntry e = g_timers.top();
        if (e.deadline > now) {
            g_timer_cv.wait_for(lock, nanoseconds(e.deadline - now));
            continue;
        }
        auto it = g_timer_seq.find(e.timer.get());
        if (it == g_timer_seq.end() || it->second != e.seq) {
            g_timers.pop();
            continue;
        }
        bool held;
        {
            std::lock_guard<std::mutex> dl(g_dispatch);
            // nothing fires while the world is stopped (a quick save takes the queue as it is)
            held = g_stopped;
            if (!held) {
                g_timers.pop();
                SetSignal(*e.timer, 1);
                g_cv.notify_all();
            }
        }
        if (held) {
            g_timer_cv.wait_for(lock, std::chrono::milliseconds(5));
            continue;
        }
        if (e.dpc) QueueDpc(e.dpc, uint32_t(Now100ns()), uint32_t(Now100ns() >> 32));
        if (auto t = e.apc_thread.lock(); t && e.apc_routine)
            QueueApc(*t, e.apc_routine, e.apc_context, uint32_t(Now100ns()), uint32_t(Now100ns() >> 32));
        if (e.period_ms > 0) {
            e.deadline += int64_t(e.period_ms) * 1000000;
            g_timers.push(e);
        } else {
            g_timer_seq.erase(e.timer.get());
        }
    }
}

void SetTimer(ObjectRef timer, int64_t due, int32_t period_ms, uint32_t dpc, std::shared_ptr<Thread> apc_thread,
              uint32_t apc_routine, uint32_t apc_context) {
    std::lock_guard<std::mutex> lock(g_timer_lock);
    if (!g_timer_thread) {
        g_timer_thread = true;
        std::thread(TimerLoop).detach();
    }
    {
        std::lock_guard<std::mutex> dl(g_dispatch);
        SetSignal(*timer, 0);
    }
    uint64_t seq = g_seq++;
    g_timer_seq[timer.get()] = seq;
    g_timers.push({ToDeadline(due), seq, timer, period_ms, dpc, apc_thread, apc_routine, apc_context});
    g_timer_cv.notify_all();
}

bool CancelTimer(Object& timer) {
    std::lock_guard<std::mutex> lock(g_timer_lock);
    return g_timer_seq.erase(&timer) != 0;
}

// quick save (core/snapshot.h), taken and put back with the world stopped. Objects are kept as descriptions: a load
// puts each back into the object this run has under the same handle or guest address, or makes it again, so threads
// waiting on an object keep waiting on the one that is signalled.

namespace {

struct ObjectDesc {
    ObjType type = ObjType::Other;
    uint32_t guest = 0;
    int32_t signal = 0, limit = 0;
    bool notification = false;
    uint32_t owner = 0, recursion = 0;
    uint64_t pos = 0;
    bool has_pos = false;
    std::string name;
    uint32_t thread = 0;             // a thread's id
    std::vector<uint8_t> described;  // Object::Describe: what makes a file or link again
    ObjectRef live;                  // the object itself, in the run that saved it
};

struct SavedHandle {
    uint32_t handle, object;
};
struct SavedPtr {
    uint32_t guest, object, serial;
};
// where a guest thread was parked at the save: its stack pointer and the code addresses on its stack above it (its
// return addresses, and some stale ones); a load waits until each is there again, as its kept stack and registers
// belong to that point (a thread elsewhere may hold pointers to memory the load replaces)
struct SavedSpot {
    uint32_t thread, esp;
    std::vector<uint32_t> code;
    bool operator==(const SavedSpot& o) const { return thread == o.thread && esp == o.esp && code == o.code; }
};
}  // namespace
}  // namespace kernel
namespace dispatch {
bool InCode(uint32_t va);  // cpu/dispatch.cpp
}
namespace kernel {
namespace {
SavedSpot SpotOf(const Thread& t) {
    SavedSpot s{t.id, t.ctx ? t.ctx->esp : 0, {}};
    if (!t.ctx || s.esp >= t.stack_top || s.esp < t.stack_top - t.stack_size) return s;
    for (uint32_t at = s.esp; at + 4 <= t.stack_top && at < s.esp + 0x400 && s.code.size() < 16; at += 4)
        if (uint32_t v = R32(at); dispatch::InCode(v)) s.code.push_back(v);
    return s;
}

// a guest thread inside a kernel wait at the save, and the objects it waited on
struct SavedWait {
    uint32_t thread;
    std::vector<uint32_t> objects;
};
struct SavedTimer {
    uint32_t object;
    int32_t period_ms;
    int64_t left_ns;  // till it fires, from the save
    uint32_t dpc, apc_thread, apc_routine, apc_context;
};

int64_t SteadyNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

ThreadRef ThreadById(uint32_t id) {
    for (const ThreadRef& t : Threads())
        if (t->id == id) return t;
    for (const ThreadRef& t : HostThreads())
        if (t->id == id) return t;
    return nullptr;
}

std::vector<uint8_t> Described(const Object& o) {
    snapshot::Writer w;
    o.Describe(w);
    return std::move(w.out);
}

struct SavedObjects : snapshot::Part {
    std::vector<ObjectDesc> objects;
    std::vector<SavedHandle> handles;
    std::vector<SavedPtr> ptrs;
    std::vector<SavedTimer> timers;
    std::vector<std::array<uint32_t, 3>> dpcs;
    uint32_t next_handle = 0, serial = 0;
    std::vector<SavedWait> waits;
    std::vector<SavedSpot> spots;
    std::vector<ObjectRef> made;  // Prepare's object for each description

    bool Ready(std::string& why) override {
        Thread* self = Current();
        for (const SavedSpot& s : spots) {
            ThreadRef t = ThreadById(s.thread);
            if (!t || t.get() == self) continue;
            if (!(SpotOf(*t) == s)) {
                char text[96];
                std::snprintf(text, sizeof text, "thread %X is parked elsewhere than at the save", s.thread);
                why = text;
                return false;
            }
        }
        return true;
    }

    bool Prepare(const snapshot::RestoreInfo&, std::string& why) override {
        made.assign(objects.size(), nullptr);
        // A thread parked in a wait keeps waiting on this run's objects, which the guest memory put back may name by
        // other handles than this run gave them (a run that went through other screens first). The save's thread of
        // the same id waited on the save's objects in the same order: those become this run's.
        std::vector<ObjectRef> pinned(objects.size());
        std::unordered_set<Object*> taken;
        uint32_t rebound = 0;
        {
            std::lock_guard<std::mutex> lock(g_dispatch);
            for (const SavedWait& w : waits) {
                ThreadRef t = ThreadById(w.thread);
                if (!t || !t->waiting || t->waiting->size() != w.objects.size()) continue;
                for (size_t k = 0; k < w.objects.size(); k++) {
                    const ObjectRef& o = (*t->waiting)[k];
                    uint32_t i = w.objects[k];
                    if (i >= objects.size()) continue;
                    if (!o || o->type != objects[i].type || objects[i].live || pinned[i] || taken.count(o.get()))
                        continue;
                    pinned[i] = o;
                    taken.insert(o.get());
                    rebound++;
                }
            }
        }
        // what this run has under the save's handles and guest addresses
        std::vector<std::vector<ObjectRef>> candidates(objects.size());
        {
            std::lock_guard<std::mutex> lock(g_handles_lock);
            for (const SavedHandle& h : handles)
                if (auto it = g_handles.find(h.handle); it != g_handles.end()) candidates[h.object].push_back(it->second);
        }
        {
            std::lock_guard<std::mutex> lock(g_ptr_lock);
            for (const SavedPtr& p : ptrs)
                if (auto it = g_by_ptr.find(p.guest); it != g_by_ptr.end()) candidates[p.object].push_back(it->second.obj);
        }
        for (size_t i = 0; i < objects.size(); i++) {
            const ObjectDesc& d = objects[i];
            if (d.live) {
                made[i] = d.live;
                continue;
            }
            if (pinned[i]) {
                made[i] = pinned[i];
                continue;
            }
            if (d.type == ObjType::Thread) {
                made[i] = ThreadById(d.thread);
                if (!made[i]) {
                    // a thread that had ended (the layout check found every running one): it stays ended
                    if (d.signal != 1) {
                        why = "a running thread of the save is missing";
                        return false;
                    }
                    auto t = std::make_shared<Thread>();
                    t->id = d.thread;
                    t->guest = d.guest;
                    t->signal = 1;
                    made[i] = t;
                }
                continue;
            }
            for (const ObjectRef& c : candidates[i])
                if (c && !taken.count(c.get()) && c->type == d.type && Described(*c) == d.described) {
                    made[i] = c;
                    break;
                }
            if (made[i]) continue;
            if (d.type == ObjType::File || d.type == ObjType::SymLink) {
                snapshot::Reader r(d.described.data(), d.described.size());
                made[i] = RebuildObject(d.type, r);
                if (!made[i]) {
                    why = "a file the save had open cannot be opened";
                    return false;
                }
            } else {
                made[i] = std::make_shared<Object>(d.type);
            }
        }
        if (rebound) LOGR("snapshot: %u objects matched by the threads waiting on them", rebound);
        return true;
    }

    void Restore(const snapshot::RestoreInfo& info) override {
        {
            std::lock_guard<std::mutex> lock(g_timer_lock);
            // as long from firing as they were: the guest's clock stood still in between
            decltype(g_timers) queue;
            g_timer_seq.clear();
            int64_t now = SteadyNs();
            for (const SavedTimer& t : timers) {
                const ObjectRef& o = made[t.object];
                uint64_t seq = g_seq++;
                g_timer_seq[o.get()] = seq;
                queue.push({now + t.left_ns, seq, o, t.period_ms, t.dpc, ThreadById(t.apc_thread), t.apc_routine,
                            t.apc_context});
            }
            g_timers = std::move(queue);
            g_timer_cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(g_dpc_lock);
            g_dpcs.assign(dpcs.begin(), dpcs.end());
            g_dpc_cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(g_handles_lock);
            g_handles.clear();
            for (const SavedHandle& h : handles) g_handles[h.handle] = made[h.object];
            g_next_handle = std::max(g_next_handle, next_handle);
        }
        {
            std::lock_guard<std::mutex> lock(g_ptr_lock);
            auto present = std::move(g_by_ptr);
            g_by_ptr.clear();
            for (const SavedPtr& p : ptrs)
                if (!info.Kept(p.guest)) g_by_ptr[p.guest] = {made[p.object], p.serial};
            // memory the load left alone keeps the objects it has
            for (auto& [guest, e] : present)
                if (info.Kept(guest)) g_by_ptr[guest] = e;
            g_serial = std::max(g_serial, serial);
        }
        {
            std::lock_guard<std::mutex> lock(g_names_lock);
            g_names.clear();
            for (size_t i = 0; i < objects.size(); i++)
                if (!objects[i].name.empty()) g_names[objects[i].name] = made[i];
        }
        std::lock_guard<std::mutex> lock(g_dispatch);
        for (size_t i = 0; i < objects.size(); i++) {
            const ObjectDesc& d = objects[i];
            Object& o = *made[i];
            // threads are the host's own; an object in memory the load left alone keeps its present state too
            if (d.type == ObjType::Thread || o.type == ObjType::Thread || (d.guest && info.Kept(d.guest))) continue;
            o.guest = d.guest;
            o.signal = d.signal;
            o.limit = d.limit;
            o.notification = d.notification;
            o.owner = d.owner;
            o.recursion = d.recursion;
            o.name = d.name;
            if (uint64_t* pos = o.Position(); pos && d.has_pos) *pos = d.pos;
        }
        // A thread parked on an event or semaphore that no handle or guest address names any more (a thread that was
        // busy at the save, waiting at the load) would sleep forever: nothing can set it now. Set it once, so the thread
        // looks at the memory put back again, as after a spurious wake.
        std::unordered_set<Object*> named;
        {
            std::lock_guard<std::mutex> hl(g_handles_lock);
            for (auto& [h, o] : g_handles) named.insert(o.get());
        }
        {
            std::lock_guard<std::mutex> pl(g_ptr_lock);
            for (auto& [guest, e] : g_by_ptr) named.insert(e.obj.get());
        }
        uint32_t woken = 0;
        static const bool trace = std::getenv("XBE_SNAPSHOT_TRACE") != nullptr;
        for (const ThreadRef& t : Threads()) {
            if (!t->waiting) continue;
            if (trace)
                for (const ObjectRef& o : *t->waiting)
                    LOGR("snapshot: thread %X waits on %p type %d guest %08X signal %d named %d", t->id,
                         static_cast<void*>(o.get()), o ? int(o->type) : -1, o ? o->guest : 0, o ? o->signal : 0,
                         o ? int(named.count(o.get())) : 0);
            for (const ObjectRef& o : *t->waiting)
                if (o && !named.count(o.get()) && (o->type == ObjType::Event || o->type == ObjType::Semaphore) &&
                    o->signal <= 0) {
                    o->signal = 1;  // not SetSignal: the guest memory it would write is the save's now
                    woken++;
                }
        }
        if (woken) {
            g_cv.notify_all();
            LOGR("snapshot: set %u objects only a parked thread still waited on", woken);
        }
        made.clear();
    }

    void Write(snapshot::Writer& w) const override {
        w.Pod(uint64_t(objects.size()));
        for (const ObjectDesc& d : objects) {
            w.Pod(uint8_t(d.type));
            w.Pod(d.guest);
            w.Pod(d.signal);
            w.Pod(d.limit);
            w.Pod(uint8_t(d.notification));
            w.Pod(d.owner);
            w.Pod(d.recursion);
            w.Pod(d.pos);
            w.Pod(uint8_t(d.has_pos));
            w.Str(d.name);
            w.Pod(d.thread);
            w.Vec(d.described);
        }
        w.Vec(handles);
        w.Vec(ptrs);
        w.Vec(timers);
        w.Vec(dpcs);
        w.Pod(next_handle);
        w.Pod(serial);
        w.Pod(uint32_t(waits.size()));
        for (const SavedWait& s : waits) {
            w.Pod(s.thread);
            w.Vec(s.objects);
        }
        w.Pod(uint32_t(spots.size()));
        for (const SavedSpot& s : spots) {
            w.Pod(s.thread);
            w.Pod(s.esp);
            w.Vec(s.code);
        }
    }
};

}  // namespace

snapshot::PartRef ReadObjects(snapshot::Reader& r) {
    auto s = std::make_unique<SavedObjects>();
    uint64_t n = r.Pod<uint64_t>();
    if (n > (1u << 20)) r.ok = false;
    for (uint64_t i = 0; i < n && r.ok; i++) {
        ObjectDesc d;
        d.type = ObjType(r.Pod<uint8_t>());
        r.Pod(d.guest);
        r.Pod(d.signal);
        r.Pod(d.limit);
        d.notification = r.Pod<uint8_t>() != 0;
        r.Pod(d.owner);
        r.Pod(d.recursion);
        r.Pod(d.pos);
        d.has_pos = r.Pod<uint8_t>() != 0;
        d.name = r.Str();
        r.Pod(d.thread);
        r.Vec(d.described);
        s->objects.push_back(std::move(d));
    }
    r.Vec(s->handles);
    r.Vec(s->ptrs);
    r.Vec(s->timers);
    r.Vec(s->dpcs);
    r.Pod(s->next_handle);
    r.Pod(s->serial);
    // the waits came later: a file without them loads as before
    if (r.ok && r.Left() >= sizeof(uint32_t)) {
        uint32_t count = r.Pod<uint32_t>();
        for (uint32_t i = 0; i < count && r.ok && i < 4096; i++) {
            SavedWait w;
            r.Pod(w.thread);
            r.Vec(w.objects);
            for (uint32_t o : w.objects) r.ok = r.ok && (o < n || o == UINT32_MAX);
            s->waits.push_back(std::move(w));
        }
    }
    // the threads' spots came later still
    if (r.ok && r.Left() >= sizeof(uint32_t)) {
        uint32_t count = r.Pod<uint32_t>();
        for (uint32_t i = 0; i < count && r.ok && i < 4096; i++) {
            SavedSpot spot;
            r.Pod(spot.thread);
            r.Pod(spot.esp);
            r.Vec(spot.code);
            s->spots.push_back(std::move(spot));
        }
    }
    for (const SavedHandle& h : s->handles) r.ok = r.ok && h.object < n;
    for (const SavedPtr& p : s->ptrs) r.ok = r.ok && p.object < n;
    for (const SavedTimer& t : s->timers) r.ok = r.ok && t.object < n;
    if (!r.ok) return nullptr;
    return s;
}

snapshot::PartRef SaveObjects() {
    auto s = std::make_unique<SavedObjects>();
    std::unordered_map<Object*, uint32_t> index;
    std::vector<ObjectRef> refs;
    auto add = [&](const ObjectRef& o) {
        auto [it, fresh] = index.emplace(o.get(), uint32_t(refs.size()));
        if (fresh) refs.push_back(o);
        return it->second;
    };
    std::vector<TimerEntry> armed;
    {
        std::lock_guard<std::mutex> lock(g_timer_lock);
        auto queue = g_timers;
        for (; !queue.empty(); queue.pop()) {
            const TimerEntry& e = queue.top();
            auto it = g_timer_seq.find(e.timer.get());
            if (it != g_timer_seq.end() && it->second == e.seq) armed.push_back(e);  // older entries are stale
        }
    }
    int64_t now = SteadyNs();
    for (const TimerEntry& e : armed) {
        auto t = e.apc_thread.lock();
        s->timers.push_back({add(e.timer), e.period_ms, e.deadline - now, e.dpc, t ? t->id : 0, e.apc_routine,
                             e.apc_context});
    }
    {
        std::lock_guard<std::mutex> lock(g_dpc_lock);
        s->dpcs.assign(g_dpcs.begin(), g_dpcs.end());
    }
    {
        std::lock_guard<std::mutex> lock(g_handles_lock);
        for (auto& [h, o] : g_handles)
            if (o) s->handles.push_back({h, add(o)});
        s->next_handle = g_next_handle;
    }
    {
        std::lock_guard<std::mutex> lock(g_ptr_lock);
        for (auto& [guest, e] : g_by_ptr)
            if (e.obj) s->ptrs.push_back({guest, add(e.obj), e.serial});
        s->serial = g_serial;
    }
    {
        std::lock_guard<std::mutex> lock(g_dispatch);
        Thread* self = Current();
        for (const ThreadRef& t : Threads())
            if (t.get() != self) s->spots.push_back(SpotOf(*t));
        for (const ThreadRef& t : Threads())
            if (t->waiting) {
                SavedWait w{t->id, {}};
                for (const ObjectRef& o : *t->waiting) w.objects.push_back(o ? add(o) : UINT32_MAX);
                s->waits.push_back(std::move(w));
            }
    }
    for (const ObjectRef& o : refs) {
        ObjectDesc d;
        d.type = o->type;
        d.thread = o->type == ObjType::Thread ? static_cast<const Thread&>(*o).id : 0;
        d.described = Described(*o);
        d.live = o;
        s->objects.push_back(std::move(d));
    }
    std::lock_guard<std::mutex> lock(g_dispatch);
    for (ObjectDesc& d : s->objects) {
        Object& o = *d.live;
        d.guest = o.guest;
        d.signal = o.signal;
        d.limit = o.limit;
        d.notification = o.notification;
        d.owner = o.owner;
        d.recursion = o.recursion;
        d.name = o.name;
        if (uint64_t* pos = o.Position()) {
            d.pos = *pos;
            d.has_pos = true;
        }
    }
    return s;
}

}  // namespace kernel
