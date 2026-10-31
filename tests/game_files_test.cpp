// Host test with tiny synthetic XDVDFS images; no game files, game window or message boxes.
// tools\dev.bat clang++ -std=c++20 -Isdk/runtime -Igenerated/game tests/game_files_test.cpp
//     sdk/runtime/kernel/disc.cpp sdk/runtime/kernel/crypto.cpp -o re/game_files_test.exe
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>

#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "image_symbols.h"

namespace {
std::filesystem::path exe_dir;
int messages = 0, reply = IDNO;
UINT message_flags = 0;
int TestMessageBox(HWND, LPCWSTR, LPCWSTR, UINT flags) {
    ++messages;
    message_flags = flags;
    return reply;
}
}

// Exercise the production finder while keeping its unknown-release confirmation headless.
#define MessageBoxW TestMessageBox
#include "../sdk/runtime/app/game_files.cpp"
#undef MessageBoxW

extern const char kImageSha1[] = "01bada152405b958af7e5fe4c072d1cee851aa3b";  // "baseline"
const app::GameInfo app::kGame = {
    .short_title = L"game files test",
    .missing = L"missing fixture",
    .supported_hashes = {
        {IMAGE_XBE, "18bc5956dbf1cc6c9a5baeb624c9e7e472a04c2e"},  // "USA"
        {IMAGE_XBE, "576347ec826f38428d8c8a6f8ec4acb2bceab911"},  // "Europe"
        {std::string_view(IMAGE_XBE) == "default.xbe" ? "moh3rdc.xbe" : "default.xbe",
         "a88a3dbaba203a8299bad3c2037aaaf2ea2f55c1"},  // "other-image"
    },
};
std::filesystem::path app::ExeDir() { return exe_dir; }
void logx::Write(Level, const char*, ...) {}

namespace {
namespace fs = std::filesystem;

void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "%s\n", what); std::exit(1); }
}

fs::path Begin(const char* name) {
    kernel::MountDisc(nullptr);
    messages = 0;
    reply = IDNO;
    message_flags = 0;
    exe_dir = fs::absolute(fs::path("re/game-files-test") / std::to_string(GetCurrentProcessId()) / name / "bin");
    fs::path assets = exe_dir.parent_path() / "assets";
    fs::create_directories(assets);
    return assets;
}

void WriteFile(const fs::path& path, std::string_view data) {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), data.size());
    Check(out.good(), "write loose fixture");
}

void WriteIso(const fs::path& path, std::string_view data) {
    constexpr uint32_t sector = 2048;
    std::vector<uint8_t> bytes(35 * sector);
    auto put32 = [&](size_t at, uint32_t value) { std::memcpy(&bytes[at], &value, 4); };
    constexpr char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(&bytes[32 * sector], magic, sizeof(magic) - 1);
    std::memcpy(&bytes[32 * sector + 0x7EC], magic, sizeof(magic) - 1);
    put32(32 * sector + 0x14, 33);
    std::string_view name = IMAGE_XBE;
    put32(32 * sector + 0x18, uint32_t((14 + name.size() + 3) & ~size_t(3)));
    put32(33 * sector + 4, 34);
    put32(33 * sector + 8, uint32_t(data.size()));
    bytes[33 * sector + 13] = uint8_t(name.size());
    std::memcpy(&bytes[33 * sector + 14], name.data(), name.size());
    std::memcpy(&bytes[34 * sector], data.data(), data.size());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(out.good(), "write disc fixture");
}

void Expect(std::string_view data, const char* disc = nullptr) {
    app::GameFiles game;
    Check(app::FindGame(game), "supported fixture accepted");
    Check(std::string_view(reinterpret_cast<const char*>(game.xbe.data()), game.xbe.size()) == data, "correct XBE selected");
    Check(messages == 0, "supported release never prompts");
    const auto* mounted = kernel::MountedDisc();
    Check(disc ? mounted && mounted->Path().filename() == disc : !mounted, "selected XBE and disc stay paired");
}
}

int main() {
    auto assets = Begin("baseline");
    WriteIso(assets / "baseline.iso", "baseline");
    Expect("baseline", "baseline.iso");

    assets = Begin("usa");
    WriteIso(assets / "usa.iso", "USA");
    Expect("USA", "usa.iso");

    assets = Begin("europe");
    WriteIso(assets / "europe.iso", "Europe");
    Expect("Europe", "europe.iso");

    assets = Begin("all");
    WriteIso(assets / "a-europe.iso", "Europe");
    WriteIso(assets / "b-usa.iso", "USA");
    WriteIso(assets / "z-baseline.iso", "baseline");
    Expect("baseline", "z-baseline.iso");

    assets = Begin("unknown-before-supported");
    WriteIso(assets / "a-unknown.iso", "unknown");
    WriteIso(assets / "b-usa.iso", "USA");
    Expect("USA", "b-usa.iso");

    assets = Begin("loose-supported");
    WriteFile(assets / IMAGE_XBE, "USA");
    WriteIso(assets / "europe.iso", "Europe");
    Expect("USA");

    assets = Begin("disc-exact-before-loose-supported");
    WriteFile(assets / IMAGE_XBE, "USA");
    WriteIso(assets / "baseline.iso", "baseline");
    Expect("baseline", "baseline.iso");

    assets = Begin("wrong-image-hash");
    WriteIso(assets / "wrong.iso", "other-image");
    app::GameFiles game;
    Check(!app::FindGame(game) && messages == 1, "a supported hash for another XBE still prompts");

    assets = Begin("unknown");
    WriteIso(assets / "unknown.iso", "unknown");
    Check(!app::FindGame(game) && messages == 1 && (message_flags & MB_ICONWARNING), "unknown release retains warning and rejection");
    reply = IDYES;
    Check(app::FindGame(game) && messages == 2 && kernel::MountedDisc(), "unknown release can still be explicitly accepted");
    kernel::MountDisc(nullptr);
    std::puts("game files: supported releases, exact-hash priority, disc pairing and unknown-release checks passed");
}
