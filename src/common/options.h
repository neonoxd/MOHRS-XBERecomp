#pragma once
// MOHRS settings kept in the settings file beside the exe, beyond the SDK's own
namespace game {

struct Options {
    bool skip_intros = false;     // the EA logo and the opening Pearl Harbor movie at boot
    bool skip_cutscenes = false;  // each level's intro and outro movies
};

Options& Opt();

}  // namespace game
