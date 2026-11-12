// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/shader_cache_test.cpp
//     sdk/runtime/gpu/shader_cache.cpp sdk/runtime/core/log.cpp -ld3dcompiler -o re/shader_cache_test.exe
// re\shader_cache_test.exe
// The disk shader cache: a compiled shader stored and read back byte for byte, a missing or damaged file is a miss,
// and opening with another tag removes the old tag's folder.
#include <windows.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/log.h"
#include "gpu/shader_cache.h"

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) g_failures++;
}

int main() {
    namespace fs = std::filesystem;
    fs::path dir = fs::path("re") / "shader_cache_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    logx::Init(dir.string(), "cache", false);

    const char* src = "float4 main(float4 p : POSITION) : SV_Position { return p * 2; }";
    ID3DBlob* code = nullptr;
    HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr);
    Check(SUCCEEDED(hr) && code, "a shader compiles");
    if (!code) return 1;
    const auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
    size_t n = code->GetBufferSize();
    Check(gpu::ValidDxbc(bytes, n), "D3DCompile's output is a valid container");
    Check(!gpu::ValidDxbc(bytes, n - 1), "a cut one is not");

    gpu::ShaderCache cache;
    cache.Open(dir / "shadercache", 0x1111);
    Check(cache.On() && fs::is_directory(dir / "shadercache" / "0000000000001111"), "Open makes the tag's folder");
    std::vector<uint8_t> out;
    Check(!cache.Load(0xABC, out), "an unknown key is a miss");
    cache.Store(0xABC, bytes, n);
    Check(cache.Load(0xABC, out) && out.size() == n && !std::memcmp(out.data(), bytes, n), "stored, read back the same");
    bool tmp_left = false;
    for (const auto& e : fs::directory_iterator(cache.Dir()))
        if (e.path().extension() == ".tmp") tmp_left = true;
    Check(!tmp_left, "no temporary file left");
    cache.Store(0xABC, bytes, n);
    Check(cache.Load(0xABC, out) && out.size() == n, "storing again replaces it");

    fs::path file = cache.Dir() / "0000000000000ABC.dxbc";
    {
        std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(0);
        f.write("XXXX", 4);
    }
    Check(!cache.Load(0xABC, out), "a damaged file is a miss");

    gpu::ShaderCache other;
    other.Open(dir / "shadercache", 0x2222);
    Check(!fs::exists(dir / "shadercache" / "0000000000001111") && fs::is_directory(other.Dir()),
          "another tag removes the old folder");
    Check(!other.Load(0xABC, out), "and its shaders");

    gpu::ShaderCache off;
    Check(!off.On() && !off.Load(0xABC, out), "an unopened cache is off");
    off.Store(0xABC, bytes, n);
    code->Release();
    if (g_failures) return 1;
    std::printf("all passed\n");
    return 0;
}
