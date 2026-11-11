// mem::IsCommittedRange: the guard that keeps the renderer off a freed texture header's garbage address
// tools\dev.bat clang++ -std=c++20 -O2 -Isdk/runtime tests/committed_range_test.cpp sdk/runtime/kernel/memory.cpp
//   sdk/runtime/core/log.cpp -o re/committed_range_test.exe
#include <cstdio>

#include "kernel/memory.h"

static int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                             \
        }                                                           \
    } while (0)

int main() {
    using namespace mem;
    if (!Init()) {
        std::printf("FAIL: no guest space\n");
        return 1;
    }

    // user memory: three pages committed
    const uint32_t user = 0x00200000;
    CHECK(!IsCommittedRange(user, 4));
    CHECK(Commit(user, 3 * kPage));
    CHECK(IsCommittedRange(user, 3 * kPage));
    CHECK(IsCommittedRange(user + 0x10, 20));
    CHECK(IsCommittedRange(user + kPage - 4, 8));       // across a page boundary, both committed
    CHECK(!IsCommittedRange(user + 3 * kPage - 4, 8));  // runs into the page after
    CHECK(!IsCommittedRange(user, 3 * kPage + 1));
    CHECK(IsCommittedRange(user, 0));                   // an empty range still needs its first byte
    CHECK(!IsCommittedRange(user + 3 * kPage, 0));
    Decommit(user + kPage, kPage);
    CHECK(!IsCommittedRange(user, 3 * kPage));          // a hole in the middle
    CHECK(IsCommittedRange(user, kPage));
    CHECK(IsCommittedRange(user + 2 * kPage, kPage));

    // the crash: a header's string bytes as a data address, nothing there
    CHECK(!IsCommittedRange(0x6E495F79, 0x268C80));

    // physical memory and its write-combined view are always there, to their last byte
    CHECK(IsCommittedRange(kContiguousBase, kPhysicalSize));
    CHECK(IsCommittedRange(0x83EB8140, 20));
    CHECK(IsCommittedRange(kWriteCombineBase + 0x1000, 0x10000));
    CHECK(!IsCommittedRange(kContiguousBase + kPhysicalSize - 8, 16));  // off the end of RAM
    CHECK(!IsCommittedRange(kWriteCombineBase + kPhysicalSize - 8, 16));

    // system memory (the render thread's header ring lives there)
    uint32_t ring = AllocSystem(1u << 20, 64);
    CHECK(ring != 0);
    CHECK(IsCommittedRange(ring, 1u << 20));
    CHECK(IsCommittedRange(ring + 0x3380, 20));

    // the end of the address space wraps to nothing
    CHECK(!IsCommittedRange(0xFFFFFFF0, 0x20));
    CHECK(!IsCommittedRange(0xFFFFFFFF, 2));

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("committed_range_test: all passed\n");
    return 0;
}
