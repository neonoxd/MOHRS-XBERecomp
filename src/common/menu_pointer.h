#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game::menu {

struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool Valid() const {
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom) &&
               right > left && bottom > top;
    }
    bool Contains(float x, float y, float pad = 0) const {
        return Valid() && x >= left - pad && x < right + pad && y >= top - pad && y < bottom + pad;
    }
    void Include(const Rect& r) {
        if (!r.Valid()) return;
        if (!Valid()) { *this = r; return; }
        left = std::min(left, r.left); top = std::min(top, r.top);
        right = std::max(right, r.right); bottom = std::max(bottom, r.bottom);
    }
};

// Invert BlitFrame's fitted picture and Hud::ScreenX's horizontal menu compression. Coordinates coming from the
// FEng drawing hooks already include animation, font justification, parent transforms and the game's safe area.
inline bool MapPointer(float client_x, float client_y, float client_w, float client_h,
                       float guest_w, float guest_h, bool wide, float& x, float& y) {
    if (client_w <= 0 || client_h <= 0 || guest_w <= 0 || guest_h <= 0) return false;
    float aspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = std::min(client_w, client_h * aspect), h = w / aspect;
    float left = (client_w - w) * 0.5f, top = (client_h - h) * 0.5f;
    if (client_x < left || client_x >= left + w || client_y < top || client_y >= top + h) return false;
    x = (client_x - left) * guest_w / w;
    y = (client_y - top) * guest_h / h;
    if (wide) x = guest_w * 0.5f + (x - guest_w * 0.5f) * (4.0f / 3.0f);
    return true;
}

// Package/focus changes swallow held buttons until release. A raw-input press also catches a tap between polls.
struct PointerButtons {
    uint32_t held = 0, blocked = 0;
    uint32_t Advance(uint32_t down, uint32_t raw_pressed, bool settle) {
        if (settle) blocked |= down | raw_pressed;
        uint32_t pressed = ((down & ~held) | raw_pressed) & ~blocked;
        blocked &= down;
        held = down;
        return pressed;
    }
};

}  // namespace game::menu
