#pragma once
// Guest memory the host keeps for the game (strings handed to it, menu packages built on the host), across quick
// loads.
//
// A quick load puts back all guest memory and the allocators as they were when the save was made, by this instance
// or another one (a save made before the game relaunched into this instance). A block allocated after that point is
// gone: its address is free in the allocator or already another allocation's, and what it holds is the save's. A
// pause screen package built after the save then read as zeros and the game crashed on it.
//
// A GuestBlock ends in a cookie this instance makes up, and is used again only while its address is still allocated
// with its size and the cookie is still there; otherwise a new block is allocated (the old address, allocated or not
// in the save, is the save's and is left alone).
#include <cstdint>
#include <string>
#include <vector>

namespace game {

class GuestBlock {
public:
    // the block of size bytes, the kept one while it is still ours, else a new zeroed one (fresh set); 0 when the
    // allocation fails
    uint32_t Get(uint32_t size, bool* fresh = nullptr);
    // the block holding these bytes: the kept one as it is while it is still ours (the game may have changed it),
    // else a new one with the bytes copied in
    uint32_t Hold(const void* data, uint32_t size, bool* fresh = nullptr);
    uint32_t Hold(const std::vector<uint8_t>& data, bool* fresh = nullptr) {
        return Hold(data.data(), uint32_t(data.size()), fresh);
    }
    uint32_t HoldText(const std::string& s) { return Hold(s.c_str(), uint32_t(s.size() + 1)); }

private:
    bool Ours(uint32_t size) const;
    uint32_t va_ = 0, size_ = 0;
};

}  // namespace game
