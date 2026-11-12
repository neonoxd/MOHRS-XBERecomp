// tools\dev.bat clang++ -std=c++20 -O2 -D_CRT_SECURE_NO_WARNINGS tests/shader_compile_bench.cpp -ld3dcompiler
//     -ldxguid -o re/shader_compile_bench.exe
// re\shader_compile_bench.exe <folder> [rounds]
// Compiles every <hash>_<entry>_<profile>.hlsl in the folder (XBE_SHADER_SOURCES writes them) with each D3DCompile
// optimization setting, best of rounds (default 2): total and longest compile time, and the instruction count of
// the result against level 1's, the setting the renderer uses by default.
#define NOMINMAX
#include <windows.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Setting {
    const char* name;
    UINT flags;
};
constexpr Setting kSettings[] = {
    {"level 1", D3DCOMPILE_OPTIMIZATION_LEVEL1},  {"level 0", D3DCOMPILE_OPTIMIZATION_LEVEL0},
    {"skip", D3DCOMPILE_SKIP_OPTIMIZATION},       {"level 2", D3DCOMPILE_OPTIMIZATION_LEVEL2},
    {"level 3", D3DCOMPILE_OPTIMIZATION_LEVEL3},
};

struct Source {
    std::string name, text, entry, profile;
};

static uint32_t Instructions(ID3DBlob* code) {
    ID3D11ShaderReflection* r = nullptr;
    if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                          reinterpret_cast<void**>(&r))))
        return 0;
    D3D11_SHADER_DESC d{};
    r->GetDesc(&d);
    r->Release();
    return d.InstructionCount;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: shader_compile_bench <folder> [rounds]\n");
        return 2;
    }
    int rounds = argc > 2 ? std::max(1, std::atoi(argv[2])) : 2;
    std::vector<Source> sources;
    for (const auto& e : fs::directory_iterator(argv[1])) {
        if (e.path().extension() != ".hlsl") continue;
        std::string stem = e.path().stem().string();  // <16 hex>_<entry>_<xs_5_0>
        if (stem.size() < 16 + 1 + 1 + 7) continue;
        Source s;
        s.name = stem;
        s.profile = stem.substr(stem.size() - 6);
        s.entry = stem.substr(17, stem.size() - 17 - 7);
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream b;
        b << f.rdbuf();
        s.text = b.str();
        sources.push_back(std::move(s));
    }
    std::printf("%zu shaders, best of %d rounds\n", sources.size(), rounds);
    // [setting][shader]
    std::vector<std::vector<double>> ms(std::size(kSettings), std::vector<double>(sources.size(), 1e30));
    std::vector<std::vector<uint32_t>> ins(std::size(kSettings), std::vector<uint32_t>(sources.size(), 0));
    for (int round = 0; round < rounds; round++)
        for (size_t k = 0; k < std::size(kSettings); k++)
            for (size_t i = 0; i < sources.size(); i++) {
                const Source& s = sources[i];
                ID3DBlob* code = nullptr;
                ID3DBlob* err = nullptr;
                auto t = std::chrono::steady_clock::now();
                HRESULT hr = D3DCompile(s.text.data(), s.text.size(), nullptr, nullptr, nullptr, s.entry.c_str(),
                                        s.profile.c_str(), kSettings[k].flags, 0, &code, &err);
                double el = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
                if (FAILED(hr)) {
                    std::printf("%s %s failed: %s\n", kSettings[k].name, s.name.c_str(),
                                err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
                } else {
                    ms[k][i] = std::min(ms[k][i], el);
                    ins[k][i] = Instructions(code);
                }
                if (code) code->Release();
                if (err) err->Release();
            }
    std::printf("%-8s %10s %9s %9s %14s %14s\n", "setting", "total ms", "mean ms", "max ms", "instr vs lvl1",
                "worst shader");
    for (size_t k = 0; k < std::size(kSettings); k++) {
        double total = 0, worst = 0;
        uint64_t ours = 0, base = 0;
        double worst_ratio = 0;
        for (size_t i = 0; i < sources.size(); i++) {
            total += ms[k][i];
            worst = std::max(worst, ms[k][i]);
            ours += ins[k][i];
            base += ins[0][i];
            if (ins[0][i]) worst_ratio = std::max(worst_ratio, double(ins[k][i]) / ins[0][i]);
        }
        std::printf("%-8s %10.1f %9.2f %9.2f %13.1f%% %13.1f%%\n", kSettings[k].name, total,
                    total / std::max<size_t>(1, sources.size()), worst, base ? 100.0 * ours / base : 0.0,
                    100.0 * worst_ratio);
    }
    // the slowest ones at level 1
    std::vector<size_t> order(sources.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ms[0][a] > ms[0][b]; });
    std::printf("\nslowest at level 1 (ms / instructions):\n");
    for (size_t j = 0; j < std::min<size_t>(10, order.size()); j++) {
        size_t i = order[j];
        std::printf("%-30s %6zu bytes", sources[i].name.c_str(), sources[i].text.size());
        for (size_t k = 0; k < std::size(kSettings); k++)
            std::printf("  %s %6.1f/%4u", kSettings[k].name, ms[k][i], ins[k][i]);
        std::printf("\n");
    }
    return 0;
}
