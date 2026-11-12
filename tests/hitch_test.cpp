// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/hitch_test.cpp
//     sdk/runtime/core/hitch.cpp sdk/runtime/core/log.cpp -luser32 -o re/hitch_test.exe
// re\hitch_test.exe
// The hitch recorder (XBE_HITCH) with the kernel faked: steady 2 ms frames, then one frame with a 15 ms shader
// compile and a file read. The slow frame is logged with both, the guest thread's stack is sampled while the present
// is overdue, and Mark writes the file with the slow frame, the events and the frame intervals.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/hitch.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

// the fake guest: 64 KB of memory, one thread whose stack holds two code addresses
static std::vector<uint8_t> g_memory(0x10000);
static std::vector<kernel::ThreadRef> g_threads;
static X86Context g_ctx{};

namespace kernel {
uint8_t* g_base = nullptr;
Thread* Current() { return nullptr; }
std::vector<ThreadRef> Threads() { return g_threads; }
}  // namespace kernel
namespace mem {
bool IsCommitted(uint32_t va) { return va < g_memory.size(); }
}  // namespace mem
namespace dispatch {
bool InCode(uint32_t va) { return va >= 0x10000 && va < 0x200000; }
}  // namespace dispatch

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) g_failures++;
}

static void Spin(double ms) {
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(ms);
    while (std::chrono::steady_clock::now() < until) {
    }
}

static std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main() {
    namespace fs = std::filesystem;
    _putenv("XBE_HITCH=1");
    fs::path dir = fs::path("re") / "hitch_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    logx::Init(dir.string(), "hitch", false);

    kernel::g_base = g_memory.data();
    g_ctx.esp = 0x8000;
    kernel::W32(0x8000, 0x1234);       // not code
    kernel::W32(0x8004, 0x0012A9D0);   // code
    kernel::W32(0x8010, 0x0019F00C);   // code
    auto t = std::make_shared<kernel::Thread>();
    t->id = 7;
    t->ctx = &g_ctx;
    g_threads.push_back(t);

    Check(hitch::On(), "XBE_HITCH switches it on");
    hitch::NameThread("render");
    for (int i = 0; i < 130; i++) {
        Spin(2);
        hitch::TitlePresent();
        hitch::Present();
    }
    {
        hitch::Scope s(hitch::Kind::Shader);
        s.Text("ps_5_0 test");
        Spin(15);
    }
    {
        hitch::Scope r(hitch::Kind::FileRead);
        r.Text("d:\\sound\\test.abk, 4096 bytes at 0");
    }
    hitch::Present();
    for (int i = 0; i < 20; i++) {
        Spin(2);
        hitch::Present();
    }
    hitch::Mark();
    logx::Flush();

    std::string log = ReadAll(logx::Path());
    Check(log.find("hitch: 1") != std::string::npos || log.find("ms frame at") != std::string::npos,
          "the slow frame is logged");
    Check(log.find("shader 1x") != std::string::npos, "its summary counts the shader");
    Check(log.find("read 1x") != std::string::npos, "and the file read");
    Check(log.find("ps_5_0 test") != std::string::npos, "the shader is listed by name");
    Check(log.find("guest 7") != std::string::npos && log.find("0012A9D0 0019F00C") != std::string::npos,
          "the guest thread's stack was sampled while the present was overdue");
    size_t frames = 0;
    for (size_t at = 0; (at = log.find("ms frame at", at)) != std::string::npos; at++) frames++;
    Check(frames == 1, "steady frames are not logged");

    fs::path mark;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().rfind("hitch_", 0) == 0 && e.path().extension() == ".txt") mark = e.path();
    Check(!mark.empty(), "Mark writes a hitch_<time>.txt beside the log");
    std::string m = mark.empty() ? "" : ReadAll(mark);
    Check(m.find("== slow frames ==") != std::string::npos && m.find("shader 1x") != std::string::npos,
          "the file describes the slow frame");
    Check(m.find("d:\\sound\\test.abk") != std::string::npos, "the file lists the events");
    Check(m.find("== frame intervals") != std::string::npos && m.find("== title frame intervals") != std::string::npos,
          "the file lists the frame intervals");
    if (g_failures) {
        std::printf("--- log ---\n%s\n--- mark ---\n%s\n", log.c_str(), m.c_str());
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
