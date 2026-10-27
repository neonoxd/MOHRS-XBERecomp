#pragma once
#include "common/options_menu.h"

namespace game {

struct MenuMouseCalls {
    uint32_t focus, message, object_message, measure_text, navigate;
    uint32_t scale_x, scale_y, offset_x, offset_y, width, height;
};

// FEng's update and drawing adapters. All guest reads/calls and geometry collection stay on the guest UI thread.
void MenuMouseUpdate(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls);
void MenuMouseRender(X86Context& ctx, uint8_t* base, GuestFunction original);
void MenuMouseObject(X86Context& ctx, uint8_t* base, GuestFunction original);
void MenuMouseText(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls);
void MenuMouseSprite(X86Context& ctx, uint8_t* base, GuestFunction original, const MenuMouseCalls& calls);

}  // namespace game
