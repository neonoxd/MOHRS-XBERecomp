// compiled shaders on disk, see shader_cache.h
#include "gpu/shader_cache.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

#include "core/log.h"

namespace gpu {

namespace fs = std::filesystem;

bool ValidDxbc(const uint8_t* p, size_t n) {
    // "DXBC", a 16 byte checksum, version 1, the container's size, the chunk count
    if (n < 32 || std::memcmp(p, "DXBC", 4) != 0) return false;
    uint32_t size;
    std::memcpy(&size, p + 24, 4);
    return size == n;
}

void ShaderCache::Open(const fs::path& dir, uint64_t tag) {
    char name[17];
    std::snprintf(name, sizeof name, "%016llX", (unsigned long long)tag);
    std::error_code ec;
    fs::create_directories(dir / name, ec);
    if (ec) {
        LOGE("shader cache: cannot make %s: %s", (dir / name).string().c_str(), ec.message().c_str());
        dir_.clear();
        return;
    }
    // another compiler or other flags: those shaders are never read again
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec) || e.path().filename() == name) continue;
        std::error_code rm;
        fs::remove_all(e.path(), rm);
    }
    dir_ = dir / name;
}

fs::path ShaderCache::Path(uint64_t key) const {
    char name[24];
    std::snprintf(name, sizeof name, "%016llX.dxbc", (unsigned long long)key);
    return dir_ / name;
}

bool ShaderCache::Load(uint64_t key, std::vector<uint8_t>& out) const {
    if (!On()) return false;
    std::ifstream f(Path(key), std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff n = f.tellg();
    if (n <= 0 || n > (64 << 20)) return false;
    out.resize(size_t(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) return false;
    if (!ValidDxbc(out.data(), out.size())) {
        LOGE("shader cache: %s is no shader, compiling again", Path(key).string().c_str());
        return false;
    }
    return true;
}

void ShaderCache::Store(uint64_t key, const void* data, size_t n) const {
    if (!On()) return;
    static std::atomic<uint32_t> serial{0};
    fs::path to = Path(key);
    fs::path tmp = to;
#ifdef _WIN32
    tmp += "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(serial++) + ".tmp";
#else
    tmp += "." + std::to_string(serial++) + ".tmp";
#endif
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(static_cast<const char*>(data), std::streamsize(n))) {
            LOG_FIRST(4, "shader cache: cannot write %s", tmp.string().c_str());
            std::error_code ec;
            f.close();
            fs::remove(tmp, ec);
            return;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
#else
    std::error_code mv;
    fs::rename(tmp, to, mv);
    if (mv) {
#endif
        std::error_code ec;
        fs::remove(tmp, ec);
    }
}

}  // namespace gpu
