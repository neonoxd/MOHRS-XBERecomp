#include "common/options_package.h"

#include <algorithm>

#include "common/feng.h"

namespace game::menu {

namespace {

constexpr char kEntry[] = "PC Options button";
constexpr float kRowPitch = 28.0f;    // the screen's rows; HUD.fng's two are 32 and 37 apart, with room for two only
constexpr float kShellPitch = 28.0f;  // Option_overlay.fng's entries, Audio to Difficulty
constexpr float kPausePitch = 40.0f;  // Preferences.fng's

// HUD.fng's rows: Crosshairs is the pattern, the HUD type row goes
const char* const kHudRows[] = {"Crosshairs text", "Crosshairs Off", "Crosshairs On", "Crosshairs button",
                                "HUD Automatic",   "HUD On",         "HUD text",      "HUD button"};

std::string Folder(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

}  // namespace

std::string RowObject(size_t row, const char* part) { return "PC Row " + std::to_string(row) + " " + part; }

std::vector<uint8_t> MakeScreen(const uint8_t* hud, size_t size, const std::vector<std::string>& labels) {
    std::optional<feng::Package> parsed = feng::Package::Parse(hud, size);
    if (!parsed || labels.empty()) return {};
    feng::Package& p = *parsed;
    const feng::Object* name = p.Find("Crosshairs text");
    const feng::Object* value = p.Find("Crosshairs On");
    size_t at = p.Index("Crosshairs button");
    if (!name || !value || at == p.objects.size()) return {};
    // the button and its arrows
    std::vector<feng::Object> button{p.objects[at]};
    uint32_t pattern = button[0].Guid();
    for (size_t i = at + 1; i < p.objects.size() && p.objects[i].Parent() == pattern; i++) button.push_back(p.objects[i]);
    std::vector<feng::Object> rows;
    uint32_t guid = p.NextGuid(), first = 0;
    for (size_t r = 0; r < labels.size(); r++) {
        float down = kRowPitch * float(r);
        feng::Object n = *name;
        n.SetName(RowObject(r, "text"));
        n.SetGuid(guid++);
        n.SetText(labels[r], "PC_OPTION_" + std::to_string(r));
        n.Move(0.0f, down);
        rows.push_back(n);
        // hidden by its Init script until the handler shows it, as the On and Off texts
        feng::Object v = *value;
        v.SetName(RowObject(r, "value"));
        v.SetGuid(guid++);
        v.SetText("", "PC_OPTION_VALUE_" + std::to_string(r));
        v.Move(0.0f, down);
        rows.push_back(v);
        uint32_t group = guid++;
        first = first ? first : group;
        for (size_t i = 0; i < button.size(); i++) {
            feng::Object b = button[i];
            if (i) {
                b.SetGuid(guid++);
                b.SetParent(group);
            } else {
                b.SetName(RowObject(r, "button"));
                b.SetGuid(group);
                b.Move(0.0f, down);
            }
            rows.push_back(b);
        }
        // the highlight messages reach every row's button
        p.AddTarget(pattern, group);
    }
    // the first row has the focus on entry
    p.ReplaceTargets(pattern, first);
    for (const char* gone : kHudRows) p.Remove(gone);
    size_t insert = std::min(p.Index("HUD Choreography"), p.objects.size());
    p.objects.insert(p.objects.begin() + insert, rows.begin(), rows.end());
    if (feng::Object* title = p.Find("FEFont28")) title->SetText("PC OPTIONS", "PC_OPTIONS_CAPS");
    p.SetName(kScreen, Folder(p.Path()) + kScreen);
    return p.Write();
}

std::vector<uint8_t> AddEntry(const uint8_t* list, size_t size) {
    std::optional<feng::Package> parsed = feng::Package::Parse(list, size);
    if (!parsed) return {};
    feng::Package& p = *parsed;
    bool shell = p.Find("Audio text") != nullptr;
    size_t at = p.Index(shell ? "Audio text" : "Audio button");
    if (at == p.objects.size() || p.Find(kEntry)) return {};
    feng::Object e = p.objects[at];
    uint32_t like = e.Guid();
    e.SetName(kEntry);
    e.SetGuid(p.NextGuid());
    e.SetText("PC Options", "PC_OPTIONS");
    e.SetPathFile(kScreen);
    float y = e.Y();
    if (shell) {
        // a row under Audio, the entries below it move down
        for (feng::Object& o : p.objects)
            if (o.IsButton() && !o.Parent() && o.Y() > y) o.Move(0.0f, kShellPitch);
        e.Move(0.0f, kShellPitch);
    } else {
        // the pause screen's list ends with HUD Options: under it
        float low = y;
        for (const feng::Object& o : p.objects)
            if (o.IsButton() && !o.Parent()) low = std::max(low, o.Y());
        e.Move(0.0f, low + kPausePitch - y);
    }
    p.AddTarget(like, e.Guid());
    p.objects.insert(p.objects.begin() + at + 1, e);
    return p.Write();
}

std::vector<uint8_t> AddPauseTabs(const uint8_t* data, size_t size) {
    auto p = feng::Package::Parse(data, size);
    if (!p) return {};
    const char* single[] = {"ObjectivesOverlay.fng", "Notes.fng", "Preferences.fng", "LoadRestart.fng"};
    const char* multi[] = {"MPLeaderBoard.fng", nullptr, "MPPreferences.fng", "MPLoadRestart.fng"};
    const char* const* pages = nullptr;
    int current = -1;
    for (int i = 0; i < 4; i++) {
        if (p->Name() == single[i]) { pages = single; current = i; break; }
        if (multi[i] && p->Name() == multi[i]) { pages = multi; current = i; break; }
    }
    if (!pages) return {};
    uint32_t source = feng::Hash(current == 0 ? "Pad_Right" : "Pad_Left");
    for (int tab = 0; tab < 4; tab++) {
        if (!pages[tab] || tab == current) continue;
        if (!p->AddPackageTransition(feng::Hash(kPauseTabEvents[tab]), source, pages[tab])) return {};
    }
    return p->Write();
}

}  // namespace game::menu
