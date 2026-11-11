#include "common/level_archive.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace game::levels {

namespace {

constexpr uint32_t kAlign = 0x80;  // the archives' files start on 0x80
constexpr char kDatabase[] = "levelfile.lfc";

// the database's classes this needs
constexpr uint32_t kInstance = 0x8, kScript = 0x9, kAnimState = 0xA, kWeapon = 0x21;
// a weapon entry's fields
constexpr size_t kName = 7, kHash = 9, kFields = 36;
// the weapon's world model: a .msh is a static mesh whose ELF blocks take their textures from the level's own
// texture data (0x10D6F0, 0x113360), so another level's does not load; and a missing one is not checked for
constexpr size_t kWorldModel = 2;
constexpr char kModel[] = ".msh";
// string fields: type 0 a file, type 4 a name
constexpr uint32_t kFile = 0, kNameType = 4;
constexpr char kWeaponBase[] = "PlayerWeapon_Base";
// a script's base class name, in its header
constexpr size_t kBaseAt = 0x24, kBaseEnd = 0x64;
// the fire extinguisher's animations are in 1_1's bank only
constexpr char kSkipped[] = "FireExtinguisher";

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(std::tolower(uint8_t(c))); });
    return s;
}

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, d.data() + at, 4);
    return v;
}

void Put32(std::vector<uint8_t>& d, uint32_t v) {
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    d.insert(d.end(), b, b + 4);
}

uint32_t Be(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

void PutBe(std::vector<uint8_t>& d, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; i--) d.push_back(uint8_t(v >> (8 * i)));
}

// levelfile.lfc: a header (version, ?, count, table offset), the table of {class, field count, field array}, each
// field {type, count, values}; offsets are from the file's start, which the game adds when it loads it (0x114370)
struct Field {
    uint32_t type = 0;
    std::vector<uint32_t> values;
    std::vector<std::string> strings;  // type 0 and 4: the values' strings, empty for a null
};
struct Entry {
    uint32_t cls = 0;
    std::vector<Field> fields;

    std::string Str(size_t field) const {
        return field < fields.size() && !fields[field].strings.empty() ? fields[field].strings[0] : std::string();
    }
};

bool IsString(uint32_t type) { return type == kFile || type == kNameType; }

std::string CStr(const std::vector<uint8_t>& d, uint32_t at) {
    if (!at || at >= d.size()) return {};
    const char* p = reinterpret_cast<const char*>(d.data() + at);
    return std::string(p, strnlen(p, d.size() - at));
}

std::optional<std::vector<Entry>> ParseDatabase(const std::vector<uint8_t>& d) {
    if (d.size() < 16) return std::nullopt;
    uint32_t count = U32(d, 8), table = U32(d, 12);
    if (uint64_t(table) + uint64_t(count) * 12 > d.size()) return std::nullopt;
    std::vector<Entry> out(count);
    for (uint32_t i = 0; i < count; i++) {
        Entry& e = out[i];
        e.cls = U32(d, table + 12 * i);
        uint32_t n = U32(d, table + 12 * i + 4), fields = U32(d, table + 12 * i + 8);
        if (n > 256 || uint64_t(fields) + 4ull * n > d.size()) return std::nullopt;
        for (uint32_t k = 0; k < n; k++) {
            uint32_t f = U32(d, fields + 4 * k);
            Field field;
            field.type = U32(d, f);
            uint32_t values = U32(d, f + 4);
            if (values > 256 || uint64_t(f) + 8 + 4ull * values > d.size()) return std::nullopt;
            for (uint32_t v = 0; v < values; v++) {
                field.values.push_back(U32(d, f + 8 + 4 * v));
                if (IsString(field.type)) field.strings.push_back(CStr(d, field.values.back()));
            }
            e.fields.push_back(std::move(field));
        }
    }
    return out;
}

