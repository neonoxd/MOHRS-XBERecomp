// Host test: native mode flags, physical device routing, transition holds, rumble and mouse ownership.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isrc -Isdk/runtime tests/controller_routing_test.cpp
//     src/common/controller_routing.cpp src/common/mouse_look.cpp -o re/controller_routing_test.exe
#include "common/controller_routing.h"
#include "common/mouse_look.h"
#include "kernel/kernel.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
std::array<uint8_t, 0x40000> memory{};
std::array<input::Pad, 3> controllers{};
constexpr game::ControllerModeAddresses mode{0x1000, 0x1001, 0x1002, 0x1003, 0x1004};
constexpr uint32_t players = 0x2000, manager = 0x3000, first = 0x10000, second = 0x14000;
int stopped_port = -1, stopped_device = -1, stops = 0, mouse_reads = 0, ticks = 0;
bool mouse_available = true;

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

input::Pad Poll(int port, bool keyboard = false) {
    int device = game::ControllerPort(port, false);
    input::Pad pad = device >= 0 ? controllers.at(device) : input::Pad{};
    game::FilterController(port, pad);
    if (port == 0) {
        pad.connected = true;
        if (keyboard) { pad.analog[0] = 255; pad.ly = 32767; }
    }
    return pad;
}

bool Quiet(const input::Pad& p) {
    if (p.buttons || p.lx || p.ly || p.rx || p.ry) return false;
    for (auto a : p.analog) if (a) return false;
    return true;
}

void ModeTests() {
    for (auto& p : controllers) p.connected = true;
    controllers[0].analog[0] = 255;
    controllers[0].rx = 24000;
    controllers[1].analog[2] = 255;
    controllers[2].buttons = 0x10;
    Check(Poll(0).analog[0] == 255 && Poll(0).rx == 24000, "first pad controls player 1 in singleplayer");
    Check(Poll(0, true).ly == 32767, "keyboard works alongside the first pad");
    Check(Poll(1).connected && Quiet(Poll(1)), "dedicated first-pad port stays connected and silent in shared mode");
    Check(Poll(2).analog[2] == 255 && Poll(3).buttons == 0x10, "extra pads keep separate stable ports");
    Check(game::ControllerPort(0, true) == 0 && game::ControllerPort(1, true) == -1, "shared rumble has one owner");

    kernel::G<uint8_t>(mode.shell) = 1;
    kernel::G<uint8_t>(mode.shell_coop) = 1;
    game::UpdateControllerMode(mode);
    Check(stops == 1 && stopped_port == 0 && stopped_device == 0, "entering setup stops the old owner's motors");
    Check(Quiet(Poll(0)), "controller cannot act as KBM when joining");
    Check(Poll(0, true).analog[0] == 255, "keyboard can join independently");
    Check(Quiet(Poll(1)), "held A and right stick cannot join/navigate on the new port");
    controllers[0].analog[2] = 255;
    Check(Poll(1).analog[2] == 255 && Poll(1).analog[0] == 0, "fresh buttons work while the transition A remains blocked");
    controllers[0] = {.connected = true};
    Poll(1);
    controllers[0].analog[0] = 255;
    Check(Poll(1).analog[0] == 255 && Quiet(Poll(0)), "fresh controller A joins only its own player");
    Check(game::ControllerPort(0, true) == -1 && game::ControllerPort(1, true) == 0 &&
          game::ControllerPort(2, true) == 1, "split rumble follows physical device assignment");
    game::UpdateControllerMode(mode);
    Check(stops == 1 && Poll(1).analog[0] == 255, "repeated publication never re-blocks input");

    // The launched co-op image restores its native gameplay flags; pause keeps those flags.
    kernel::G<uint8_t>(mode.shell) = 0;
    kernel::G<uint8_t>(mode.shell_coop) = 0;
    kernel::G<uint8_t>(mode.game_coop) = 1;
    game::UpdateControllerMode(mode);
    Check(Poll(1).analog[0] == 255 && Quiet(Poll(0)), "co-op gameplay and pause retain separate devices");
    kernel::G<uint8_t>(mode.game_coop) = 0;
    kernel::G<uint8_t>(mode.game_multiplayer) = 1;
    game::UpdateControllerMode(mode);
    Check(Poll(1).analog[0] == 255, "PvP gameplay retains separate devices");

    // Shell flags take priority over stale gameplay flags after returning to the main menu.
    kernel::G<uint8_t>(mode.shell) = 1;
    game::UpdateControllerMode(mode);
    Check(stops == 2 && stopped_port == 1 && stopped_device == 0, "return to main menu stops the split owner's motors");
    Check(Quiet(Poll(0)) && Quiet(Poll(1)), "held A cannot accept on the main menu after returning");
    controllers[0] = {.connected = true};
    Poll(0);
    controllers[0].buttons = 0x10;
    Check(Poll(0).buttons == 0x10, "controller resumes controlling player 1 after release");
    kernel::G<uint8_t>(mode.shell_multiplayer) = 1;
    game::UpdateControllerMode(mode);
    Check(game::ControllerPort(0, true) == -1, "PvP setup also separates the devices before play");
    Poll(1);
    controllers[0] = {};
    Check(!Poll(1).connected && Poll(0).connected, "unplugging a pad leaves KBM available");
    controllers[0] = {.connected = true};
    controllers[0].analog[0] = 255;
    Check(Poll(1).analog[0] == 255 && Quiet(Poll(0)), "reconnection restores the same dedicated port");
    Check(game::ControllerPort(-1, false) == -1 && game::ControllerPort(4, true) == -1, "invalid ports map to no device");
}

