#pragma once
#include <cstddef>
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

// actions the keyboard and mouse can be bound to in the settings file's [keys], key names comma separated
// letter, digit and symbol keys bind by position as the US key there
struct Action {
    const char* id;
    const char* defaults;
};
const Action* Actions(size_t* count);
void LoadBindings();  // after the settings load

}  // namespace input