// the database with entries added: its own bytes stay, a new table (its entries, then the new) and the new
// entries' fields and strings go after them
std::vector<uint8_t> AddEntries(const std::vector<uint8_t>& d, const std::vector<Entry>& added) {
    std::vector<uint8_t> out = d;
    auto align = [&] {
        while (out.size() % 4) out.push_back(0);
    };
    align();
    std::vector<uint32_t> arrays;
    for (const Entry& e : added) {
        std::vector<uint32_t> records;
        for (const Field& f : e.fields) {
            std::vector<uint32_t> values = f.values;
            if (IsString(f.type)) {
                for (size_t v = 0; v < values.size(); v++) {
                    if (!values[v]) continue;
                    values[v] = uint32_t(out.size());
                    const std::string& s = f.strings[v];
                    out.insert(out.end(), s.begin(), s.end());
                    out.push_back(0);
                    align();
                }
            }
            records.push_back(uint32_t(out.size()));
            Put32(out, f.type);
            Put32(out, uint32_t(values.size()));
            for (uint32_t v : values) Put32(out, v);
        }
        arrays.push_back(uint32_t(out.size()));
        for (uint32_t r : records) Put32(out, r);
    }
    uint32_t count = U32(d, 8), table = U32(d, 12);
    uint32_t at = uint32_t(out.size());
    out.insert(out.end(), d.begin() + table, d.begin() + table + 12 * count);
    for (size_t i = 0; i < added.size(); i++) {
        Put32(out, added[i].cls);
        Put32(out, uint32_t(added[i].fields.size()));
        Put32(out, arrays[i]);
    }
    uint32_t total = count + uint32_t(added.size());
    std::memcpy(out.data() + 8, &total, 4);
    std::memcpy(out.data() + 12, &at, 4);
    return out;
}

// The level settings entry (class 0x1B) sizes the object pools 0xCB5E0 makes: field 57 the weapon holders' (0x408
// bytes each, 6 when the field is unset), which campaign levels leave at 6 and the multiplayer maps set to 16. A new
// record is written for it (the old may be shared) and the field array pointed at it.
constexpr uint32_t kSettings = 0x1B;
constexpr size_t kHolderPool = 57;
constexpr uint32_t kHolderPoolDefault = 6, kInt = 1;

void SetHolderPool(std::vector<uint8_t>& d, uint32_t holders) {
    uint32_t count = U32(d, 8), table = U32(d, 12);
    for (uint32_t i = 0; i < count; i++) {
        if (U32(d, table + 12 * i) != kSettings) continue;
        uint32_t n = U32(d, table + 12 * i + 4), fields = U32(d, table + 12 * i + 8);
        if (n <= kHolderPool) return;
        uint32_t record = U32(d, fields + 4 * kHolderPool);
        uint32_t now = U32(d, record) == kInt ? U32(d, record + 8) : kHolderPoolDefault;
        if (now >= holders) return;
        while (d.size() % 4) d.push_back(0);
        uint32_t at = uint32_t(d.size());
        Put32(d, kInt);
        Put32(d, 1);
        Put32(d, holders);
        std::memcpy(d.data() + fields + 4 * kHolderPool, &at, 4);
        return;
    }
}

Entry FileEntry(uint32_t cls, const std::string& file) {
    Field f;
    f.type = kFile;
    f.values = {1};  // any non-null: the string is written in its place
    f.strings = {file};
    return {cls, {f}};
}

// a weapon a donor holds and what it takes
struct Weapon {
    std::string name;
    Source* donor = nullptr;
    Entry entry;
    std::vector<std::string> files;  // every file it names, its scripts and their instances
    std::vector<Entry> extra;        // the script, instance and animation state entries
};

// the script whose base is PlayerWeapon_Base holding the weapon's hash, or empty: not every weapon has one
std::string ScriptOf(Source& donor, uint32_t hash) {
    uint8_t key[4];
    std::memcpy(key, &hash, 4);
    for (const VivEntry& e : donor.Entries()) {
        if (!Lower(e.name).ends_with(".cbs") || e.size < kBaseEnd || e.size > 0x10000) continue;
        std::vector<uint8_t> d = donor.Read(e);
        const char* at = reinterpret_cast<const char*>(d.data() + kBaseAt);
        std::string base(at, strnlen(at, kBaseEnd - kBaseAt));
        if (base != kWeaponBase) continue;
        if (std::search(d.begin(), d.end(), key, key + 4) != d.end()) return e.name.substr(0, e.name.size() - 4);
    }
    return {};
}

