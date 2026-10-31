// Headless kernel test; synthetic buffers only, no game launch or player save access.
// tools\dev.bat clang++ -std=c++20 -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime tests/disc_security_test.cpp
//     sdk/runtime/kernel/disc.cpp sdk/runtime/kernel/crypto.cpp -o re/disc_security_test.exe
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "kernel/kernel.h"
#include "xbe/xbe.h"

namespace {
std::map<uint32_t, kernel::KernelFn> exports;
std::array<uint8_t, 0x10000> memory{};
}
namespace kernel {
uint8_t* g_base = memory.data();
const xbe::Image* g_image = nullptr;
void RegisterExport(uint32_t ordinal, const char*, KernelFn fn) { exports[ordinal] = fn; }
uint32_t NewHandle(ObjectRef) { return 0; }
ObjectRef FromHandle(uint32_t) { return nullptr; }
Thread* Current() { return nullptr; }
void QueueApc(Thread&, uint32_t, uint32_t, uint32_t, uint32_t) {}
int32_t SetEvent(Object&) { return 0; }
int32_t ResetEvent(Object&) { return 0; }
}
void logx::Write(Level, const char*, ...) {}

#include "../sdk/runtime/kernel/files.cpp"

namespace {
constexpr uint32_t sptd = 0x1000, buffer = 0x2000, iosb = 0x4000;
constexpr std::array<uint8_t, 12> authentication{0xAD, 0, 0xFF, 2, 0xFD, 0xFF, 0xFE, 0, 6, 0x64, 0, 0xC0};

void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "%s\n", what); std::exit(1); }
}

void Query(std::array<uint8_t, 12> cdb, uint32_t bytes, uint32_t input_bytes = 0x2C) {
    std::fill(memory.begin(), memory.end(), 0xCC);
    std::memset(memory.data() + sptd, 0, 0x2C);
    kernel::W32(sptd + 0xC, bytes);
    kernel::W32(sptd + 0x14, buffer);
    std::memcpy(memory.data() + sptd + 0x1C, cdb.data(), cdb.size());
    X86Context ctx{};
    ctx.esp = 0x8000;
    uint32_t args[] = {0, 0, 0, 0, iosb, 0x4D014, sptd, input_bytes, 0, 0};
    std::memcpy(memory.data() + ctx.esp + 4, args, sizeof(args));
    exports.at(196)(ctx, memory.data());
    Check(ctx.eax == 0 && kernel::R32(iosb) == 0, "IOCTL completes successfully");
    Check(memory[buffer - 1] == 0xCC && memory[buffer + bytes] == 0xCC, "reply stays within the transfer buffer");
}
}

int main() {
    xbe::Image image;
    image.base = 0x10000;
    image.header_size = 0x1000;
    image.cert = image.base + 0x184;
    image.file.resize(image.header_size);
    uint32_t timestamp = 0x12345678;
    std::memcpy(image.file.data() + 0x188, &timestamp, sizeof(timestamp));
    kernel::g_image = &image;

    Query({0x5A, 0, 0x3E}, 0x1C);
    Check(memory[buffer + 10] == 1 && memory[buffer + 11] == 1 && memory[buffer + 12] == 1,
          "MODE SENSE still reports authenticated media");

    Query(authentication, 0x664);
    Check(memory[buffer] == 6 && memory[buffer + 1] == 0x62, "READ DVD STRUCTURE length is big endian");
    Check(kernel::R32(buffer + 0x4AB) == timestamp, "authentication matches the selected XBE certificate timestamp");
    // Independent SHA-1 fixture for LE32(0x4CB) || payload, with timestamp 0x12345678 at reply+0x4AB.
    constexpr uint8_t digest[] = {0x06, 0x21, 0xB5, 0xB8, 0, 0, 0xA0, 0xA6, 0x41, 0x3C,
                                  0x46, 0x99, 0x0D, 0x17, 0xE7, 0x82, 0x21, 0x5D, 0x04, 0x2C};
    Check(std::memcmp(memory.data() + buffer + 0x4CF, digest, sizeof(digest)) == 0,
          "authentication digest passes the XAPI payload check");

    timestamp = 0x87654321;
    std::memcpy(image.file.data() + 0x188, &timestamp, sizeof(timestamp));
    Query(authentication, 0x664);
    Check(kernel::R32(buffer + 0x4AB) == timestamp, "a different release gets its own certificate timestamp");

    Query(authentication, 0x663);
    Check(memory[buffer] == 0xCC, "short authentication transfers are left untouched");
    Query(authentication, 0x664, 0x2B);
    Check(memory[buffer] == 0xCC, "short SCSI command headers are ignored");
    auto other = authentication;
    other[6] = 0;
    Query(other, 0x664);
    Check(memory[buffer] == 0xCC, "other DVD structure requests are not mistaken for authentication");
    kernel::g_image = nullptr;
    Query(authentication, 0x664);
    Check(memory[buffer] == 0xCC, "authentication needs a loaded certificate");
    std::puts("disc security: media page, retail authentication, certificate/digest and transfer bounds passed");
}
