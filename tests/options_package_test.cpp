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
static uint32_t U32(const std::vector<uint8_t>& d, size_t at);

static void Save(const fs::path& in, const char* name, const std::vector<uint8_t>& data) {
    if (g_out.empty()) return;
    fs::path dir = g_out / in.parent_path().filename();
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
}

static void Screen(const fs::path& hud, const game::menu::ScreenSpec& spec, size_t rows, size_t keys = 0) {
    std::vector<uint8_t> d = Read(hud);
    std::vector<std::string> labels;
    for (size_t i = 0; i < rows; i++) labels.push_back("Row " + std::to_string(i));
    std::vector<uint8_t> made = game::menu::MakeScreen(d.data(), d.size(), labels, spec, keys);
    Check(!made.empty(), "no screen from " + hud.string());
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p.has_value(), "made screen does not parse");
    Check(p->Name() == spec.file, "screen name");
    Check(p->Path().ends_with(std::string("\\") + spec.file), "screen path " + p->Path());
    Check(Buttons(*p) == labels.size() + keys, "a button a row");
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
    // the key rows: at the last places, on the value rows' lines, their buttons answering a press: their flash, then
    // the press sent on to the game (Ri 2, Ru Button_Pressed), two responses
    static const uint32_t kPressed = feng::Hash("Button_Pressed");
    auto answers = [](const feng::Object* o) {
        for (const feng::Chunk& c : o->chunks)
            for (size_t i = 0; i < c.fields.size(); i++) {
                const feng::Field& f = c.fields[i];
                if (f.tag != feng::Tag2("Mi") || f.data.size() != 4 || U32(f.data, 0) != kPressed) continue;
                auto at = [&](size_t k, const char (&tag)[3]) {
                    return i + k < c.fields.size() && c.fields[i + k].tag == feng::Tag2(tag) ?
                               U32(c.fields[i + k].data, 0) : 0xFFFFFFFFu;
                };
                return at(1, "MC") == 2 && at(2, "Ri") == 0 && at(5, "Ri") == 2 && at(6, "Ru") == kPressed &&
                       at(7, "Rt") == 0 && (i + 8 == c.fields.size() || c.fields[i + 8].tag == feng::Tag2("Mi"));
            }
        return false;
    };
    for (size_t r = 0; r < labels.size(); r++) {
        const feng::Object* row = p->Find(game::menu::RowObject(r, "button"));
        const feng::Object* b = p->Find(game::menu::KeyObject(r, "button"));
        Check(!answers(row), "a value row's button leaves a press to the package");
        Check((b != nullptr) == (r >= labels.size() - keys), "a key row at each of the last places");
        if (!b) continue;
        const feng::Object* v = p->Find(game::menu::KeyObject(r, "value"));
        const feng::Object* t = p->Find(game::menu::KeyObject(r, "text"));
        Check(v && t && b->IsButton() && b->Y() == row->Y() && b->Guid() != row->Guid(), "key row objects");
        Check(v->Y() == p->Find(game::menu::RowObject(r, "value"))->Y(), "key value on its line");
        size_t at = p->Index(game::menu::KeyObject(r, "button"));
        Check(at + 2 < p->objects.size() && p->objects[at + 1].Parent() == b->Guid() &&
                  p->objects[at + 2].Parent() == b->Guid(), "key arrows follow their button");
        Check(answers(b), "a key row's button answers a press");
    }
    // written again unchanged
    Check(p->Write() == made, "made screen writes back the same");
    Save(hud, spec.file, made);
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
    if (list.filename() == "Preferences.fng") {
        // the pause screen's Cheats under PC Options, below every other entry
        made = game::menu::AddEntry(made.data(), made.size(), game::menu::kCheats);
        Check(!made.empty(), "no Cheats entry in " + list.string());
        p = feng::Package::Parse(made.data(), made.size());
        Check(p.has_value() && Buttons(*p) == Buttons(*before) + 2, "two more buttons");
        e = p->Find("PC Options button");
        const feng::Object* c = p->Find("Cheats button");
        Check(e && c && c->IsButton(), "Cheats entry");
        for (const feng::Object& o : p->objects)
            Check(&o == c || !o.IsButton() || o.Y() < c->Y(), "Cheats entry is the last row");
        Check(game::menu::AddEntry(made.data(), made.size(), game::menu::kCheats).empty(), "Cheats added twice");
        std::printf("%s: Cheats entry at y %.0f\n", list.string().c_str(), c->Y());
    }
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
    if (current == 2 && pages == single) data = game::menu::AddEntry(data.data(), data.size(), game::menu::kCheats);
    auto original = Responses(data);
    auto made = game::menu::AddPauseTabs(data.data(), data.size());
    Check(!made.empty(), "direct tab responses: " + file.string());
    auto parsed = feng::Package::Parse(made.data(), made.size());
    Check(parsed && parsed->Write() == made, "tab package round trip");
    if (current == 2) Check(parsed->Find("PC Options button"), "PC Options entry survived tab additions");
    if (current == 2 && pages == single) Check(parsed->Find("Cheats button"), "Cheats entry survived tab additions");
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

