#include "common/level_sound.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace game::levels {

namespace {

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, d.data() + at, 4);
    return v;
}
uint16_t U16(const std::vector<uint8_t>& d, size_t at) {
    uint16_t v = 0;
    if (at + 2 <= d.size()) std::memcpy(&v, d.data() + at, 2);
    return v;
}
void Set32(std::vector<uint8_t>& d, size_t at, uint32_t v) { std::memcpy(d.data() + at, &v, 4); }
void Set16(std::vector<uint8_t>& d, size_t at, uint16_t v) { std::memcpy(d.data() + at, &v, 2); }

// the event file: "UBER", ?, version, flag, column count, record count, record size, trailer count; the columns
// {name hash, type, offset}; the records; a trailer kept as it is
constexpr uint32_t kUber = 0x52454255;
constexpr uint32_t kHashType = 6;                 // the column the records are sorted by
constexpr uint32_t kBankColumn = 0x7FFF5F24;      // the banks the sample is in, a bit each (0x106570: +0xA4)
constexpr uint32_t kSampleColumn = 0xBC1069FF;    // the sample's index in its bank (+0xA0)
constexpr uint32_t kScriptBank = 1u << 30;        // bank 31, <m><l>scr.abk

struct Column {
    uint32_t name, type, at;
};
// a column's bytes by its type (0x106570): 0 and 1 a byte, 2 a short, the rest four bytes
size_t ColumnSize(uint32_t type) { return type <= 1 ? 1 : type == 2 ? 2 : 4; }

struct Events {
    uint32_t count = 0, size = 0;
    size_t records = 0;  // where the records start
    uint32_t hash_at = 0, bank_at = 0, sample_at = 0;
    std::vector<Column> columns;

    uint32_t Hash(const std::vector<uint8_t>& d, uint32_t i) const { return U32(d, records + size * i + hash_at); }
};

// a record of another level's file in this one's columns (levels differ: 1_7 has a column fewer); a column the
// other lacks is zero
std::vector<uint8_t> Convert(const std::vector<uint8_t>& record, const Events& from, const Events& to) {
    std::vector<uint8_t> out(to.size, 0);
    for (const Column& c : to.columns)
        for (const Column& f : from.columns)
            if (f.name == c.name && f.type == c.type) {
                std::memcpy(out.data() + c.at, record.data() + f.at, ColumnSize(c.type));
                break;
            }
    return out;
}

std::optional<Events> ParseEvents(const std::vector<uint8_t>& d) {
    if (d.size() < 0x20 || U32(d, 0) != kUber) return std::nullopt;
    Events e;
    uint32_t columns = U32(d, 0x10);
    e.count = U32(d, 0x14);
    e.size = U32(d, 0x18);
    e.records = 0x20 + size_t(columns) * 12;
    if (columns > 64 || e.size > 256 || e.records + size_t(e.count) * e.size > d.size()) return std::nullopt;
    bool hash = false, bank = false, sample = false;
    for (uint32_t c = 0; c < columns; c++) {
        Column col{U32(d, 0x20 + 12 * c), U32(d, 0x24 + 12 * c), U32(d, 0x28 + 12 * c)};
        if (col.at + ColumnSize(col.type) > e.size) return std::nullopt;
        if (col.type == kHashType && !hash) e.hash_at = col.at, hash = true;
        if (col.name == kBankColumn && col.type == 4) e.bank_at = col.at, bank = true;
        if (col.name == kSampleColumn && col.type == 2) e.sample_at = col.at, sample = true;
        e.columns.push_back(col);
    }
    if (!hash || !bank || !sample) return std::nullopt;
    return e;
}

// a bank: "BNKl", version, count, header size, data size, 0; offsets to each sample's PT header, each from its own
// place; the PT headers (tags: byte tag, byte length, big endian value; 0xFC-0xFE markers, 0xFF the end) up to the
// header size; the data. Tags 0x88 and 0x89 are where a sample's data (and a second channel's) is in the file: the
// data part, or for some codecs (0x04) inside the header part after the PT headers.
constexpr uint32_t kBank = 0x6C4B4E42;
constexpr uint8_t kData = 0x88, kData2 = 0x89;

struct Sample {
    size_t at = 0, size = 0;            // its PT header
    std::vector<size_t> offset_tags;    // where in the header a data offset is (4 bytes, big endian)
};
struct Bank {
    uint32_t count = 0, header = 0, data = 0;
    std::vector<Sample> samples;
};

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void PutBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

