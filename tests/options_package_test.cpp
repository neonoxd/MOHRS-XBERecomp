// tools\dev.bat clang++ -std=c++20 -Isrc tests/options_package_test.cpp src/common/feng.cpp
//     src/common/options_package.cpp -o re/options_package_test.exe
// re\options_package_test.exe <folder of .fng files> [<more folders>] [--out <folder>]
// The folders hold the .fng files of data\shell\shell.viv and data\pausscrn\pause.viv (or mppause.viv), extracted
// from your copy of the game; --out writes the made packages there.
#include <cstdio>
#include <cstring>
#include <map>
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

static uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v; Check(at + 4 <= d.size(), "truncated field");
    std::memcpy(&v, d.data() + at, 4); return v;
}

// Inspect serialized package responses independently of the package writer.
static std::map<uint32_t, std::vector<feng::Field>> Responses(const std::vector<uint8_t>& d) {
    std::map<uint32_t, std::vector<feng::Field>> result;
    for (size_t at = 8; at + 8 <= d.size();) {
        size_t end = at + 8 + U32(d, at + 4);
        Check(end <= d.size(), "response chunk bounds");
        if (U32(d, at) == 0x52676B50) { // PkgR
            uint32_t message = 0;
            for (size_t f = at + 8; f + 4 <= end;) {
                uint16_t tag, size;
                std::memcpy(&tag, d.data() + f, 2); std::memcpy(&size, d.data() + f + 2, 2);
                Check(f + 4 + size <= end, "response field bounds");
                if (tag == feng::Tag2("Mi")) message = U32(d, f + 4);
                result[message].push_back({tag, {d.begin() + f + 4, d.begin() + f + 4 + size}});
                f += 4 + size;
            }
        }
        at = end;
    }
    return result;
}

static void PauseTabs(const fs::path& file) {
    const char* single[] = {"ObjectivesOverlay.fng", "Notes.fng", "Preferences.fng", "LoadRestart.fng"};
    const char* multi[] = {"MPLeaderBoard.fng", nullptr, "MPPreferences.fng", "MPLoadRestart.fng"};
    const char* const* pages = nullptr;
    int current = -1;
    for (int i = 0; i < 4; i++) {
        if (file.filename() == single[i]) { pages = single; current = i; }
        if (multi[i] && file.filename() == multi[i]) { pages = multi; current = i; }
    }
    if (!pages) return;
    auto data = Read(file);
    // Preferences must support both the PC Options insertion and direct tab responses.
    if (current == 2) data = game::menu::AddEntry(data.data(), data.size());
    auto original = Responses(data);
    auto made = game::menu::AddPauseTabs(data.data(), data.size());
    Check(!made.empty(), "direct tab responses: " + file.string());
    auto parsed = feng::Package::Parse(made.data(), made.size());
    Check(parsed && parsed->Write() == made, "tab package round trip");
    if (current == 2) Check(parsed->Find("PC Options button"), "PC Options entry survived tab additions");
    auto changed = Responses(made);
    uint32_t source = feng::Hash(current == 0 ? "Pad_Right" : "Pad_Left");
    Check(original.contains(source), "native tab cleanup response exists");
    for (const auto& [message, fields] : original) {
        Check(changed.contains(message) && changed[message].size() == fields.size(), "native response retained");
        for (size_t i = 0; i < fields.size(); i++)
            Check(fields[i].tag == changed[message][i].tag && fields[i].data == changed[message][i].data,
                  "native keyboard/controller response is unchanged");
    }
    size_t added = 0;
    for (int tab = 0; tab < 4; tab++) {
        uint32_t message = feng::Hash(game::menu::kPauseTabEvents[tab]);
        if (!pages[tab] || tab == current) { Check(!changed.contains(message), "no missing/current page transition"); continue; }
        added++;
        Check(changed.contains(message), "clicked tab has a direct response");
        auto expected = original[source];
        auto actual = changed[message];
        Check(actual.size() == expected.size(), "same native cleanup command count");
        bool destination = false;
        for (size_t i = 0; i < actual.size(); i++) {
            Check(actual[i].tag == expected[i].tag, "native command order");
            if (actual[i].tag == feng::Tag2("Mi")) continue;
            if (actual[i].tag == feng::Tag2("Rs")) {
                std::string path(reinterpret_cast<const char*>(actual[i].data.data()));
                Check(path.ends_with(std::string("\\") + pages[tab]), "direct destination filename");
                destination = true;
            } else Check(actual[i].data == expected[i].data, "all native cleanup arguments retained");
        }
        Check(destination, "direct page replacement");
    }
    Check(changed.size() == original.size() + added, "exactly one response per other available tab");
    Check(game::menu::AddPauseTabs(made.data(), made.size()).empty(), "tab responses cannot be inserted twice");
    std::printf("%s: direct pause tabs validated\n", file.string().c_str());
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
            PauseTabs(f.path());
            if (name == "HUD.fng") Screen(f.path());
            if (name == "Option_overlay.fng" || name == "Preferences.fng" || name == "MPPreferences.fng") Entry(f.path());
        }
    }
    std::printf("%zu packages read and written back\n", files);
    std::puts("options package checks passed");
}
