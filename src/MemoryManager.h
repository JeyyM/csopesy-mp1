#pragma once

// MemoryManager.h
//
// First-fit flat memory allocator for the CSOPESY OS emulator.
//
// Manages a single contiguous physical address space of `totalMemory_` bytes.
// Every allocation is exactly `memPerProc_` bytes (fixed-size).
//
// NOT internally thread-safe — all public methods must be called while the
// caller holds Scheduler::mutex_.  This avoids a second mutex and the
// associated lock-ordering hazards.
//
// Address layout (example: totalMemory=16384, memPerProc=4096):
//
//   0         4096       8192       12288      16384
//   |--P1-----|--P3------|  (free)  |--P7------|
//
// First-fit: scan from address 0 and return the first gap >= memPerProc_.
//
// External fragmentation: for this fixed-size scheme we report the total
// free bytes (= totalMemory - usedBytes), which matches the spec sample.

#include <cstdint>
#include <string>
#include <vector>

class MemoryManager {
public:
    // One contiguous block currently held by a process.
    struct Allocation {
        std::string processName;
        int base;    // inclusive start address
        int size;    // always == memPerProc_
    };

    // Call once (or on scheduler restart) to set the memory layout.
    // Clears any previous allocations.
    void configure(uint32_t totalMemory, uint32_t memPerProc);

    // True after configure() was called with nonzero values.
    bool isConfigured() const;

    // First-fit allocation.
    // Returns the base address on success, or -1 if no contiguous block
    // of memPerProc_ bytes is available.
    int allocate(const std::string& processName);

    // Frees the allocation owned by processName.  No-op if not found.
    void release(const std::string& processName);

    // Snapshot sorted by base address descending (for top-down printout).
    std::vector<Allocation> snapshotDescending() const;

    // Total free bytes that are not currently allocated.
    // Matches the "Total external fragmentation in KB" field in the spec sample.
    uint32_t externalFragmentationBytes() const;

    int      allocatedCount() const;
    uint32_t totalMemory()   const { return totalMemory_; }
    uint32_t memPerProc()    const { return memPerProc_; }

private:
    uint32_t totalMemory_ = 0;
    uint32_t memPerProc_  = 0;

    // Kept sorted ascending by base address at all times.
    std::vector<Allocation> allocations_;
};