std::optional<Weapon> FromDonor(Source& donor, const Entry& e) {
    Weapon w;
    w.name = e.Str(kName);
    w.donor = &donor;
    w.entry = e;
    for (const Field& f : e.fields)
        if (f.type == kFile)
            for (const std::string& s : f.strings)
                if (!s.empty()) w.files.push_back(s);
    if (e.fields[kHash].values.empty()) return std::nullopt;
    std::string script = ScriptOf(donor, e.fields[kHash].values[0]);
    if (!script.empty()) {
        w.files.push_back(script + ".cbs");
        w.extra.push_back(FileEntry(kScript, script + ".cbs"));
        if (donor.Find(script + ".sin")) {
            w.files.push_back(script + ".sin");
            w.extra.push_back(FileEntry(kInstance, script + ".sin"));
        }
    }
    // its animation states, loaded at the level's start (0xCB5E0)
    for (const Field& f : e.fields)
        if (f.type == kFile)
            for (const std::string& s : f.strings)
                if (Lower(s).ends_with(".aso")) w.extra.push_back(FileEntry(kAnimState, s));
    return w;
}

struct Out {
    std::string name;
    const std::vector<uint8_t>* level = nullptr;  // a file of the level's own archive
    uint32_t offset = 0, size = 0;
    std::vector<uint8_t> data;  // or its own bytes
};

std::vector<uint8_t> WriteViv(const std::vector<Out>& files) {
    std::vector<uint8_t> dir;
    for (const Out& f : files) dir.insert(dir.end(), f.name.size() + 7, 0);
    size_t dir_end = 6 + dir.size();
    size_t at = (dir_end + kAlign - 1) / kAlign * kAlign;
    std::vector<uint32_t> offsets;
    for (const Out& f : files) {
        offsets.push_back(uint32_t(at));
        uint32_t size = f.level ? f.size : uint32_t(f.data.size());
        at = (at + size + kAlign - 1) / kAlign * kAlign;
    }
    std::vector<uint8_t> out;
    out.reserve(at);
    out.push_back(0xC0);
    out.push_back(0xFB);
    PutBe(out, uint32_t(dir_end - 4), 2);
    PutBe(out, uint32_t(files.size()), 2);
    for (size_t i = 0; i < files.size(); i++) {
        PutBe(out, offsets[i], 3);
        PutBe(out, files[i].level ? files[i].size : uint32_t(files[i].data.size()), 3);
        out.insert(out.end(), files[i].name.begin(), files[i].name.end());
        out.push_back(0);
    }
    for (size_t i = 0; i < files.size(); i++) {
        out.resize(offsets[i], 0);
        const Out& f = files[i];
        if (f.level) out.insert(out.end(), f.level->begin() + f.offset, f.level->begin() + f.offset + f.size);
        else out.insert(out.end(), f.data.begin(), f.data.end());
    }
    // the last file ends the archive, as the game's own do
    return out;
}

}  // namespace

size_t VivDirectorySize(const uint8_t* head, size_t size) {
    if (size < 6 || head[0] != 0xC0 || head[1] != 0xFB) return 0;
    return size_t(Be(head + 2, 2)) + 4;
}

std::vector<VivEntry> ParseViv(const uint8_t* d, size_t size) {
    size_t end = VivDirectorySize(d, size);
    if (!end || end > size) return {};
    uint32_t count = Be(d + 4, 2);
    std::vector<VivEntry> out;
    size_t at = 6;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 7 > end) return {};
        VivEntry e;
        e.offset = Be(d + at, 3);
        e.size = Be(d + at + 3, 3);
        at += 6;
        size_t n = strnlen(reinterpret_cast<const char*>(d + at), end - at);
        if (at + n >= end) return {};
        e.name.assign(reinterpret_cast<const char*>(d + at), n);
        at += n + 1;
        out.push_back(std::move(e));
    }
    return out;
}

const VivEntry* Source::Find(const std::string& name) {
    std::string want = Lower(name);
    for (const VivEntry& e : Entries())
        if (Lower(e.name) == want) return &e;
    return nullptr;
}

std::vector<uint8_t> Source::Read(const std::string& name) {
    const VivEntry* e = Find(name);
    return e ? Read(*e) : std::vector<uint8_t>{};
}

