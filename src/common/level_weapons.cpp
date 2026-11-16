#include "common/level_weapons.h"
#include "common/guest_block.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/cheats.h"
#include "common/level_archive.h"
#include "common/level_sound.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

using kernel::R32;
using kernel::W32;

namespace game {

namespace {

// default.xbe
constexpr uint32_t kHeapSize = 0x3000000;                  // the main heap's block (0x10FFD0)
constexpr uint32_t kBankData = 0x390B3C, kBankSize = 0x390B40;  // the level's anim.bnk in memory (0x113810)
constexpr uint32_t kBankRef = 0x269CC8, kBankObject = 0x269CC0;  // its EAGL object and AnimationBank (0x68BD0)
constexpr uint32_t kTable = 0x267CC0, kTableCount = 0x267CB8;    // {name hash, index}, sorted by hash
constexpr uint32_t kTableRoom = 0x400;                      // up to kBankObject
constexpr uint32_t kArchiveFile = 0x114120;                 // cdecl (name, size out): a file of the level archive
constexpr uint32_t kLoadBank = 0x68BD0, kFindAnimation = 0x68CE0;

constexpr uint32_t kMB = 1u << 20;
constexpr uint32_t kMostExtra = 12 * kMB;  // the heap made larger by up to this
constexpr uint32_t kReserve = 8 * kMB;     // physical memory left for what is allocated after the heap
constexpr uint32_t kMargin = 3 * kMB;      // the heap's extra less what the added files take: for what they make

// campaign levels giving their weapons, in order: the first to hold one gives it
const char* const kDonors[] = {"1_2", "1_3", "1_5", "1_7", "1_9", "1_1", "1_4", "1_8"};
constexpr char kBankLevel[] = "DATA\\2\\2_1\\anim.bnk";

std::atomic<uint32_t> g_heap_extra{0};  // what the heap was given more at boot; 0 in co-op, which has no hooks

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
    return s;
}

// a level archive on the disc image, its directory read once and its files when asked for
class DiscLevel : public levels::Source {
public:
    explicit DiscLevel(const std::string& path) {
        const kernel::Disc* disc = kernel::MountedDisc();
        entry_ = disc ? disc->Find(path) : nullptr;
        if (!entry_ || entry_->dir) return;
        uint8_t head[6];
        if (disc->Read(*entry_, 0, head, 6) != 6) return;
        size_t size = levels::VivDirectorySize(head, 6);
        if (!size || size > entry_->size) return;
        std::vector<uint8_t> dir(size);
        if (disc->Read(*entry_, 0, dir.data(), uint32_t(size)) != size) return;
        entries_ = levels::ParseViv(dir.data(), dir.size());
    }
    const std::vector<levels::VivEntry>& Entries() override { return entries_; }
    std::vector<uint8_t> Read(const levels::VivEntry& e) override {
        std::vector<uint8_t> out(e.size);
        if (!entry_ || kernel::MountedDisc()->Read(*entry_, e.offset, out.data(), e.size) != e.size) return {};
        return out;
    }
    using levels::Source::Read;

private:
    const kernel::Disc::Entry* entry_ = nullptr;
    std::vector<levels::VivEntry> entries_;
};

std::string LevelPath(const char* level) {
    std::string l(level);
    return "DATA\\" + l.substr(0, l.find('_')) + "\\" + l + "\\level.viv";
}

// the campaign level a path under D: is the archive of ("1_2" for data\1\1_2\level.viv), or empty
std::string CampaignLevel(const std::string& path) {
    std::string p = Lower(path);
    std::replace(p.begin(), p.end(), '/', '\\');
    const std::string head = "data\\1\\", tail = "\\level.viv";
    if (!p.starts_with(head) || !p.ends_with(tail)) return {};
    std::string level = p.substr(head.size(), p.size() - head.size() - tail.size());
    if (!level.starts_with("1_") || level.find('\\') != std::string::npos) return {};
    return level;
}

std::mutex g_lock;
// what a level is served: its archive, and its sound event file and script bank with the added weapons' sounds
struct Plan {
    std::string level;
    bool built = false;
    kernel::DiscFileContents archive, events, bank;
};
Plan g_plan;  // the last level asked for: the game opens and sizes its files a few times

constexpr uint32_t kSoundNames = 0x202740;  // the weapons' sound names, by definition +0x60 (0x100CA0)
constexpr uint32_t kSoundNameCount = 52;
constexpr uint32_t kSoundRoom = 2 * kMB;    // heap kept for the script bank's growth

std::string SoundName(int index) {
    if (index < 0 || uint32_t(index) >= kSoundNameCount) return {};
    uint32_t p = R32(kSoundNames + 4 * uint32_t(index));
    return p >= 0x10000 ? kernel::ReadCString(p, 64) : std::string();
}

// sound\<m>_<l>.ubr, the sound event file, or sound\<m><l>scr.abk, the script bank
std::string SoundPath(const std::string& level, bool bank) {
    std::string m = level.substr(0, level.find('_')), l = level.substr(level.find('_') + 1);
    return "DATA\\" + m + "\\" + level + "\\SOUND\\" + (bank ? m + l + "scr.abk" : level + ".ubr");
}

// the campaign level a path under D: is a file of, and which: 0 level.viv, 1 the sound event file, 2 the script bank
std::pair<std::string, int> LevelFile(const std::string& path) {
    std::string p = Lower(path);
    std::replace(p.begin(), p.end(), '/', '\\');
    if (std::string level = CampaignLevel(p); !level.empty()) return {level, 0};
    const std::string head = "data\\1\\";
    if (!p.starts_with(head)) return {};
    size_t slash = p.find('\\', head.size());
    if (slash == std::string::npos) return {};
    std::string level = p.substr(head.size(), slash - head.size());
    if (!level.starts_with("1_")) return {};
    if (p == Lower(SoundPath(level, false))) return {level, 1};
    if (p == Lower(SoundPath(level, true))) return {level, 2};
    return {};
}

void Build(Plan& plan, uint32_t extra) {
    const kernel::Disc* disc = kernel::MountedDisc();
    const std::string& level = plan.level;
    std::vector<uint8_t> own, bank;
    if (!disc || !disc->Load(LevelPath(level.c_str()), own) || !disc->Load(kBankLevel, bank)) {
        LOGE("weapons from other levels: %s or %s not read", LevelPath(level.c_str()).c_str(), kBankLevel);
        return;
    }
    static std::map<std::string, std::unique_ptr<DiscLevel>> donors;
    std::vector<levels::Source*> sources;
    std::map<levels::Source*, std::string> donor_level;
    for (const char* d : kDonors) {
        if (level == d) continue;
        auto& source = donors[d];
        if (!source) source = std::make_unique<DiscLevel>(LevelPath(d));
        if (source->Entries().empty()) continue;
        sources.push_back(source.get());
        donor_level[source.get()] = d;
    }
    size_t budget = extra > kMargin + kSoundRoom ? extra - kMargin - kSoundRoom : 0;
    size_t room = std::min<size_t>(levels::kMaxArchive - own.size(), budget);
    std::optional<levels::Merged> merged = levels::MergeWeapons(own, sources, bank, room);
    if (!merged) {
        LOGR("weapons from other levels: none added to %s (room %zu KB)", level.c_str(), room >> 10);
        return;
    }
    std::string names;
    for (const std::string& w : merged->weapons) names += " " + w;
    LOGR("weapons from other levels: %s has%s added (%zu KB more, room %zu KB)", level.c_str(), names.c_str(),
         merged->added >> 10, room >> 10);

    // their sounds: each weapon's events and samples from the level it came from
    std::vector<uint8_t> weapbul = DiscLevel(LevelPath(level.c_str())).Read(std::string("weapbul_medium.dat"));
    std::vector<uint8_t> events, scr;
    static std::map<std::string, levels::SoundDonor> sound_donors;
    if (!weapbul.empty() && disc->Load(SoundPath(level, false), events) && disc->Load(SoundPath(level, true), scr)) {
        std::vector<levels::SoundWant> wants;
        for (size_t i = 0; i < merged->weapons.size(); i++) {
            std::string name = SoundName(levels::WeaponSoundIndex(weapbul, merged->hashes[i]));
            const std::string& from = donor_level[merged->donors[i]];
            if (name.empty() || from.empty()) continue;
            levels::SoundDonor& d = sound_donors[from];
            if (d.events.empty() &&
                (!disc->Load(SoundPath(from, false), d.events) || !disc->Load(SoundPath(from, true), d.bank)))
                d = {};
            if (d.events.empty()) continue;
            std::vector<uint32_t> want = levels::WeaponSoundEvents(name);
            // and the foley its animation states name
            for (const std::string& state : merged->anim_states[i]) {
                std::vector<uint32_t> named = levels::EventsNamedIn(merged->donors[i]->Read(state), d.events);
                want.insert(want.end(), named.begin(), named.end());
            }
            wants.push_back({&d, std::move(want)});
        }
        if (std::optional<levels::SoundMerged> sounds = levels::MergeSounds(events, scr, wants)) {
            LOGR("weapons from other levels: %s sounds: %d events, %d samples (%zu KB more)", level.c_str(),
                 sounds->records, sounds->samples, (sounds->bank.size() - scr.size()) >> 10);
            plan.events = std::make_shared<const std::vector<uint8_t>>(std::move(sounds->events));
            plan.bank = std::make_shared<const std::vector<uint8_t>>(std::move(sounds->bank));
        } else {
            LOGR("weapons from other levels: %s sounds: none added", level.c_str());
        }
    }
    plan.archive = std::make_shared<const std::vector<uint8_t>>(std::move(merged->archive));
}

kernel::DiscFileContents Provide(const std::string& path) {
    // MOHRS_LEVEL_WEAPONS=0: the levels as they are on the disc
    static const bool enabled = [] {
        const char* v = std::getenv("MOHRS_LEVEL_WEAPONS");
        return !v || *v != '0';
    }();
    uint32_t extra = g_heap_extra.load();
    // every campaign level, the cheat on or not: the added weapons wait unowned in the ring, so switching Give All
    // Weapons on in a level gives them at once
    if (!enabled || !extra) return nullptr;
    auto [level, file] = LevelFile(path);
    if (level.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_plan.built || g_plan.level != level) {
        g_plan = {level, true, nullptr, nullptr, nullptr};
        Build(g_plan, extra);
    }
    return file == 0 ? g_plan.archive : file == 1 ? g_plan.events : g_plan.bank;
}

// The added animation bank's EAGL object (held) and AnimationBank are kept in guest memory, in a block allocated at
// the same point of every boot (the main heap's allocation), so a quick load puts them back with the rest: a run that
// loads a save made in a level (from the shell, say) never loaded the bank itself. Its name index is made again from
// the AnimationBank whenever that changes (game thread).
uint32_t g_bank_block = 0;  // {kBankMagic, ref, object}
constexpr uint32_t kBankMagic = 0x4B4E4142;  // "BANK"
struct BankIndex {
    uint32_t object = 0;
    std::map<uint32_t, uint32_t> index;  // name hash -> index
};
BankIndex g_index;
uint32_t g_host_bank[3] = {0, 0, 0};  // when no block could be had
bool g_loading = false;

uint32_t BankWord(int i) {
    if (!g_bank_block) return g_host_bank[i];
    return R32(g_bank_block) == kBankMagic ? R32(g_bank_block + 4 * uint32_t(i)) : 0;
}
void SetBank(uint32_t ref, uint32_t object) {
    if (!g_bank_block) {
        g_host_bank[0] = kBankMagic, g_host_bank[1] = ref, g_host_bank[2] = object;
        return;
    }
    W32(g_bank_block, kBankMagic);
    W32(g_bank_block + 4, ref);
    W32(g_bank_block + 8, object);
}
uint32_t BankRef() { return BankWord(1); }
uint32_t BankObject() { return BankWord(2); }

// as the bank load builds the level's table (0x68BD0): each animation's name "S_0x<hash>" at [object+0x14][i], the
// count at object+4
const std::map<uint32_t, uint32_t>& Index() {
    uint32_t object = BankObject();
    if (object == g_index.object) return g_index.index;
    g_index = {object, {}};
    if (object < 0x10000) return g_index.index;
    uint32_t count = std::min(R32(object + 4), kTableRoom), names = R32(object + 0x14);
    for (uint32_t i = 0; i < count && names >= 0x10000; i++) {
        uint32_t name = R32(names + 4 * i);
        unsigned hash = 0;
        if (name >= 0x10000 && std::sscanf(static_cast<const char*>(kernel::HostPtr(name)), "S_0x%08x", &hash) == 1)
            g_index.index[hash] = i;
    }
    return g_index.index;
}

void Release(X86Context& ctx) {
    uint32_t ref = BankRef();
    SetBank(0, 0);
    if (ref < 0x10000) return;
    uint32_t count = R32(ref + 4) - 1;
    W32(ref + 4, count);
    if (count) return;
    uint32_t saved = ctx.ecx;
    ctx.ecx = ref;
    kernel::CallGuest(ctx, R32(R32(ref)), {1});  // its deleting destructor
    ctx.ecx = saved;
}

}  // namespace

