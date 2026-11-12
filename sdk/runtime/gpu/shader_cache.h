#pragma once
// Compiled shaders kept on disk between runs: <dir>/<tag>/<key>.dxbc, key = the Hash of source, entry and profile,
// tag = the compiler and its flags. Opening removes the folders of other tags. See docs/performance.md.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gpu {

// a whole DXBC container: its magic and the size its header gives
bool ValidDxbc(const uint8_t* p, size_t n);

class ShaderCache {
public:
    // dir/<tag as 16 hex digits>, made when missing; the other folders in dir are removed
    void Open(const std::filesystem::path& dir, uint64_t tag);
    bool On() const { return !dir_.empty(); }
    // false when missing or not a valid container
    bool Load(uint64_t key, std::vector<uint8_t>& out) const;
    // written under a name of its own and renamed, so another instance never reads half a file
    void Store(uint64_t key, const void* data, size_t n) const;
    const std::filesystem::path& Dir() const { return dir_; }

private:
    std::filesystem::path Path(uint64_t key) const;
    std::filesystem::path dir_;
};

}  // namespace gpu