std::optional<Merged> MergeWeapons(const std::vector<uint8_t>& level, const std::vector<Source*>& donors,
                                   const std::vector<uint8_t>& bank, size_t most_added) {
    std::vector<VivEntry> own = ParseViv(level.data(), level.size());
    if (own.empty()) return std::nullopt;
    const VivEntry* db_entry = nullptr;
    std::set<std::string> have_files;
    for (const VivEntry& e : own) {
        if (uint64_t(e.offset) + e.size > level.size()) return std::nullopt;
        have_files.insert(Lower(e.name));
        if (Lower(e.name) == kDatabase) db_entry = &e;
    }
    if (!db_entry) return std::nullopt;
    std::vector<uint8_t> db(level.begin() + db_entry->offset, level.begin() + db_entry->offset + db_entry->size);
    std::optional<std::vector<Entry>> entries = ParseDatabase(db);
    if (!entries) return std::nullopt;
    std::set<std::string> have_weapons, listed;
    std::string world_model;  // the level's first weapon's, which added weapons show as
    for (const Entry& e : *entries) {
        if (e.cls == kWeapon && e.fields.size() >= kFields) {
            have_weapons.insert(e.Str(kName));
            if (world_model.empty()) world_model = e.Str(kWorldModel);
        }
        if (e.cls == kScript || e.cls == kInstance || e.cls == kAnimState) listed.insert(Lower(e.Str(0)));
    }
    // a level without player weapons (a cut scene level, the front end) gets none
    if (have_weapons.empty()) return std::nullopt;

    std::vector<Weapon> weapons;
    std::set<std::string> taken = have_weapons;
    for (Source* donor : donors) {
        std::vector<uint8_t> d = donor->Read(kDatabase);
        std::optional<std::vector<Entry>> de = ParseDatabase(d);
        if (!de) continue;
        for (const Entry& e : *de) {
            if (e.cls != kWeapon || e.fields.size() < kFields) continue;
            std::string name = e.Str(kName);
            if (name.empty() || name == kSkipped || taken.count(name)) continue;
            std::optional<Weapon> w = FromDonor(*donor, e);
            if (!w) continue;
            bool complete = true;
            for (const std::string& f : w->files) complete = complete && (have_files.count(Lower(f)) || donor->Find(f));
            if (!complete) continue;
            taken.insert(name);
            weapons.push_back(std::move(*w));
        }
    }
    if (weapons.empty() || bank.empty()) return std::nullopt;

    // the level's files as they are, the database with the new entries, then the files new to it
    Merged merged;
    std::vector<Out> files;
    for (const VivEntry& e : own) files.push_back({e.name, &level, e.offset, e.size, {}});
    size_t db_index = size_t(db_entry - own.data());
    std::vector<Entry> added;
    size_t growth = bank.size() + kAlign;
    std::set<std::string> adding;
    std::vector<Out> extra;
    for (Weapon& w : weapons) {
        // a world model the level has: its own or, when it lacks it, its first weapon's
        Field& model = w.entry.fields[kWorldModel];
        if (!model.strings.empty() && !have_files.count(Lower(model.strings[0])) && !world_model.empty())
            model.strings[0] = world_model;
        std::vector<Out> mine;
        size_t bytes = 0;
        for (const std::string& f : w.files) {
            std::string key = Lower(f);
            if (have_files.count(key) || adding.count(key) || key.ends_with(kModel)) continue;
            Out o;
            o.name = f;
            o.data = w.donor->Read(f);
            bytes += o.data.size() + kAlign + f.size() + 7;
            mine.push_back(std::move(o));
        }
        // too much for the archive or the heap: the weapons after it would be too
        if (growth + bytes > most_added) break;
        growth += bytes;
        for (Out& o : mine) {
            adding.insert(Lower(o.name));
            extra.push_back(std::move(o));
        }
        for (const Entry& e : w.extra)
            if (!listed.count(Lower(e.Str(0)))) {
                listed.insert(Lower(e.Str(0)));
                added.push_back(e);
            }
        added.push_back(w.entry);
        merged.weapons.push_back(w.name);
        merged.hashes.push_back(w.entry.fields[kHash].values[0]);
        merged.donors.push_back(w.donor);
        std::vector<std::string> states;
        for (const std::string& f : w.files)
            if (Lower(f).ends_with(".aso")) states.push_back(f);
        merged.anim_states.push_back(std::move(states));
    }
    if (merged.weapons.empty()) return std::nullopt;
    std::vector<uint8_t> patched = AddEntries(db, added);
    // a holder each, with the multiplayer maps' room (16 for 11 weapons)
    SetHolderPool(patched, uint32_t(have_weapons.size() + merged.weapons.size() + 5));
    files[db_index] = {own[db_index].name, nullptr, 0, 0, std::move(patched)};
    for (Out& o : extra) files.push_back(std::move(o));
    files.push_back({kBankFile, nullptr, 0, 0, bank});
    merged.archive = WriteViv(files);
    if (merged.archive.size() > kMaxArchive) return std::nullopt;
    merged.added = merged.archive.size() > level.size() ? merged.archive.size() - level.size() : 0;
    return merged;
}

}  // namespace game::levels