std::optional<Bank> ParseBank(const std::vector<uint8_t>& d) {
    if (d.size() < 0x14 || U32(d, 0) != kBank) return std::nullopt;
    Bank b;
    b.count = U16(d, 6);
    b.header = U32(d, 8);
    b.data = U32(d, 12);
    if (size_t(b.header) + b.data != d.size() || 0x14 + 4ull * b.count > b.header) return std::nullopt;
    std::vector<size_t> heads;
    for (uint32_t i = 0; i < b.count; i++) heads.push_back(0x14 + 4 * i + U32(d, 0x14 + 4 * i));
    for (uint32_t i = 0; i < b.count; i++) {
        Sample s;
        s.at = heads[i];
        size_t end = i + 1 < b.count ? heads[i + 1] : b.header;
        if (end <= s.at || end > b.header || d[s.at] != 'P' || d[s.at + 1] != 'T') return std::nullopt;
        s.size = end - s.at;
        for (size_t j = s.at + 4; j < end;) {
            uint8_t t = d[j];
            if (t == 0xFF) break;
            if (t >= 0xFC) {
                j++;
                continue;
            }
            if (j + 2 > end) return std::nullopt;
            uint8_t n = d[j + 1];
            if (j + 2 + n > end) return std::nullopt;
            if (t == kData || t == kData2) {
                if (n != 4) return std::nullopt;
                uint32_t v = Be32(&d[j + 2]);
                if (v < 0x14 + 4 * b.count || v >= d.size()) return std::nullopt;
                s.offset_tags.push_back(j + 2 - s.at);
            }
            j += 2 + n;
        }
        if (s.offset_tags.empty()) return std::nullopt;
        b.samples.push_back(std::move(s));
    }
    return b;
}

}  // namespace

