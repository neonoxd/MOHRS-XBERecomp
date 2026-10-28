#pragma once
#include <cstdint>

#include "core/input.h"

namespace game {

// Port 0 is always KBM. The first pad shares it in singleplayer and has port 1 to itself in split screen.
// Port 1 stays connected (but silent) in shared mode, so entering setup never waits for XAPI hotplug polling.
class ControllerRouting {
public:
    bool Split() const { return split_; }
    bool SetSplit(bool split) {
        if (split == split_) return false;
        split_ = split;
        capture_ = true;
        return true;
    }

    int Device(int port, bool rumble) const {
        if (port < 0 || port > 3) return -1;
        if (port == 0) return split_ ? -1 : 0;
        if (port == 1) return rumble && !split_ ? -1 : 0;
        return port - 1;
    }

    void Filter(int port, input::Pad& pad) {
        if ((port == 0 && split_) || (port == 1 && !split_)) {
            bool connected = pad.connected;
            pad = {};
            pad.connected = connected;
            return;
        }
        if (port != (split_ ? 1 : 0)) return;
        // Buttons held while changing owners cannot become a new Join/Accept/Fire press on the new port.
        if (capture_) {
            blocked_ = pad;
            capture_ = false;
        }
        blocked_.buttons &= pad.buttons;
        pad.buttons &= ~blocked_.buttons;
        for (int i = 0; i < 8; i++) {
            if (pad.analog[i] <= 1) blocked_.analog[i] = 0;
            if (blocked_.analog[i] > 1) pad.analog[i] = 0;
        }
        SettleAxis(pad.lx, blocked_.lx); SettleAxis(pad.ly, blocked_.ly);
        SettleAxis(pad.rx, blocked_.rx); SettleAxis(pad.ry, blocked_.ry);
    }

private:
    static void SettleAxis(int16_t& value, int16_t& blocked) {
        constexpr int deadzone = 8000;
        if (value >= -deadzone && value <= deadzone) blocked = 0;
        if (blocked < -deadzone || blocked > deadzone) value = 0;
    }
    bool split_ = false, capture_ = false;
    input::Pad blocked_{};
};

struct ControllerModeAddresses {
    uint32_t shell, shell_coop, shell_multiplayer, game_coop, game_multiplayer;
};

// Guest-thread publication before the native pad update. Background XAPI polling never reads guest mode data.
void UpdateControllerMode(const ControllerModeAddresses& addresses);
int ControllerPort(int port, bool rumble);
void FilterController(int port, input::Pad& pad);

}  // namespace game
