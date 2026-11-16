#include "common/options_package.h"

#include <algorithm>
#include <cmath>

#include "common/feng.h"

namespace game::menu {

namespace {

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
std::string KeyObject(size_t row, const char* part) { return "PC Key " + std::to_string(row) + " " + part; }

std::vector<uint8_t> MakeScreen(const uint8_t* hud, size_t size, const std::vector<std::string>& labels,
                                const ScreenSpec& spec, size_t keys) {
    std::optional<feng::Package> parsed = feng::Package::Parse(hud, size);
    if (!parsed || labels.empty() || keys >= labels.size()) return {};
    feng::Package& p = *parsed;
    const feng::Object* name = p.Find("Crosshairs text");
    const feng::Object* value = p.Find("Crosshairs On");
    size_t at = p.Index("Crosshairs button");
    if (!name || !value || at == p.objects.size()) return {};
    // the button and its arrows
    std::vector<feng::Object> button{p.objects[at]};
    uint32_t pattern = button[0].Guid();
    for (size_t i = at + 1; i < p.objects.size() && p.objects[i].Parent() == pattern; i++) button.push_back(p.objects[i]);
    static const uint32_t kPressed = feng::Hash("Button_Pressed"), kHighlight = feng::Hash("Button_Highlight");
    constexpr uint32_t kToGame = 2;
    std::vector<feng::Object> rows;
    uint32_t guid = p.NextGuid(), first = 0;
    // a row's name, value and button group at a place, its objects named by `object`
    auto add = [&](size_t r, std::string (*object)(size_t, const char*), const std::string& label,
                   const std::string& tag, bool key) {
        float down = kRowPitch * float(r);
        feng::Object n = *name;
        n.SetName(object(r, "text"));
        n.SetGuid(guid++);
        n.SetText(label, tag + std::to_string(r));
        n.Move(0.0f, down);
        rows.push_back(n);
        // hidden by its Init script until the handler shows it, as the On and Off texts
        feng::Object v = *value;
        v.SetName(object(r, "value"));
        v.SetGuid(guid++);
        v.SetText("", tag + "VALUE_" + std::to_string(r));
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
                b.SetName(object(r, "button"));
                b.SetGuid(group);
                b.Move(0.0f, down);
                // a press flashes it and goes on to the handler as the package's Pad_Left does (response type 2,
                // a message to the game); the engine's own copy for the handler (target 0xFFFFFFFB) never gets there
                if (key && (!b.CopyAnswer(kPressed, kHighlight) || !b.AddResponse(kPressed, kToGame, kPressed)))
                    return false;
            }
            rows.push_back(b);
        }
        // the highlight messages reach every row's button
        p.AddTarget(pattern, group);
        return true;
    };
    for (size_t r = 0; r < labels.size(); r++) add(r, RowObject, labels[r], "PC_OPTION_", false);
    for (size_t r = labels.size() - keys; r < labels.size(); r++)
        if (!add(r, KeyObject, "", "PC_KEY_", true)) return {};
    // the first row has the focus on entry
    p.ReplaceTargets(pattern, first);
    for (const char* gone : kHudRows) p.Remove(gone);
    size_t insert = std::min(p.Index("HUD Choreography"), p.objects.size());
    p.objects.insert(p.objects.begin() + insert, rows.begin(), rows.end());
    if (feng::Object* title = p.Find("FEFont28")) title->SetText(spec.title, spec.title_label);
    p.SetName(spec.file, Folder(p.Path()) + spec.file);
    return p.Write();
}

std::vector<uint8_t> AddEntry(const uint8_t* list, size_t size, const ScreenSpec& spec) {
    std::optional<feng::Package> parsed = feng::Package::Parse(list, size);
    if (!parsed) return {};
    feng::Package& p = *parsed;
    bool shell = p.Find("Audio text") != nullptr;
    size_t at = p.Index(shell ? "Audio text" : "Audio button");
    if (at == p.objects.size() || p.Find(spec.entry)) return {};
    feng::Object e = p.objects[at];
    uint32_t like = e.Guid();
    e.SetName(spec.entry);
    e.SetGuid(p.NextGuid());
    e.SetText(spec.entry_text, spec.entry_label);
    e.SetPathFile(spec.file);
    float y = e.Y();
    if (shell) {
        // a row under Audio, the entries below it move down
        for (feng::Object& o : p.objects)
            if (o.IsButton() && !o.Parent() && o.Y() > y) o.Move(0.0f, kShellPitch);
        e.Move(0.0f, kShellPitch);
    } else {
        // the pause screen's list ends with HUD Options (then the entries added): under it
        float low = y;
        for (const feng::Object& o : p.objects)
            if (o.IsButton() && !o.Parent()) low = std::max(low, o.Y());
        e.Move(0.0f, low + kPausePitch - y);
    }
    p.AddTarget(like, e.Guid());
    p.objects.insert(p.objects.begin() + at + 1, e);
    return p.Write();
}