uint32_t NameHash(const std::string& name) {
    uint32_t crc = 0xFFFFFFFF;
    for (char c : name) {
        crc ^= uint8_t(c);
        for (int k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

std::vector<uint32_t> WeaponSoundEvents(const std::string& sound_name) {
    std::vector<uint32_t> out;
    for (const char* kind : {"WpnFire_", "WpnSlct_", "WpnRld_"}) {
        std::string base = kind + sound_name;
        out.push_back(NameHash(base));
        for (int zone = 1; zone <= 9; zone++) out.push_back(NameHash(base + "_Zone" + std::to_string(zone)));
    }
    return out;
}

std::vector<uint32_t> EventsNamedIn(const std::vector<uint8_t>& file, const std::vector<uint8_t>& events) {
    std::optional<Events> e = ParseEvents(events);
    if (!e || file.size() < 4) return {};
    std::set<uint32_t> all, found;
    for (uint32_t i = 0; i < e->count; i++) all.insert(e->Hash(events, i));
    for (size_t at = 0; at + 4 <= file.size(); at++)
        if (uint32_t v = U32(file, at); all.count(v)) found.insert(v);
    return {found.begin(), found.end()};
}

int WeaponSoundIndex(const std::vector<uint8_t>& weapbul, uint32_t weapon_hash) {
    // weapons, bullets, projectiles; then {kind, index} and the 0x6C byte definition, its hash first
    constexpr size_t kHead = 12, kRecord = 4 + 0x6C, kSound = 0x60;
    uint32_t weapons = U32(weapbul, 0);
    for (uint32_t i = 0; i < weapons && kHead + (i + 1) * kRecord <= weapbul.size(); i++) {
        size_t def = kHead + i * kRecord + 4;
        if (U32(weapbul, def) == weapon_hash) return U16(weapbul, def + kSound);
    }
    return -1;
}

std::optional<SoundMerged> MergeSounds(const std::vector<uint8_t>& events, const std::vector<uint8_t>& bank,
                                       const std::vector<SoundWant>& wants) {
    std::optional<Events> te = ParseEvents(events);
    std::optional<Bank> tb = ParseBank(bank);
    if (!te || !tb) return std::nullopt;
    std::set<uint32_t> have;
    for (uint32_t i = 0; i < te->count; i++) have.insert(te->Hash(events, i));

    // what is taken: records (their sample index still the donor's), and per donor its samples in order
    struct Taken {
        const SoundDonor* donor;
        std::vector<uint8_t> record;
        uint32_t sample;
    };
    std::vector<Taken> taken;
    std::map<const SoundDonor*, std::vector<uint32_t>> samples;  // donor -> its sample indexes, in order of use
    std::map<const SoundDonor*, Bank> donor_banks;
    for (const SoundWant& w : wants) {
        if (!w.donor) continue;
        std::optional<Events> de = ParseEvents(w.donor->events);
        if (!de) continue;
        auto bit = donor_banks.find(w.donor);
        if (bit == donor_banks.end()) {
            std::optional<Bank> db = ParseBank(w.donor->bank);
            if (!db) continue;
            bit = donor_banks.emplace(w.donor, std::move(*db)).first;
        }
        std::set<uint32_t> want(w.events.begin(), w.events.end());
        for (uint32_t i = 0; i < de->count; i++) {
            uint32_t h = de->Hash(w.donor->events, i);
            if (!want.count(h) || have.count(h)) continue;
            size_t at = de->records + size_t(de->size) * i;
            std::vector<uint8_t> record(w.donor->events.begin() + at, w.donor->events.begin() + at + de->size);
            if (U32(record, de->bank_at) != kScriptBank) continue;
            uint32_t sample = U16(record, de->sample_at);
            record = Convert(record, *de, *te);
            if (sample >= bit->second.count) continue;
            // only a sample in the data part is taken
            const Sample& from = bit->second.samples[sample];
            bool in_data = true;
            for (size_t t : from.offset_tags) in_data = in_data && Be32(&w.donor->bank[from.at + t]) >= bit->second.header;
            if (!in_data) continue;
            auto& list = samples[w.donor];
            if (std::find(list.begin(), list.end(), sample) == list.end()) list.push_back(sample);
            taken.push_back({w.donor, std::move(record), sample});
        }
        // an event is taken whole from the first donor that has it
        for (const Taken& t : taken) have.insert(U32(t.record, te->hash_at));
    }
    if (taken.empty()) return std::nullopt;

    // the bank: the new samples' headers after the level's, their data after the level's
    std::vector<std::pair<const SoundDonor*, uint32_t>> added;  // in the new bank's order
    for (auto& [donor, list] : samples)
        for (uint32_t s : list) added.emplace_back(donor, s);
    uint32_t count = tb->count + uint32_t(added.size());
    size_t headers = 0;
    for (const Sample& s : tb->samples) headers += s.size;
    for (auto& [donor, s] : added) headers += donor_banks[donor].samples[s].size;
    size_t header = (0x14 + 4 * size_t(count) + headers + 15) / 16 * 16;
    uint32_t delta = uint32_t(header - tb->header);

    std::vector<uint8_t> out(header, 0);
    std::memcpy(out.data(), bank.data(), 0x14);
    Set16(out, 6, uint16_t(count));
    Set32(out, 8, uint32_t(header));
    // the level's data moves by delta
    out.insert(out.end(), bank.begin() + tb->header, bank.end());
    size_t at = 0x14 + 4 * size_t(count);
    uint32_t index = 0;
    auto place_header = [&](const std::vector<uint8_t>& src, const Sample& s) {
        Set32(out, 0x14 + 4 * index, uint32_t(at - (0x14 + 4 * index)));
        std::memcpy(out.data() + at, src.data() + s.at, s.size);
        size_t placed = at;
        at += s.size;
        index++;
        return placed;
    };
    for (const Sample& s : tb->samples) {
        size_t h = place_header(bank, s);
        // data in the header part moves with it, by the offset table's growth
        for (size_t t : s.offset_tags) {
            uint32_t v = Be32(&bank[s.at + t]);
            PutBe32(&out[h + t], v >= tb->header ? v + delta : v + 4 * uint32_t(added.size()));
        }
    }
    // each new sample's data spans: from each of its offsets to the next offset in its bank
    std::map<std::pair<const SoundDonor*, uint32_t>, uint32_t> new_index;
    for (auto& [donor, s] : added) {
        const Bank& db = donor_banks[donor];
        const std::vector<uint8_t>& src = donor->bank;
        std::vector<uint32_t> all;
        for (const Sample& x : db.samples)
            for (size_t t : x.offset_tags) all.push_back(Be32(&src[x.at + t]));
        std::sort(all.begin(), all.end());
        const Sample& sample = db.samples[s];
        new_index[{donor, s}] = index;
        size_t h = place_header(src, sample);
        for (size_t t : sample.offset_tags) {
            uint32_t from = Be32(&src[sample.at + t]);
            auto next = std::upper_bound(all.begin(), all.end(), from);
            uint32_t to = next == all.end() ? uint32_t(src.size()) : *next;
            // where it was in its sixteen, as the codec's blocks may want
            while (out.size() % 16 != from % 16) out.push_back(0);
            PutBe32(&out[h + t], uint32_t(out.size()));
            out.insert(out.end(), src.begin() + from, src.begin() + to);
        }
    }
    Set32(out, 12, uint32_t(out.size() - header));

    // the event file: the new records among the level's, by hash
    std::vector<std::vector<uint8_t>> records;
    for (uint32_t i = 0; i < te->count; i++) {
        size_t r = te->records + size_t(te->size) * i;
        records.emplace_back(events.begin() + r, events.begin() + r + te->size);
    }
    for (Taken& t : taken) {
        Set16(t.record, te->sample_at, uint16_t(new_index[{t.donor, t.sample}]));
        records.push_back(std::move(t.record));
    }
    std::stable_sort(records.begin(), records.end(), [&](const auto& a, const auto& b) {
        return U32(a, te->hash_at) < U32(b, te->hash_at);
    });
    SoundMerged m;
    m.events.assign(events.begin(), events.begin() + te->records);
    for (const auto& r : records) m.events.insert(m.events.end(), r.begin(), r.end());
    m.events.insert(m.events.end(), events.begin() + te->records + size_t(te->size) * te->count, events.end());
    Set32(m.events, 0x14, uint32_t(records.size()));
    m.bank = std::move(out);
    m.records = int(taken.size());
    m.samples = int(added.size());
    return m;
}

}  // namespace game::levels
