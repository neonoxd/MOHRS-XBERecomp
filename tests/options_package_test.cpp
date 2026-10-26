// tools\dev.bat clang++ -std=c++20 -Isrc tests/options_package_test.cpp src/common/feng.cpp
//     src/common/options_package.cpp -o re/options_package_test.exe
// re\options_package_test.exe <folder of .fng files> [<more folders>] [--out <folder>]
// The folders hold the .fng files of data\shell\shell.viv and data\pausscrn\pause.viv (or mppause.viv), extracted
// from your copy of the game; --out writes the made packages there.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "common/feng.h"
#include "common/options_package.h"

namespace fs = std::filesystem;

static void Check(bool ok, const std::string& message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message.c_str());
        std::exit(1);
    }
}

static std::vector<uint8_t> Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static size_t Buttons(const feng::Package& p) {
    size_t n = 0;
    for (const feng::Object& o : p.objects) n += o.IsButton();
    return n;
}

static fs::path g_out;

static void Save(const fs::path& in, const char* name, const std::vector<uint8_t>& data) {
    if (g_out.empty()) return;
    fs::path dir = g_out / in.parent_path().filename();
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
}

static void Screen(const fs::path& hud) {
    std::vector<uint8_t> d = Read(hud);
    std::vector<std::string> labels;
    for (size_t i = 0; i < game::menu::kRows; i++) labels.push_back("Row " + std::to_string(i));
    std::vector<uint8_t> made = game::menu::MakeScreen(d.data(), d.size(), labels);
    Check(!made.empty(), "no screen from " + hud.string());
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p.has_value(), "made screen does not parse");
    Check(p->Name() == game::menu::kScreen, "screen name");
    Check(p->Path().ends_with(std::string("\\") + game::menu::kScreen), "screen path " + p->Path());
    Check(Buttons(*p) == labels.size(), "a button a row");
    Check(!p->Find("Crosshairs button") && !p->Find("HUD button"), "HUD rows left");
    float last = -1e9f;
    for (size_t r = 0; r < labels.size(); r++) {
        const feng::Object* b = p->Find(game::menu::RowObject(r, "button"));
        const feng::Object* v = p->Find(game::menu::RowObject(r, "value"));
        const feng::Object* t = p->Find(game::menu::RowObject(r, "text"));
        Check(b && v && t, "row objects");
        Check(b->Y() > last && b->Y() < 200.0f, "rows go down the screen");
        last = b->Y();
        size_t at = p->Index(game::menu::RowObject(r, "button"));
        Check(at + 2 < p->objects.size() && p->objects[at + 1].Parent() == b->Guid() &&
                  p->objects[at + 2].Parent() == b->Guid(),
              "arrows follow their button");
    }
    // written again unchanged
    Check(p->Write() == made, "made screen writes back the same");
    Save(hud, game::menu::kScreen, made);
    std::printf("%s: %zu bytes, %zu objects\n", hud.string().c_str(), made.size(), p->objects.size());
}

static void Entry(const fs::path& list) {
    std::vector<uint8_t> d = Read(list);
    std::optional<feng::Package> before = feng::Package::Parse(d.data(), d.size());
    std::vector<uint8_t> made = game::menu::AddEntry(d.data(), d.size());
    Check(before && !made.empty(), "no entry in " + list.string());
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p.has_value(), "list with the entry does not parse");
    Check(Buttons(*p) == Buttons(*before) + 1, "one more button");
    const feng::Object* e = p->Find("PC Options button");
    Check(e && e->IsButton(), "entry");
    Check(game::menu::AddEntry(made.data(), made.size()).empty(), "entry added twice");
    for (const feng::Object& o : p->objects)
        Check(&o == e || !o.IsButton() || o.Y() != e->Y(), "entry on another entry's row");
    Save(list, list.filename().string().c_str(), made);
    std::printf("%s: entry at y %.0f\n", list.string().c_str(), e->Y());
}

int main(int argc, char** argv) {
    std::vector<fs::path> dirs;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--out" && i + 1 < argc) {
            g_out = argv[++i];
        } else {
            dirs.push_back(argv[i]);
        }
    }
    Check(!dirs.empty(), "usage: options_package_test <folder of .fng files>... [--out <folder>]");
    size_t files = 0;
    for (const fs::path& dir : dirs) {
        for (const fs::directory_entry& f : fs::directory_iterator(dir)) {
            if (f.path().extension() != ".fng") continue;
            std::vector<uint8_t> d = Read(f.path());
            std::optional<feng::Package> p = feng::Package::Parse(d.data(), d.size());
            Check(p.has_value(), "does not parse: " + f.path().string());
            Check(p->Write() == d, "does not write back the same: " + f.path().string());
            files++;
            std::string name = f.path().filename().string();
            if (name == "HUD.fng") Screen(f.path());
            if (name == "Option_overlay.fng" || name == "Preferences.fng" || name == "MPPreferences.fng") Entry(f.path());
        }
    }
    std::printf("%zu packages read and written back\n", files);
    std::puts("options package checks passed");
}
