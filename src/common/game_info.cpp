#include "app/app.h"
#include "common/options.h"

game::Options& game::Opt() {
    static Options o;
    return o;
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Medal of Honor: Rising Sun",  // window and splash
    .short_title = L"MOHRS",                 // message boxes
    .name = L"MOHRS",                        // window classes, crash dumps, handover event
    .log = "mohrs",                          // log file prefix
    .settings = "settings.toml",             // settings file beside the exe
    .splash = L"splash",                     // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MOHRS"}, {"moh3rdc.xbe", "MOHRSCoop"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr},          // game settings kept in the settings file
                {"skip_cutscenes", &game::Opt().skip_cutscenes, nullptr}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .widescreen = false,                  // 4:3 only: never reads the video flags, creates its device without the widescreen flag
};
