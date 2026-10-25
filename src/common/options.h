#pragma once
#include <cstdint>
#include <map>
#include <string>

// MOHRS settings kept in the settings file beside the exe, beyond the SDK's own
namespace game {

struct Options {
    bool skip_intros = false;     // the EA logo and the opening Pearl Harbor movie at boot
    bool skip_cutscenes = false;  // each level's intro and outro movies
    // keyboard and mouse (src/common/mouse_keyboard.cpp)
    float mouse_sensitivity = 1.0f;  // horizontal, 0.05 degrees a count at 1
    float mouse_vertical_sensitivity = 1.0f;
    bool invert_mouse = false;  // the Y axis
    bool invert_mouse_x = false;
    uint32_t walk_speed = 50;  // percent of running, while the walk key is held
    std::map<std::string, std::string> keys;  // action id to key names, absent is the default
};

Options& Opt();

}  // namespace game
