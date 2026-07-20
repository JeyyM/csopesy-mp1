// MemoryManager.cpp
//
// Implements the first-fit flat memory allocator.
//
// Key invariant: allocations_ is always sorted ascending by base address.
// This lets allocate() scan gaps in a single linear pass.
//
// Worked example (totalMemory=16384, memPerProc=4096):
//
//   allocate("P1") → cursor=0, no blocks yet, append {P1, 0, 4096}. Returns 0.
//   allocate("P2") → cursor starts at 4096, append {P2, 4096, 4096}. Returns 4096.
//   allocate("P3") → cursor=8192, append. Returns 8192.
//   allocate("P4") → cursor=12288, append. Returns 12288.
//   allocate("P5") → cursor reaches 16384, 16384+4096 > 16384. Returns -1 (full).
//   release("P2")  → removes {P2, 4096, 4096}. Gap at [4096, 8192).
//   allocate("P5") → cursor=0, P1 at 0-4096, gap is 4096-0=0 before P1 → nope;
//                    after P1 cursor=4096, next block P3 at 8192,
//                    gap = 8192-4096=4096 >= 4096 → insert {P5, 4096, 4096}. Returns 4096.

#include "MemoryManager.h"

#include <algorithm>

void MemoryManager::configure(uint32_t totalMemory, uint32_t memPerProc) {
    totalMemory_ = totalMemory;
    memPerProc_  = memPerProc;
    allocations_.clear();
}

bool MemoryManager::isConfigured() const {
    return totalMemory_ > 0 && memPerProc_ > 0;
}

// Every process that calls this always asks for the same amount of memory. 
//To find it a spot, we walk through memory starting at address 0 
// and stop at the first empty gap that's big enough.
int MemoryManager::allocate(const std::string& processName) {
    if (!isConfigured()) {
        return -1;
    }

    // Walk through memory left to right. "cursor" is where we currently
    // are; for each process already in memory, check if there's enough
    // empty space between the cursor and that process to fit a new one.
    int cursor = 0;
    for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
        const int gapSize = it->base - cursor;
        if (gapSize >= static_cast<int>(memPerProc_)) {
            // Found a gap at [cursor, cursor+memPerProc_).
            // Insert before *it to maintain ascending sort.
            allocations_.insert(it, {processName, cursor, static_cast<int>(memPerProc_)});
            return cursor;
        }
        cursor = it->base + it->size;
    }

    // Check the space after the last block.
    if (static_cast<uint32_t>(cursor) + memPerProc_ <= totalMemory_) {
        allocations_.push_back({processName, cursor, static_cast<int>(memPerProc_)});
        return cursor;
    }

    // There's genuinely no room anywhere for this process right
    // now. We never kick another process out or save anything 
    // to disk to free up room. Whoever
    // called this function (the Scheduler) will see the -1 and simply
    // make the process wait its turn again.
    return -1;  // memory is full
}

//This is the only place a process's memory ever gets freed.
// It only ever gets called once a process has completely finished
// running — never while it's just paused waiting for another turn. So a
// process holds onto the exact same spot in memory for its whole
// lifetime, from the moment it starts until the moment it's done.
void MemoryManager::release(const std::string& processName) {
    allocations_.erase(
        std::remove_if(allocations_.begin(), allocations_.end(),
                       [&](const Allocation& a) { return a.processName == processName; }),
        allocations_.end());
}

// Returns everything currently in memory, ordered from the highest
// address down to the lowest
std::vector<MemoryManager::Allocation> MemoryManager::snapshotDescending() const {
    auto sorted = allocations_;
    std::sort(sorted.begin(), sorted.end(),
              [](const Allocation& a, const Allocation& b) { return a.base > b.base; });
    return sorted;
}

// Works out the "Total external fragmentation" number for the
// memory snapshot file. Figuring out how much is "used" is just
// multiply (number of processes) by (size of each one). Whatever's left
// over out of the total is unused wasted space which is the
// fragmentation number.
uint32_t MemoryManager::externalFragmentationBytes() const {
    const uint32_t used =
        static_cast<uint32_t>(allocations_.size()) * memPerProc_;
    return totalMemory_ > used ? totalMemory_ - used : 0u;
}

int MemoryManager::allocatedCount() const {
    return static_cast<int>(allocations_.size());
}
