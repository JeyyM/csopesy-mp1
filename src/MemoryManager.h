#pragma once

// MemoryManager.h
//
// Demand-paging memory manager for the CSOPESY OS emulator (MCO2).
//
// Physical memory is divided into fixed-size frames (memPerFrame bytes each);
// total frames = maxOverallMem / memPerFrame. Each process has its own page
// table. Pages are loaded into frames ON DEMAND: the first time a process
// touches a page, a page fault brings it in. When no frame is free, a FIFO
// page-replacement policy evicts a victim page to the backing store
// (csopesy-backing-store.txt) and reuses its frame.
//
// Pages belonging to a process need NOT occupy contiguous frames.
//
// NOT internally thread-safe — every public method must be called while the
// caller (Scheduler) holds Scheduler::mutex_. This avoids a second lock and
// the associated lock-ordering hazards.

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class MemoryManager {
public:
    // (Re)configure the physical memory layout. Clears all state.
    // memPerFrame doubles as the page size. Pass zeros to disable.
    void configure(uint32_t maxOverallMem, uint32_t memPerFrame);

    // True once configured with nonzero sizes.
    bool isConfigured() const;

    uint32_t frameSize() const { return memPerFrame_; }
    uint32_t totalFrames() const;
    uint32_t totalMemory() const { return maxOverallMem_; }

    // Demand-paging access: ensure (proc, page) is resident and return its
    // frame index. On a page fault, loads the page — evicting a FIFO victim
    // to the backing store first if every frame is occupied. Updates the
    // paged-in / paged-out counters. Returns -1 only when not configured.
    int accessPage(const std::string& proc, int page);

    // Release every frame, page-table entry, and backing entry owned by proc.
    // Called when a process finishes or is terminated by a violation.
    void releaseProcess(const std::string& proc);

    // ── Statistics for process-smi / vmstat ─────────────────────────────────
    uint64_t numPagedIn() const { return pagedIn_; }
    uint64_t numPagedOut() const { return pagedOut_; }
    uint32_t usedFrames() const;
    uint32_t usedMemoryBytes() const;
    uint32_t freeMemoryBytes() const;

    // Resident bytes held by one process (its occupied frames * frameSize).
    uint32_t residentBytes(const std::string& proc) const;

    // Overwrites csopesy-backing-store.txt with the pages currently swapped out.
    void writeBackingStoreFile() const;

private:
    struct Frame {
        bool occupied = false;
        std::string proc;
        int page = -1;
    };
    struct PageEntry {
        bool present = false;   // currently in a physical frame
        int frame = -1;         // frame index when present
        bool inBacking = false; // has been evicted to the backing store
    };

    int findFreeFrame() const;
    int evictOneFrame();  // FIFO victim; returns the freed frame index

    uint32_t maxOverallMem_ = 0;
    uint32_t memPerFrame_ = 0;

    std::vector<Frame> frames_;
    std::deque<int> fifo_;  // occupied frame indices in load order (oldest front)
    std::unordered_map<std::string, std::unordered_map<int, PageEntry>> pageTables_;

    uint64_t pagedIn_ = 0;
    uint64_t pagedOut_ = 0;
};
