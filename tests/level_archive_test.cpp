// tools\dev.bat clang++ -std=c++20 -Isrc tests/level_archive_test.cpp src/common/level_archive.cpp
//     -o re/level_archive_test.exe
// re\level_archive_test.exe <folder> [--out <folder>]
// The folder holds level archives named <level>.viv (data\1\<level>\level.viv from your copy of the game) and 2_1.bnk
// (data\2\2_1\anim.bnk):
//   for l in 1_1 1_2 1_3 1_4 1_5 1_7 1_8 1_9 1_11 1_16; do
//     python tools/xiso.py get "assets/<image>.iso" DATA/1/$l/level.viv re/levels/$l.viv; done
//   python tools/xiso.py get "assets/<image>.iso" DATA/2/2_1/anim.bnk re/levels/2_1.bnk
// Every campaign level is merged with the others' weapons: the level's own files stay byte for byte, the added
// weapons' files are there, and merging the result again finds nothing missing (its database reads back).
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "common/level_archive.h"

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

class Bytes : public Source {
public:
    explicit Bytes(std::vector<uint8_t> d) : data_(std::move(d)), entries_(ParseViv(data_.data(), data_.size())) {}
    const std::vector<VivEntry>& Entries() override { return entries_; }
    std::vector<uint8_t> Read(const VivEntry& e) override {
        return {data_.begin() + e.offset, data_.begin() + e.offset + e.size};
    }
    using Source::Read;
    const std::vector<uint8_t>& Data() const { return data_; }

private:
    std::vector<uint8_t> data_;
    std::vector<VivEntry> entries_;
};

int main(int argc, char** argv) {
    Check(argc >= 2, "usage: level_archive_test <folder> [--out <folder>]");
    fs::path dir = argv[1], out;
    if (argc >= 4 && std::string(argv[2]) == "--out") out = argv[3];
    const char* order[] = {"1_2", "1_3", "1_5", "1_7", "1_9", "1_1", "1_4", "1_8"};
    std::map<std::string, Bytes*> levels;
    for (const char* l : {"1_1", "1_2", "1_3", "1_4", "1_5", "1_7", "1_8", "1_9", "1_11", "1_16"}) {
        fs::path p = dir / (std::string(l) + ".viv");
        if (!fs::exists(p)) continue;
        levels[l] = new Bytes(Load(p));
        Check(!levels[l]->Entries().empty(), std::string(l) + ": no archive");
    }
    std::vector<uint8_t> bank = Load(dir / "2_1.bnk");
    Check(!bank.empty(), "no 2_1.bnk");
    int merged_levels = 0;
    for (auto& [name, level] : levels) {
        std::vector<Source*> donors;
        for (const char* d : order)
            if (d != name && levels.count(d)) donors.push_back(levels[d]);
        std::optional<Merged> m = MergeWeapons(level->Data(), donors, bank);
        if (!m) {
            std::printf("%-5s nothing added\n", name.c_str());
            continue;
        }
        merged_levels++;
        Check(m->archive.size() <= kMaxArchive, name + ": archive too big");
        Bytes merged(m->archive);
        Check(!merged.Entries().empty(), name + ": merged archive does not parse");
        // the level's own files, unchanged but the database
        for (const VivEntry& e : level->Entries()) {
            const VivEntry* f = merged.Find(e.name);
            Check(f != nullptr, name + ": lost " + e.name);
            Check(f->offset % 0x80 == 0, name + ": misaligned " + e.name);
            if (e.name != "levelfile.lfc") Check(merged.Read(*f) == level->Read(e), name + ": changed " + e.name);
        }
        Check(merged.Find(kBankFile) && merged.Read(kBankFile) == bank, name + ": no bank");
        // no world model of another level (its textures are that level's)
        for (const VivEntry& e : merged.Entries()) {
            std::string n = e.name;
            for (char& c : n) c = char(std::tolower(uint8_t(c)));
            if (n.ends_with(".msh")) Check(level->Find(e.name) != nullptr, name + ": added world model " + e.name);
        }
        Check(merged.Entries().size() > level->Entries().size() + 1, name + ": no files added");
        // merged again: every weapon the donors have is there now
        Check(!MergeWeapons(m->archive, donors, bank), name + ": the merged database does not read back");
        std::string list;
        for (const std::string& w : m->weapons) list += " " + w;
        std::printf("%-5s +%zu files, %.2f MB -> %.2f MB:%s\n", name.c_str(),
                    merged.Entries().size() - level->Entries().size(), m->added / 1e6, m->archive.size() / 1e6,
                    list.c_str());
        if (!out.empty()) {
            fs::create_directories(out);
            std::ofstream(out / (name + ".viv"), std::ios::binary)
                .write(reinterpret_cast<const char*>(m->archive.data()), std::streamsize(m->archive.size()));
        }
        // a limit on what may be added stops at a whole weapon
        std::optional<Merged> small = MergeWeapons(level->Data(), donors, bank, bank.size() + 600000);
        if (small) Check(small->weapons.size() < m->weapons.size() && small->added <= bank.size() + 700000,
                         name + ": the limit was not kept");
    }
    Check(merged_levels > 0, "no level merged");
    std::printf("level archive checks passed\n");
}
