#pragma once
// The sounds of weapons from other levels (src/common/level_archive.h, docs/menus.md). Host only, so
// tests/level_sound_test.cpp checks it on the game's files.
//
// A weapon's sounds are events named WpnFire_<name>, WpnSlct_<name> and WpnRld_<name> (each also with _Zone<n>), the
// name from the game's table at 0x202740 by the weapon definition's +0x60 (weapbul_*.dat). The level's sound event
// file (sound\<m>_<l>.ubr, "UBER") holds them: records of fixed columns sorted by the event's hash (the type 6
// column), each naming a bank by a bit mask (bit 30 the script bank, sound\<m><l>scr.abk, which stays loaded) and a
// sample index in it. A level holds only its own weapons'. The merge copies another level's records for an event the
// level lacks into its event file, and their samples (BNKl: a header, a per-sample header offset table, PT headers
// whose tags 0x88/0x89 are absolute data offsets, then the data) to the end of its script bank.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game::levels {

// the game's name hash (0x10EB60): CRC-32
uint32_t NameHash(const std::string& name);
// the hashes of a weapon's sound events, by its sound name ("Springfield")
std::vector<uint32_t> WeaponSoundEvents(const std::string& sound_name);
// the events of an event file whose hashes a file holds: a weapon's animation states (.aso) name their foley (the
// Springfield's bolt, the shotgun's pump) by hash
std::vector<uint32_t> EventsNamedIn(const std::vector<uint8_t>& file, const std::vector<uint8_t>& events);
// a weapon definition's sound name index (+0x60) in weapbul_*.dat, by the weapon's hash; -1 when it has none
int WeaponSoundIndex(const std::vector<uint8_t>& weapbul, uint32_t weapon_hash);

struct SoundDonor {
    std::vector<uint8_t> events, bank;  // its .ubr and script bank
};
struct SoundWant {
    const SoundDonor* donor = nullptr;
    std::vector<uint32_t> events;  // the event hashes to take from it
};
struct SoundMerged {
    std::vector<uint8_t> events, bank;
    int records = 0, samples = 0;
};

// The level's event file and script bank with the wanted events the level lacks and their samples. None when
// nothing was added or a file is not as expected.
std::optional<SoundMerged> MergeSounds(const std::vector<uint8_t>& events, const std::vector<uint8_t>& bank,
                                       const std::vector<SoundWant>& wants);

}  // namespace game::levels
