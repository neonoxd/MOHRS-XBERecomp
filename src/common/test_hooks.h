#pragma once
// Hook bodies both images share (default.xbe in src/game, moh3rdc.xbe in src/coop); each image wraps them at its
// own addresses of the same game functions.
#include <cstdint>

#include "cpu/x86_context.h"

namespace game {

// Development: MOHRS_START_LEVEL=<mission>_<level> (e.g. 1_2) starts that level instead of the first one. The boot
// picks the level with the game state's SetCurrentLevel(mission, level): 1, 1 on a fresh profile (legal intro, then
// the level), 1, 16 for the logo and the front end. Called at that function's entry; changes only its first call,
// and only the fresh profile's 1, 1, so the menus and later level changes behave as on the console.
void PickStartLevel(X86Context& ctx);

// Movie skipping, after the game's config GetString(key, buffer, size) returned found: game.cfg names each movie,
// and "none" there means no movie. Answers "none" for legal_movie and legal_intro_movie (EA logo, the opening Pearl
// Harbor movie) with skip_intros, and for each level's intro_movie and outro_movie with skip_cutscenes. The bonus
// movies picked in the menus play.
void SkipMovie(uint32_t key, uint32_t buffer, uint32_t size);

}  // namespace game