static void MissionTime(const fs::path& file) {
    std::vector<uint8_t> d = Read(file);
    std::optional<feng::Package> before = feng::Package::Parse(d.data(), d.size());
    std::vector<uint8_t> made = game::menu::AddMissionTime(d.data(), d.size());
    Check(!made.empty(), "mission time row not added: " + file.string());
    Save(file, game::menu::kSummary, made);
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p && p->Write() == made, "mission time package round trip");
    Check(p->objects.size() == before->objects.size() + 2, "mission time adds a label and a value");
    const feng::Object* hits = p->Find("Hits value");
    const feng::Object* label = p->Find(game::menu::kTimeLabel);
    const feng::Object* value = p->Find(game::menu::kTimeValue);
    const feng::Object* head = p->Find("Head value");
    const feng::Object* medal = p->Find("Medal Awarded text");
    Check(hits && label && value && head && medal, "mission time row objects");
    float pitch = before->Find("Head value")->Y() - before->Find("Hits value")->Y();
    Check(value->Y() == hits->Y() + pitch && label->Y() == value->Y(), "mission time row under Hits");
    Check(head->Y() == value->Y() + pitch, "the body rows move down a row");
    Check(medal->Y() == before->Find("Medal Awarded text")->Y() + pitch, "the medal text moves down a row");
    Check(p->Find("PauseScreenFont")->Y() == before->Find("PauseScreenFont")->Y(), "the choreography text stays");
    Check(label->Guid() != value->Guid() && label->Guid() >= before->NextGuid(), "new GUIDs");
    Check(game::menu::AddMissionTime(made.data(), made.size()).empty(), "mission time cannot be added twice");
    std::printf("%s: mission time at y %.0f, medal text at y %.0f\n", file.string().c_str(), value->Y(), medal->Y());
}

static void HudRows(const fs::path& file) {
    std::vector<uint8_t> d = Read(file);
    std::optional<feng::Package> before = feng::Package::Parse(d.data(), d.size());
    std::vector<uint8_t> made = game::menu::AddHudRows(d.data(), d.size());
    Check(!made.empty(), "HUD rows not added: " + file.string());
    Save(file, game::menu::kHudScreen, made);
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p && p->Write() == made, "HUD package round trip");
    float pitch = p->Find("HUD text")->Y() - p->Find("Crosshairs text")->Y();
    size_t last = p->Index("HUD button");
    int down = 2;
    for (std::string row : {game::menu::kTimerRow, game::menu::kFpsRow}) {
        const feng::Object* text = p->Find(row + " text");
        const feng::Object* on = p->Find(row + " On");
        const feng::Object* off = p->Find(row + " Off");
        const feng::Object* button = p->Find(row + " button");
        Check(text && on && off && button, row + " row objects");
        Check(text->Y() == p->Find("Crosshairs text")->Y() + float(down) * pitch, row + " row under HUD Type");
        Check(button->Y() == p->Find("Crosshairs button")->Y() + float(down) * pitch && button->IsButton(),
              "its button");
        size_t at = p->Index(row + " button");
        Check(at > last, "buttons in screen order");
        last = at;
        Check(at + 2 < p->objects.size() && p->objects[at + 1].Parent() == button->Guid() &&
                  p->objects[at + 2].Parent() == button->Guid(), "its arrows");
        std::printf("%s: %s row at y %.0f\n", file.string().c_str(), row.c_str(), text->Y());
        down++;
    }
    Check(p->objects.size() == before->objects.size() + 12, "text, On, Off, button and two arrows added a row");
    Check(game::menu::AddHudRows(made.data(), made.size()).empty(), "HUD rows cannot be added twice");
}

static void AimRow(const fs::path& file) {
    std::vector<uint8_t> d = Read(file);
    std::optional<feng::Package> before = feng::Package::Parse(d.data(), d.size());
    std::vector<uint8_t> made = game::menu::AddAimRow(d.data(), d.size());
    Check(!made.empty(), "modern aiming row not added: " + file.string());
    Save(file, game::menu::kControllerScreen, made);
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p && p->Write() == made, "controller package round trip");
    std::string row = game::menu::kAimRow;
    const feng::Object* text = p->Find(row + " text");
    const feng::Object* on = p->Find(row + " On");
    const feng::Object* off = p->Find(row + " Off");
    const feng::Object* button = p->Find(row + " button");
    Check(text && on && off && button, "modern aiming row objects");
    float pitch = (p->Find("Invert Y Axis text")->Y() - p->Find("Vibrate text")->Y()) / 3.0f;
    Check(pitch > 0.0f, "rows go down");
    Check(text->Y() == p->Find("Invert Y Axis text")->Y() + pitch, "modern aiming row under Invert Y Axis");
    Check(on->Y() == p->Find("Invert Y Axis On")->Y() + pitch && off->Y() == p->Find("Invert Y Axis Off")->Y() + pitch,
          "its On and Off");
    Check(button->Y() == p->Find("Invert Y Axis button")->Y() + pitch && button->IsButton(), "its button");
    size_t at = p->Index(row + " button");
    Check(at + 2 < p->objects.size() && p->objects[at + 1].Parent() == button->Guid() &&
              p->objects[at + 2].Parent() == button->Guid(), "its arrows");
    Check(p->objects.size() == before->objects.size() + 6, "text, On, Off, button and two arrows added");
    Check(game::menu::AddAimRow(made.data(), made.size()).empty(), "modern aiming row cannot be added twice");
    std::printf("%s: modern aiming row at y %.0f (pitch %.0f)\n", file.string().c_str(), text->Y(), pitch);
}

