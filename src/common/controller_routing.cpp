#include "common/controller_routing.h"

#include <mutex>

#include "core/log.h"
#include "kernel/kernel.h"

namespace game {
namespace {
std::mutex g_routing_lock;
ControllerRouting g_routing;
}

void UpdateControllerMode(const ControllerModeAddresses& a) {
    bool shell = kernel::G<uint8_t>(a.shell) != 0;
    bool split = shell ? kernel::G<uint8_t>(a.shell_coop) || kernel::G<uint8_t>(a.shell_multiplayer) :
                         kernel::G<uint8_t>(a.game_coop) || kernel::G<uint8_t>(a.game_multiplayer);
    int old_owner;
    {
        std::lock_guard lock(g_routing_lock);
        if (g_routing.Split() == split) return;
        old_owner = g_routing.Split() ? 1 : 0;
    }
    // Stop the old owner's motors before changing the mapping. No routing lock across the SDK callback.
    input::Rumble(old_owner, 0, 0);
    {
        std::lock_guard lock(g_routing_lock);
        g_routing.SetSplit(split);
    }
    LOGR("input: %s", split ? "split screen: KBM port 1, controllers ports 2-4" :
                               "singleplayer: KBM and first controller share port 1");
}

int ControllerPort(int port, bool rumble) {
    std::lock_guard lock(g_routing_lock);
    return g_routing.Device(port, rumble);
}

void FilterController(int port, input::Pad& pad) {
    std::lock_guard lock(g_routing_lock);
    g_routing.Filter(port, pad);
}

}  // namespace game