void MouseTests() {
    kernel::W32(players, first); kernel::W32(players + 4, second);
    kernel::G<uint8_t>(manager + 0x23E0) = 2;
    kernel::G<uint8_t>(manager) = 1;
    kernel::G<uint8_t>(manager + 0x8F8) = 0;
    kernel::G<float>(first + 0xE30) = 35;
    kernel::G<float>(second + 0xE30) = 35;
    X86Context ctx{};
    ctx.ebp = 0x18000;
    ctx.esi = first;
    game::MouseLook(ctx, players, manager);
    Check(mouse_reads == 0 && ticks == 1, "controller player's update cannot consume mouse counts; timing still runs once");
    ctx.esi = second;
    game::MouseLook(ctx, players, manager);
    Check(mouse_reads == 1 && ticks == 1 && kernel::G<float>(second + 0x274) < 0 &&
          kernel::G<float>(first + 0x274) == 0, "mouse follows KBM when the controller joined first");
    kernel::G<uint8_t>(manager) = 0;
    kernel::G<uint8_t>(manager + 0x8F8) = 1;
    ctx.esi = first;
    game::MouseLook(ctx, players, manager);
    Check(mouse_reads == 2 && kernel::G<float>(first + 0x278) > 0, "KBM-first join order aims the first player");
    game::TurretLook(ctx, players, manager);
    Check(mouse_reads == 3 && kernel::G<float>(first + 0xE1C) < 0, "mounted guns use the same KBM assignment");
    kernel::G<uint8_t>(manager) = 2;
    game::MouseLook(ctx, players, manager);
    Check(mouse_reads == 3, "no KBM participant means mouse cannot steer a controller player");
    kernel::G<uint8_t>(manager + 0x23E0) = 1;
    kernel::G<uint8_t>(manager) = 0;
    game::MouseLook(ctx, players, manager);
    Check(mouse_reads == 4, "singleplayer mouse still aims player 1");
}
}

namespace kernel { uint8_t* g_base = memory.data(); }
namespace logx { void Write(Level, const char*, ...) {} }
namespace input {
void Rumble(int port, uint16_t left, uint16_t right) {
    Check(left == 0 && right == 0, "mode transitions only stop motors");
    stopped_port = port; stopped_device = game::ControllerPort(port, true); stops++;
}
bool TakeMouse(float& dx, float& dy) { mouse_reads++; dx = 10; dy = 5; return mouse_available; }
}
namespace game { void PlayerTick(float) { ticks++; } }

int main() {
    ModeTests(); MouseTests();
    std::puts("controller routing: all checks passed");
}
