// tools\dev.bat clang++ -std=c++20 -O2 -Isdk/runtime tests/vertex_packing_test.cpp sdk/runtime/gpu/vertex.cpp -o re/vertex_packing_test.exe
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgiformat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "gpu/vertex.h"

namespace kernel { uint8_t* g_base; }
namespace mem { uint32_t VirtualFromPhysical(uint32_t p) { return p; } }
namespace logx { void Write(Level, const char*, ...) {} }

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static void Put(uint32_t at, const void* p, size_t n) { std::memcpy(kernel::g_base + at, p, n); }

static void Stream(gpu::State& s, int slot, int reg, uint32_t type, uint32_t data, uint32_t stride) {
    uint32_t header = 64 + uint32_t(slot) * 16;
    Put(header + 4, &data, 4);
    s.streams[slot] = {header, stride};
    s.vs->inputs[reg] = {slot, 0, type};
}

static void Position(const gpu::Assembled& a, uint32_t vertex, int16_t x, int16_t y, int16_t z) {
    const auto& e = a.elements[0];
    const auto& b = a.buffers[e.buffer];
    Check(e.format == DXGI_FORMAT_R16G16B16A16_SINT, "positions must use signed SHORT4 fetches");
    Check((a.decode & 15) == gpu::kDecodeIntW1, "SHORT3 shader must supply w = 1");
    Check(b.stride == 8 && b.bytes >= (vertex + 1) * b.stride, "padded position fetch must be in bounds");
    int16_t p[4];
    std::memcpy(p, b.src + size_t(vertex) * b.stride + e.offset, sizeof(p));
    Check(p[0] == x && p[1] == y && p[2] == z && p[3] == 0, "signed coordinates and padding must survive");
}

int main() {
    // The last six-byte vertex ends at a protected page: an eight-byte guest read would fault.
    kernel::g_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16384, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(kernel::g_base != nullptr, "guest memory allocation failed");
    DWORD old;
    Check(VirtualProtect(kernel::g_base + 12288, 4096, PAGE_NOACCESS, &old), "guard page failed");
    gpu::State s;
    s.vs_handle = 1;
    s.vs = std::make_shared<gpu::VertexShader>();
    const int16_t positions[][3] = {{-123, 45, 32767}, {-32768, 2, -3}, {7, 8, 9}, {10, 11, 12}};
    Put(512, positions, sizeof(positions));
    const uint8_t colours[][3] = {{0, 127, 255}, {255, 0, 63}, {12, 34, 56}, {78, 90, 123}};
    Put(1024, colours, sizeof(colours));
    const float uv[][2] = {{0, 1}, {0.25f, 0.75f}, {1, 0}, {0.5f, 0.5f}};
    Put(1536, uv, sizeof(uv));
    Stream(s, 0, 0, 0x35, 512, 6);
    Stream(s, 1, 3, 0x34, 1024, 3);
    Stream(s, 2, 9, 0x22, 1536, 8);
    gpu::Assembled a;
    Check(gpu::Assemble(s, 6, 1, 3, nullptr, a), "nonindexed triangle strip must assemble");
    Check(a.vertices.empty() && a.strip && a.draw_count == 3, "packed streams must keep native topology");
    Position(a, 0, -32768, 2, -3);
    Position(a, 2, 10, 11, 12);
    const auto& cb = a.buffers[a.elements[3].buffer];
    Check(cb.stride == 4 && cb.src[0] == 255 && cb.src[1] == 0 && cb.src[2] == 63 && cb.src[3] == 0,
          "three-byte colour must be padded independently");
    Check(((a.decode >> 12) & 15) == gpu::kDecodeW1, "three-component colour must retain alpha = 1");
    const auto& tb = a.buffers[a.elements[9].buffer];
    Check(tb.src == kernel::g_base + 1544 && tb.stride == 8, "aligned streams should stay direct");

    const uint16_t dense[] = {2, 0, 1};
    s.base_vertex = 1;
    Check(gpu::Assemble(s, 5, 0, 3, reinterpret_cast<const uint8_t*>(dense), a), "indexed base-vertex draw failed");
    Check(a.index16 && a.base_vertex == 0 && a.index_src == dense, "dense guest indices must remain unchanged");
    Position(a, 0, -32768, 2, -3);
    Position(a, 2, 10, 11, 12);

    s.base_vertex = 0;
    Check(gpu::Assemble(s, 8, 0, 4, nullptr, a), "packed quad failed");
    const uint32_t quad[] = {0, 1, 2, 0, 2, 3};
    Check(a.index_count == 6 && !a.index16 && !std::memcmp(a.index_src, quad, sizeof(quad)),
          "quad triangulation must preserve winding and vertex order");
    Position(a, 3, 10, 11, 12);

    Put(512 + 1200 * 6, positions[3], 6);
    Put(1024 + 1200 * 3, colours[3], 3);
    Put(1536 + 1200 * 8, uv[3], 8);
    const uint16_t sparse[] = {1200, 0, 2};
    Check(gpu::Assemble(s, 5, 0, 3, reinterpret_cast<const uint8_t*>(sparse), a), "sparse draw failed");
    Check(!a.index_count && a.draw_count == 3, "sparse draws must gather in draw order");
    Position(a, 0, 10, 11, 12);
    Position(a, 1, -123, 45, 32767);
    Position(a, 2, 7, 8, 9);

    s.vs->inputs[3] = {};
    s.vs->inputs[9] = {};
    Put(12288 - 6, positions[0], 6);
    Stream(s, 0, 0, 0x35, 12288 - 6, 6);
    Check(gpu::Assemble(s, 1, 0, 1, nullptr, a), "guarded final vertex failed");
    Position(a, 0, -123, 45, 32767);
    VirtualFree(kernel::g_base, 0, MEM_RELEASE);
    std::puts("Packed vertex checks passed");
}
