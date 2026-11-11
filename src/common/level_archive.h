#pragma once
// Weapons from other levels (docs/menus.md "Weapons from other levels"): a level archive (data\1\1_N\level.viv) with
// the player weapons of the other campaign levels added. Host only, so tests/level_archive_test.cpp checks it on the
// game's files.
//
// A level's player weapons are its object database's (levelfile.lfc) class 0x21 entries, one a weapon: its skeleton,
// first person mesh, world model, crosshair, textures and animation states (.aso) by file name, its name and its
// weapon hash. The weapon's script (a .cbs whose base is PlayerWeapon_Base, holding the weapon hash), its instance
// (.sin) and its animation states are class 0x9, 0x8 and 0xA entries naming the files. All of it is fetched by name
// from the level archive. The first person animations are in the level's anim.bnk, so the archive also gets an
// animation bank that holds them (multiplayer map 2_1's, which has every player weapon's but the fire
// extinguisher's) as kBankFile, which the game is made to search after its own (src/common/level_weapons.h).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace game::levels {

inline constexpr char kBankFile[] = "mohrs_weapons.bnk";
// a .viv keeps offsets and sizes in 3 bytes
inline constexpr size_t kMaxArchive = 0xFFFFFF;

struct VivEntry {
    std::string name;
    uint32_t offset = 0, size = 0;
};
// the directory of a C0FB archive (big endian: magic, directory size less 4, count, then 3-byte offset, 3-byte size
// and name per file); empty when it is none
std::vector<VivEntry> ParseViv(const uint8_t* data, size_t size);
// the directory's size from the first 6 bytes, 0 when they are no archive's
size_t VivDirectorySize(const uint8_t* head, size_t size);

// an archive read a file at a time (a level on the disc image, or a file in a test)
class Source {
public:
    virtual ~Source() = default;
    virtual const std::vector<VivEntry>& Entries() = 0;
    virtual std::vector<uint8_t> Read(const VivEntry& entry) = 0;
    const VivEntry* Find(const std::string& name);  // any case
    std::vector<uint8_t> Read(const std::string& name);
};

struct Merged {
    std::vector<uint8_t> archive;
    std::vector<std::string> weapons;  // the weapons added, by name
    std::vector<uint32_t> hashes;      // their weapon hashes
    std::vector<Source*> donors;       // the level each came from
    std::vector<std::vector<std::string>> anim_states;  // each one's .aso files
    size_t added = 0;                  // bytes more than the level's own archive
};

// The level's archive with every player weapon it lacks from the donors (campaign levels, the first holding a
// weapon gives it) and the bank. None when the level has no player weapons or nothing is missing.
// A weapon's world model (.msh) is not added, and the entry names the level's first weapon's instead: a static mesh
// takes its textures from the level's own texture data (0x10D6F0, 0x113360), so another level's breaks the level's
// load, and a missing one is not checked for. The settings entry's holder pool is raised to hold them all.
std::optional<Merged> MergeWeapons(const std::vector<uint8_t>& level, const std::vector<Source*>& donors,
                                   const std::vector<uint8_t>& bank, size_t most_added = kMaxArchive);

}  // namespace game::levels
