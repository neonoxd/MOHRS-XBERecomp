#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// the PC options screen as FEng packages (src/common/feng.h): made from the game's HUD options screen, and the entry
// opening it added to the options lists. Host only, so tests/options_package_test.cpp checks them on the game's files.
namespace game::menu {

inline constexpr char kScreen[] = "PCOptions.fng";
// PC Options' rows: the first picks the category, the others show its settings; each place under it holds a value
// row and a key row, the handler enabling one of them (or neither) for the category shown
inline constexpr size_t kRows = 10;
inline constexpr size_t kKeyRows = kRows - 1;
// the pause screen's cheats (src/common/cheats.h), made the same way and listed under PC Options in Preferences.fng
inline constexpr char kCheatsScreen[] = "Cheats.fng";
inline constexpr size_t kCheatRows = 10;

// a screen made from HUD.fng and the list entry opening it
struct ScreenSpec {
    const char* file;         // the package's name, next to HUD.fng
    const char* title;        // the title text and its label
    const char* title_label;
    const char* entry;        // the list entry's object name, text and label
    const char* entry_text;
    const char* entry_label;
};
inline constexpr ScreenSpec kPcOptions{kScreen, "PC OPTIONS", "PC_OPTIONS_CAPS", "PC Options button", "PC Options",
                                       "PC_OPTIONS"};
inline constexpr ScreenSpec kCheats{kCheatsScreen, "CHEATS", "PC_CHEATS_CAPS", "Cheats button", "Cheats", "PC_CHEATS"};
inline constexpr const char* kPauseTabEvents[] = {
    "Mouse Pause Objectives", "Mouse Pause Notes", "Mouse Pause Preferences", "Mouse Pause Exit"};

// the row's objects the screen's handler shows text in and finds the focus by: part "text", "value" or "button"
std::string RowObject(size_t row, const char* part);
// a key row's, at the place of value row `row`
std::string KeyObject(size_t row, const char* part);

// PCOptions.fng from HUD.fng's bytes (the shell's or the pause screen's): the HUD rows give way to one per label,
// each a name, a value the handler fills in and a button whose arrows flash on the focus. The last `keys` places
// also get a key row: the same objects, empty, whose button answers Button_Pressed with its flash and the message
// sent on to the game (response type 2, as the package's Pad_Left), so a press on it goes to the handler instead of
// the package's accept. Empty on failure.
std::vector<uint8_t> MakeScreen(const uint8_t* hud, size_t size, const std::vector<std::string>& labels,
                                const ScreenSpec& spec = kPcOptions, size_t keys = 0);

// an options list with an entry opening the screen from the same folder: in the shell's Option_overlay.fng under
// Audio, in the pause screen's Preferences.fng or MPPreferences.fng under the last entry; empty on failure or when
// the list has it already
std::vector<uint8_t> AddEntry(const uint8_t* list, size_t size, const ScreenSpec& spec = kPcOptions);

// the end-of-level stats (EndOfLevelSummary.fng) with a mission time row under Hits, made from the Hits row: its
// label and a value the stats handler fills in (src/common/options_menu.h); the rows below and the medal text move
// down a row. Empty on failure or when the package has it already.
inline constexpr char kSummary[] = "EndOfLevelSummary.fng";
inline constexpr char kTimeLabel[] = "Mission Time label";
inline constexpr char kTimeValue[] = "Mission Time value";
std::vector<uint8_t> AddMissionTime(const uint8_t* data, size_t size);

// HUD Options (HUD.fng, the shell's and the pause screen's) with Mission Timer, FPS Counter, HUD Position and HUD
// Offset rows under HUD Type, made from the Crosshairs row: its text, the game's On and Off texts, which the handler
// shows one of, and its button with the arrows. HUD Position and HUD Offset have one " value" text (made from On)
// instead, which the handler fills in. Empty on failure or when the package has them already.
inline constexpr char kHudScreen[] = "HUD.fng";
inline constexpr char kTimerRow[] = "Mission Timer";  // the objects: "<row> text", " On", " Off", " button"
inline constexpr char kFpsRow[] = "FPS Counter";
inline constexpr char kHudPositionRow[] = "HUD Position";  // the objects: "<row> text", " value", " button"
inline constexpr char kHudOffsetRow[] = "HUD Offset";
std::vector<uint8_t> AddHudRows(const uint8_t* data, size_t size);

// Controller Options (ControllerOptions.fng, the shell's and the pause screen's) with a Modern Aiming row under
// Invert Y Axis, made from that row the same way (src/common/modern_aim.h). Empty on failure or when the package has
// it already.
inline constexpr char kControllerScreen[] = "ControllerOptions.fng";
inline constexpr char kAimRow[] = "Modern Aiming";
std::vector<uint8_t> AddAimRow(const uint8_t* data, size_t size);

// Exit to Desktop as the last entry of the shell's main menu (Single_Multiplayer_selection.fng, titled MAIN MENU: a
// copy of Multiplayer under it) and of the pause screen's exit page (LoadRestart.fng, a copy of Exit to Shell under
// it), a row pitch down. A press
// sends kExitMessage on to the game (response type 2), which the FEng message queue's hook takes
// (src/common/options_menu.h). Empty for other packages, on failure or when the package has it already.
inline constexpr char kMainMenu[] = "Single_Multiplayer_selection.fng";
inline constexpr char kExitEntry[] = "Exit to Desktop button";
inline constexpr char kExitMessage[] = "MOHRS Exit To Desktop";
std::vector<uint8_t> AddExitEntry(const uint8_t* data, size_t size);

// Direct mouse tab transitions, cloned from each pause page's native left/right cleanup response.
// Empty for packages other than pause pages, or when a native transition could not be found.
std::vector<uint8_t> AddPauseTabs(const uint8_t* data, size_t size);

}  // namespace game::menu