static void ExitEntry(const fs::path& file) {
    std::vector<uint8_t> d = Read(file);
    std::optional<feng::Package> before = feng::Package::Parse(d.data(), d.size());
    std::vector<uint8_t> made = game::menu::AddExitEntry(d.data(), d.size());
    Check(!made.empty(), "exit to desktop not added: " + file.string());
    Save(file, before->Name().c_str(), made);
    std::optional<feng::Package> p = feng::Package::Parse(made.data(), made.size());
    Check(p && p->Write() == made, "exit entry package round trip");
    Check(p->objects.size() == before->objects.size() + 1 && Buttons(*p) == Buttons(*before) + 1, "one button added");
    const feng::Object* e = p->Find(game::menu::kExitEntry);
    Check(e && e->IsButton() && !e->Parent(), "exit entry is a top level button");
    bool shell = before->Name() == game::menu::kMainMenu;
    const feng::Object* last = p->Find(shell ? "Multiplayer text" : "Exit button");
    const feng::Object* up = p->Find(shell ? "Coop Multiplayer Text" : "Save Points button");
    Check(e->Y() == last->Y() + (last->Y() - up->Y()) && e->X() == last->X(), "a row under the last entry");
    // the last entry; the main menu's Online Multiplayer, hidden by the screen's setup, has its place
    for (const feng::Object& o : p->objects)
        Check(&o == e || !o.IsButton() || o.Y() < e->Y() || (shell && o.Name() == "Online Multiplayer Text"),
              "the last entry");
    Check(e->Guid() >= before->NextGuid(), "new GUID");
    // the positions its scripts set (track To 7, the first key's x, y, z after -1) are its own
    size_t keys = 0;
    for (const feng::Chunk& c : e->chunks) {
        if (c.tag != 0x70726353) continue;  // Scrp
        uint32_t track = 0;
        for (const feng::Field& f : c.fields) {
            if (f.tag == feng::Tag2("To")) track = U32(f.data, 0);
            if (f.tag != feng::Tag2("Kd") || track != 7) continue;
            uint32_t yk = U32(f.data, 8);
            float y;
            std::memcpy(&y, &yk, 4);
            Check(y == e->Y(), "a script puts the entry at y " + std::to_string(y));
            keys++;
        }
    }
    Check(!shell || keys > 0, "the main menu entry's scripts set its position");
    // a press is one response: the message on to the game (type 2)
    const feng::Chunk* answers = nullptr;
    for (const feng::Chunk& c : e->chunks)
        if (c.tag == 0x5267734D) answers = &c;  // MsgR
    Check(answers != nullptr, "its answers");
    uint32_t pressed = feng::Hash("Button_Pressed"), highlight = feng::Hash("Button_Highlight");
    bool found = false, lit = false;
    for (size_t i = 0; i < answers->fields.size(); i++) {
        const feng::Field& f = answers->fields[i];
        if (f.tag != feng::Tag2("Mi")) continue;
        lit |= U32(f.data, 0) == highlight;
        if (U32(f.data, 0) != pressed) continue;
        Check(i + 4 < answers->fields.size() && U32(answers->fields[i + 1].data, 0) == 1 &&
                  U32(answers->fields[i + 2].data, 0) == 2 &&
                  U32(answers->fields[i + 3].data, 0) == feng::Hash(game::menu::kExitMessage) &&
                  (i + 5 == answers->fields.size() || answers->fields[i + 5].tag == feng::Tag2("Mi")),
              "press sends the exit message");
        found = true;
    }
    Check(found && lit, "press and highlight answers");
    Check(game::menu::AddExitEntry(made.data(), made.size()).empty(), "exit entry cannot be added twice");
    Check(shell || !game::menu::AddPauseTabs(made.data(), made.size()).empty(), "the exit page keeps its mouse tabs");
    std::printf("%s: exit to desktop at y %.0f\n", file.string().c_str(), e->Y());
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
            if (name == game::menu::kSummary) MissionTime(f.path());
            if (name == game::menu::kMainMenu || name == "LoadRestart.fng") ExitEntry(f.path());
            if (name == game::menu::kControllerScreen) AimRow(f.path());
            if (name == "HUD.fng") {
                HudRows(f.path());
                Screen(f.path(), game::menu::kPcOptions, game::menu::kRows, game::menu::kKeyRows);
                Screen(f.path(), game::menu::kCheats, game::menu::kCheatRows);
            }
            if (name == "Option_overlay.fng" || name == "Preferences.fng" || name == "MPPreferences.fng") Entry(f.path());
        }
    }
    std::printf("%zu packages read and written back\n", files);
    std::puts("options package checks passed");
}
