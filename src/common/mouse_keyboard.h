#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/input.h"

// the keyboard and mouse as the controller on port 0, after BLACK-XBERecomp's
namespace input {

void StartMouse(void* hwnd);     // window thread, the game window is up
void OnRawInput(void* handle);   // window thread, WM_INPUT
void ReleaseCursor();            // the window lost the focus: the clip goes, held keys wait for release
void Keyboard(Pad& pad);         // held keys onto port 0's pad
void SeenPad(const Pad& pad);    // a controller's report before the keys join it: its input hands over to it
bool KeyboardActive();           // keyboard or mouse used last, not a controller
// the lean key held in play (it also presses the aim): Modern Aiming gives the player the game's lean on the ground
bool LeanHeld();
bool Playing();                  // the game took mouse counts in the last moment: the player has the view
// raw mouse counts since the last call, scaled by the settings; false when there are none. Asked by the game's look
// update, which runs in play only: being asked is what Playing reports.
bool TakeMouse(float& dx, float& dy);

struct MenuPointer {
    bool active = false;
    float x = 0, y = 0, width = 0, height = 0;
    uint32_t buttons = 0, pressed = 0;  // LMB bit 0, RMB bit 1
    int wheel = 0;
};
MenuPointer ReadMenuPointer();
void SetMenuInput(bool active);  // explicit FEng ownership, independent of the gameplay look timeout
void UseMenuMouse();
void MenuResume();             // a short Start press through the normal pause handling
void SetFrontEnd(bool front_end);  // guest thread: in the front-end shell the pause key goes back instead
void SetSaveMenu(bool save_menu);  // guest thread: so it does on the in-game save screens (a save machine's)
// what the weapon keys asked for in play since the last call: a group (1 primary, 2 secondary, 3 special), or steps
// through every weapon (next positive, previous negative), or nothing; a press waits a moment for the game to take it
struct WeaponRequest {
    int group = 0;
    int steps = 0;
};
WeaponRequest TakeWeaponRequest();
// guest thread, each look update of KBM's player: a sniper scope is up, so the wheel zooms instead of switching
void SetScoped(bool scoped);

// actions the keyboard and mouse can be bound to in the settings file's [keys], key names comma separated
// letter, digit and symbol keys bind by position as the US key there
struct Action {
    const char* id;
    const char* defaults;
};
const Action* Actions(size_t* count);
void LoadBindings();  // after the settings load
// keys bound outside the pad, held while the game window has the focus
enum class Hotkey { QuickSave, QuickLoad };
bool HotkeyHeld(Hotkey key);

// PC Options' key rows (src/common/options_menu.h), on the guest thread. The keys bound to an action as the settings
// file names them, ", " between, "None" for none.
std::string BoundKeys(const char* id);
// the action bound to that key alone (a name from the settings file's list); other actions lose it, but for the
// wheel shared by the weapon steps and the sniper zoom. The bindings apply at once.
void BindKey(const char* id, const std::string& key);
void ClearKeys(const char* id);
// a key press for a key row: from the start until the guest takes the result every pad reads as released (nothing
// reaches the menus), held keys wait to be let go, then the first key or mouse button down is the result; Escape
// cancels, Backspace clears, a controller's B cancels. Keys held at the end wait for release before they act.
enum class Capture { None, Waiting, Key, Cancel, Clear };
void StartCapture();
Capture TakeCapture(std::string& key);  // Key with its name; Waiting until there is one
bool Capturing();
// every pad's report while capturing: blanked, true; false otherwise
bool CapturePad(Pad& pad);

}  // namespace input