void InstallLevelWeapons() { kernel::SetDiscFileProvider(Provide); }

void HeapAlloc(X86Context& ctx, uint8_t* base, GuestFunction original) {
    static bool done = false;
    uint32_t size = R32(ctx.esp + 4);
    if (done || size != kHeapSize) {
        original(ctx, base);
        return;
    }
    done = true;
    g_bank_block = mem::AllocSystem(16, 16);
    if (g_bank_block) SetBank(0, 0);
    mem::Stats s = mem::Query();
    uint64_t free = uint64_t(mem::kPhysicalSize - mem::kKernelDataSize) - uint64_t(s.contiguous_used) * mem::kPage;
    uint64_t room = free > uint64_t(size) + kReserve ? free - size - kReserve : 0;
    uint32_t extra = uint32_t(std::min<uint64_t>(room, kMostExtra)) / kMB * kMB;
    if (const char* v = std::getenv("MOHRS_HEAP_EXTRA_MB")) extra = std::min<uint32_t>(uint32_t(std::atoi(v)), 32) * kMB;
    // the game stops on a failed allocation: the larger block is tried first
    if (extra) {
        uint32_t trial = mem::AllocContiguous(size + extra, 0, 0xFFFFFFFF, mem::kPage);
        if (trial) mem::FreeContiguous(trial);
        else extra = 0;
    }
    W32(ctx.esp + 4, size + extra);
    original(ctx, base);
    if (!ctx.eax && extra) LOGE("heap: %u MB more could not be had", extra / kMB);
    if (ctx.eax) g_heap_extra = extra;
    LOGR("heap: %u MB (%u MB more), %llu MB of physical memory was free", (size + extra) / kMB, extra / kMB,
         (unsigned long long)(free / kMB));
}