std::vector<uint8_t> AddMissionTime(const uint8_t* data, size_t size) {
    std::optional<feng::Package> parsed = feng::Package::Parse(data, size);
    if (!parsed) return {};
    feng::Package& p = *parsed;
    const feng::Object* label = p.Find("Hits label");
    const feng::Object* value = p.Find("Hits value");
    const feng::Object* next = p.Find("Head value");  // the first row under Hits
    if (!label || !value || !next || p.Find(kTimeValue)) return {};
    float y = value->Y(), pitch = next->Y() - y;
    if (pitch <= 0.0f) return {};
    // everything on screen under Hits moves down a row (the choreography text sits off screen at x 310)
    for (feng::Object& o : p.objects)
        if (!o.Parent() && o.Y() > y + 1.0f && std::abs(o.X()) < 300.0f) o.Move(0.0f, pitch);
    uint32_t guid = p.NextGuid();
    feng::Object l = *label, v = *value;
    l.SetName(kTimeLabel);
    l.SetGuid(guid++);
    l.SetText("Mission time", "MISSION_TIME");
    l.Move(0.0f, pitch);
    v.SetName(kTimeValue);
    v.SetGuid(guid++);
    v.Move(0.0f, pitch);
    size_t at = p.Index("Hits value");
    p.objects.insert(p.objects.begin() + at + 1, {l, v});
    return p.Write();
}

namespace {

// a copy of an On/Off row (its "<pattern> text", " Off", " On" and " button" with the arrows) named row, moved down,
// after the button named after (and its arrows); false when an object is missing or the row is there already
bool CopyToggleRow(feng::Package& p, const std::string& pattern, const std::string& row, const char* label,
                   float down, const std::string& after) {
    size_t at = p.Index(pattern + " button"), last = p.Index(after + " button");
    if (at == p.objects.size() || last == p.objects.size() || p.Find(row + " button") || down <= 0.0f) return false;
    uint32_t guid = p.NextGuid(), group_like = p.objects[at].Guid();
    std::vector<feng::Object> rows;
    for (const char* part : {" text", " Off", " On"}) {
        const feng::Object* o = p.Find(pattern + part);
        if (!o) return false;
        feng::Object c = *o;
        c.SetName(row + part);
        c.SetGuid(guid++);
        if (std::string(part) == " text") c.SetText(row, label);
        c.Move(0.0f, down);
        rows.push_back(c);
    }
    // the button and its arrows
    uint32_t group = guid++;
    feng::Object b = p.objects[at];
    b.SetName(row + " button");
    b.SetGuid(group);
    b.Move(0.0f, down);
    rows.push_back(b);
    for (size_t i = at + 1; i < p.objects.size() && p.objects[i].Parent() == group_like; i++) {
        feng::Object a = p.objects[i];
        a.SetGuid(guid++);
        a.SetParent(group);
        rows.push_back(a);
    }
    // the highlight messages reach the new button too
    p.AddTarget(group_like, group);
    uint32_t parent = p.objects[last].Guid();
    while (last + 1 < p.objects.size() && p.objects[last + 1].Parent() == parent) last++;
    p.objects.insert(p.objects.begin() + last + 1, rows.begin(), rows.end());
    return true;
}

}  // namespace

std::vector<uint8_t> AddHudRows(const uint8_t* data, size_t size) {
    std::optional<feng::Package> parsed = feng::Package::Parse(data, size);
    if (!parsed) return {};
    feng::Package& p = *parsed;
    const feng::Object* first = p.Find("Crosshairs text");
    const feng::Object* second = p.Find("HUD text");
    if (!first || !second) return {};
    // two and three rows under Crosshairs, at the rows' pitch, after HUD Type's and then Mission Timer's button
    float pitch = second->Y() - first->Y();
    if (!CopyToggleRow(p, "Crosshairs", kTimerRow, "MISSION_TIMER", 2.0f * pitch, "HUD")) return {};
    if (!CopyToggleRow(p, "Crosshairs", kFpsRow, "FPS_COUNTER", 3.0f * pitch, kTimerRow)) return {};
    return p.Write();
}

std::vector<uint8_t> AddAimRow(const uint8_t* data, size_t size) {
    std::optional<feng::Package> parsed = feng::Package::Parse(data, size);
    if (!parsed) return {};
    feng::Package& p = *parsed;
    const feng::Object* first = p.Find("Vibrate text");
    const feng::Object* last = p.Find("Invert Y Axis text");
    if (!first || !last) return {};
    // Vibrate, Analog Sensitivity, Turn Speed, Invert Y Axis: one row pitch under the last
    float down = (last->Y() - first->Y()) / 3.0f;
    if (!CopyToggleRow(p, "Invert Y Axis", kAimRow, "MODERN_AIMING", down, "Invert Y Axis")) return {};
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
