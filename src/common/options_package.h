#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// the PC options screen as FEng packages (src/common/feng.h): made from the game's HUD options screen, and the entry
// opening it added to the options lists. Host only, so tests/options_package_test.cpp checks them on the game's files.
namespace game::menu {

inline constexpr char kScreen[] = "PCOptions.fng";
inline constexpr size_t kRows = 10;
// the pause screen's cheats (src/common/cheats.h), made the same way and listed under PC Options in Preferences.fng
inline constexpr char kCheatsScreen[] = "Cheats.fng";
inline constexpr size_t kCheatRows = 8;

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

// PCOptions.fng from HUD.fng's bytes (the shell's or the pause screen's): the HUD rows give way to one per label,
// each a name, a value the handler fills in and a button whose arrows flash on the focus; empty on failure
std::vector<uint8_t> MakeScreen(const uint8_t* hud, size_t size, const std::vector<std::string>& labels,
                                const ScreenSpec& spec = kPcOptions);

// an options list with an entry opening the screen from the same folder: in the shell's Option_overlay.fng under
// Audio, in the pause screen's Preferences.fng or MPPreferences.fng under the last entry; empty on failure or when
// the list has it already
std::vector<uint8_t> AddEntry(const uint8_t* list, size_t size, const ScreenSpec& spec = kPcOptions);

// Direct mouse tab transitions, cloned from each pause page's native left/right cleanup response.
// Empty for packages other than pause pages, or when a native transition could not be found.
std::vector<uint8_t> AddPauseTabs(const uint8_t* data, size_t size);

}  // namespace game::menu