void LoadWeaponBank(X86Context& ctx, uint8_t* base, GuestFunction original) {
    original(ctx, base);
    if (g_loading) return;
    // kept, again after a quick load that took them (src/common/guest_block.h)
    static GuestBlock name_block, size_block;
    uint32_t name = name_block.HoldText(levels::kBankFile);
    uint32_t size_out = size_block.Get(4);
    if (!name || !size_out) return;
    W32(size_out, 0);
    uint32_t data = kernel::CallGuest(ctx, kArchiveFile, {name, size_out});
    if (!data) return;
    if (BankRef()) Release(ctx);
    uint32_t count = std::min(R32(kTableCount), kTableRoom);
    std::vector<uint8_t> table(count * 8);
    std::memcpy(table.data(), kernel::HostPtr(kTable), table.size());
    uint32_t saved[] = {R32(kBankData), R32(kBankSize), R32(kBankRef), R32(kBankObject)};
    // the bank load as for the level's, with its globals at the added bank
    W32(kBankData, data);
    W32(kBankSize, R32(size_out));
    W32(kBankRef, 0);
    W32(kTableCount, 0);
    g_loading = true;
    kernel::CallGuest(ctx, kLoadBank, {});
    g_loading = false;
    SetBank(R32(kBankRef), R32(kBankObject));
    uint32_t n = std::min(R32(kTableCount), kTableRoom);
    // the index made from the bank as the load's table has it
    const std::map<uint32_t, uint32_t>& index = Index();
    uint32_t differ = index.size() == n ? 0 : 1;
    for (uint32_t i = 0; i < n; i++) {
        auto it = index.find(R32(kTable + 8 * i));
        if (it == index.end() || it->second != R32(kTable + 8 * i + 4)) differ++;
    }
    if (differ) LOGW("weapons from other levels: the added bank's index differs from its table in %u places", differ);
    // the level's bank as it was
    std::memcpy(kernel::HostPtr(kTable), table.data(), table.size());
    W32(kTableCount, count);
    W32(kBankData, saved[0]);
    W32(kBankSize, saved[1]);
    W32(kBankRef, saved[2]);
    W32(kBankObject, saved[3]);
    LOGR("weapons from other levels: the added animation bank holds %u animations", n);
    if (!BankObject()) Release(ctx);
}

void FindAnimation(X86Context& ctx, uint8_t* base, GuestFunction original) {
    uint32_t out = R32(ctx.esp + 4), key = R32(ctx.esp + 8), bank = R32(ctx.esp + 12);
    original(ctx, base);
    if (bank || !BankObject() || R32(out)) return;
    const std::map<uint32_t, uint32_t>& index = Index();
    auto it = index.find(key);
    if (it == index.end()) return;
    // the lookup given a bank takes the animation's index in it
    kernel::CallGuest(ctx, kFindAnimation, {out, it->second, BankObject()});
    ctx.eax = out;
}

void ReleaseWeaponBank(X86Context& ctx, uint8_t* base, GuestFunction original) {
    original(ctx, base);
    if (BankRef()) Release(ctx);
}

}  // namespace game
