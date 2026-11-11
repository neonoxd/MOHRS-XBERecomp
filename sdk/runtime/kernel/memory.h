#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace mem {

constexpr uint32_t kPage = 0x1000;
constexpr uint32_t kUserLow = 0x00010000;
constexpr uint32_t kUserHigh = 0x7FFE0000;
constexpr uint32_t kContiguousBase = 0x80000000;  // guest VA = kContiguousBase | physical
// a development kit's 128 MB: a title sizes its own pools for a retail console's 64 MB and uses the rest only when
// a game's hooks give it more (MOHRS: the main heap grown for weapons from other levels). The XDK's D3D masks
// physical addresses to 28 bits, so up to 256 MB would do.
constexpr uint32_t kPhysicalSize = 128u << 20;
constexpr uint32_t kWriteCombineBase = 0xF0000000;
constexpr uint32_t kKernelDataBase = 0x80000000;   // first physical megabyte holds kernel variables
constexpr uint32_t kKernelDataSize = 0x00100000;
constexpr uint32_t kSystemBase = 0xB0000000;       // pool, system memory, thread blocks
constexpr uint32_t kSystemSize = 0x10000000;
constexpr uint32_t kStackBase = 0xD0000000;
constexpr uint32_t kStackSize = 0x10000000;

// NT allocation flags
constexpr uint32_t kMemCommit = 0x1000, kMemReserve = 0x2000, kMemDecommit = 0x4000, kMemRelease = 0x8000,
                   kMemTopDown = 0x100000, kMemNoZero = 0x800000;

bool Init();
uint8_t* Base();
bool Commit(uint32_t va, uint32_t size);
void Decommit(uint32_t va, uint32_t size);
bool IsCommitted(uint32_t va);
// every page of [va, va + size) is readable guest memory; a header the title freed can point anywhere
bool IsCommittedRange(uint32_t va, uint32_t size);
// every committed guest range, the physical memory first (crash dumps take them)
std::vector<std::pair<uint32_t, uint32_t>> CommittedRanges();

// user virtual space, NTSTATUS results
uint32_t AllocateVirtual(uint32_t& base, uint32_t& size, uint32_t type, uint32_t protect);
uint32_t FreeVirtual(uint32_t& base, uint32_t& size, uint32_t type);
struct RegionInfo {
    uint32_t base, alloc_base, alloc_protect, size, state, protect, type;
};
bool QueryVirtual(uint32_t addr, RegionInfo& out);
void ReserveImage(uint32_t base, uint32_t size);

uint32_t AllocContiguous(uint32_t size, uint32_t lowest, uint32_t highest, uint32_t align);
void FreeContiguous(uint32_t va);
uint32_t AllocationSize(uint32_t va);  // any allocator, 0 if unknown

// a guest thread's blocks (KPCR, ETHREAD) at a fixed place by its index, the top of system memory, so other system
// allocations never move them: a quick save loads only where every thread's blocks are where they were. 0 past the end.
constexpr uint32_t kThreadBlockSize = 0x800;
constexpr uint32_t kThreadBlocksSize = 0x100000;
constexpr uint32_t kThreadBlocks = kSystemBase + kSystemSize - kThreadBlocksSize;
uint32_t AllocThreadBlocks(uint32_t index);

uint32_t AllocSystem(uint32_t size, uint32_t align = 16);
void FreeSystem(uint32_t va);

uint32_t AllocStack(uint32_t size);  // returns the top, guard below the bottom
void FreeStack(uint32_t top);

uint32_t PhysicalFromVirtual(uint32_t va);
uint32_t VirtualFromPhysical(uint32_t pa);

struct Stats {
    uint32_t total_pages, available_pages, virtual_committed, contiguous_used, system_used;
};
Stats Query();

}  // namespace mem
