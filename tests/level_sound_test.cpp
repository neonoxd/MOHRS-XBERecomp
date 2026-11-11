// tools\dev.bat clang++ -std=c++20 -Isrc tests/level_sound_test.cpp src/common/level_sound.cpp
//     -o re/level_sound_test.exe
// re\level_sound_test.exe <folder>
// The folder holds each campaign level's sound\<m>_<l>.ubr as <level>.ubr and sound\<m><l>scr.abk as
// <level>.scr.abk, and weapbul_medium.dat (from a level.viv), all from your copy of the game:
//   for l in 1_1 1_2 1_3 1_4 1_5 1_7 1_8 1_9; do
//     python tools/xiso.py get "assets/<image>.iso" DATA/1/$l/SOUND/$l.ubr re/levels/$l.ubr
//     python tools/xiso.py get "assets/<image>.iso" DATA/1/$l/SOUND/${l/_/}scr.abk re/levels/$l.scr.abk; done
// Each level gets the other levels' player weapons' sound events: they are found in the merged event file, the
// level's own records and samples stay, and merging again adds nothing.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "common/level_sound.h"

namespace fs = std::filesystem;
using namespace game::levels;

static void Check(bool ok, const std::string& message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}

static std::vector<uint8_t> Load(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

// the event hashes of a .ubr's records
static std::vector<uint32_t> Hashes(const std::vector<uint8_t>& d) {
    uint32_t columns = U32(d, 0x10), count = U32(d, 0x14), size = U32(d, 0x18);
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < count; i++) out.push_back(U32(d, 0x20 + 12 * columns + size * i + 36));
    return out;
}

int main(int argc, char** argv) {
    Check(argc >= 2, "usage: level_sound_test <folder>");
    fs::path dir = argv[1];
    // the player weapons by donor, as src/common/level_archive.cpp takes them, and their sound names (0x202740)
    struct W {
        const char* level;
        uint32_t hash;
        const char* sound;
    };
    const W weapons[] = {{"1_2", 0xC186CA3C, "Colt_45"},     {"1_2", 0x8EFB8E12, "M1_Garand"},
                         {"1_2", 0x392805E6, "Thompson"},    {"1_3", 0x1CE5DC00, "Model_11"},
                         {"1_3", 0x6E584C98, "Springfield"}, {"1_5", 0x3B4501E8, "Sten_Silenced"},
                         {"1_5", 0x52DF0B5F, "Welrod"},      {"1_7", 0x4F5286C5, "Bazooka"},
                         {"1_7", 0xF45E50D5, "Shotgun"},     {"1_9", 0xB823E880, "Model_99"},
                         {"1_1", 0x9D0E7C6C, "BAR"}};
    std::vector<uint8_t> weapbul = Load(dir / "weapbul_medium.dat");
    Check(!weapbul.empty(), "no weapbul_medium.dat");
    const char* table[] = {"Colt_45", "M1_Garand", "Thompson", "Model_11", "Springfield", "Sten_Silenced",
                           "Welrod", "Bazooka", "Shotgun", "Model_99", "BAR"};
    const int index[] = {1, 9, 14, 19, 7, 17, 3, 36, 12, 21, 23};
    for (size_t i = 0; i < std::size(weapons); i++) {
        Check(WeaponSoundIndex(weapbul, weapons[i].hash) == index[i], std::string("sound index of ") + table[i]);
        Check(std::string(table[i]) == weapons[i].sound, "table");
    }
    Check(NameHash("WpnFire_Springfield") == 0x7F0F6CFD, "name hash");

    std::map<std::string, SoundDonor> donors;
    for (const char* l : {"1_1", "1_2", "1_3", "1_4", "1_5", "1_7", "1_8", "1_9"}) {
        donors[l] = {Load(dir / (std::string(l) + ".ubr")), Load(dir / (std::string(l) + ".scr.abk"))};
        Check(!donors[l].events.empty() && !donors[l].bank.empty(), std::string(l) + ": sound files missing");
    }
    for (auto& [level, own] : donors) {
        std::vector<SoundWant> wants;
        for (const W& w : weapons)
            if (level != w.level) wants.push_back({&donors[w.level], WeaponSoundEvents(w.sound)});
        std::optional<SoundMerged> m = MergeSounds(own.events, own.bank, wants);
        Check(m.has_value(), level + ": nothing merged");
        std::vector<uint32_t> before = Hashes(own.events), after = Hashes(m->events);
        Check(after.size() == before.size() + size_t(m->records), level + ": record count");
        for (size_t i = 1; i < after.size(); i++) Check(after[i - 1] <= after[i], level + ": records not sorted");
        for (const W& w : weapons) {
            uint32_t fire = NameHash(std::string("WpnFire_") + w.sound);
            bool found = false;
            for (uint32_t h : after) found = found || h == fire;
            Check(found, level + ": no fire event for " + w.sound);
        }
        // the level's own bank stays a prefix of the sample count and its data, shifted
        Check(U32(m->bank, 0) == U32(own.bank, 0), level + ": bank magic");
        uint16_t count = 0;
        std::memcpy(&count, m->bank.data() + 6, 2);
        uint16_t was = 0;
        std::memcpy(&was, own.bank.data() + 6, 2);
        Check(count == was + m->samples, level + ": sample count");
        Check(size_t(U32(m->bank, 8)) + U32(m->bank, 12) == m->bank.size(), level + ": bank sizes");
        Check(std::equal(own.bank.begin() + U32(own.bank, 8), own.bank.end(), m->bank.begin() + U32(m->bank, 8)),
              level + ": the level's own sample data moved or changed");
        // merged again: nothing more
        Check(!MergeSounds(m->events, m->bank, wants), level + ": merged again added more");
        std::printf("%s: +%d records, +%d samples, bank %.2f MB -> %.2f MB\n", level.c_str(), m->records, m->samples,
                    own.bank.size() / 1e6, m->bank.size() / 1e6);
    }
    std::printf("level sound checks passed\n");
}
