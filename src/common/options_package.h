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
inline constexpr const char* kPauseTabEvents[] = {
    "Mouse Pause Objectives", "Mouse Pause Notes", "Mouse Pause Preferences", "Mouse Pause Exit"};

// the row's objects the screen's handler shows text in and finds the focus by: part "text", "value" or "button"
std::string RowObject(size_t row, const char* part);

// PCOptions.fng from HUD.fng's bytes (the shell's or the pause screen's): the HUD rows give way to one per label,
// each a name, a value the handler fills in and a button whose arrows flash on the focus; empty on failure
std::vector<uint8_t> MakeScreen(const uint8_t* hud, size_t size, const std::vector<std::string>& labels);

// an options list with a PC Options entry under Audio opening PCOptions.fng from the same folder: the shell's
// Option_overlay.fng, the pause screen's Preferences.fng or MPPreferences.fng; empty on failure
std::vector<uint8_t> AddEntry(const uint8_t* list, size_t size);

// Direct mouse tab transitions, cloned from each pause page's native left/right cleanup response.
// Empty for packages other than pause pages, or when a native transition could not be found.
std::vector<uint8_t> AddPauseTabs(const uint8_t* data, size_t size);

}  // namespace game::menu
