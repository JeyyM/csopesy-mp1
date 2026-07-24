// MemoryManager.cpp
//
// Implements the demand-paging memory manager (see MemoryManager.h).
//
// Worked example (maxOverallMem=1024, memPerFrame=256 -> 4 frames):
//
//   accessPage("P1", 0) -> fault, free frame 0 -> load. pagedIn=1. returns 0.
//   accessPage("P1", 1) -> fault, free frame 1 -> load. pagedIn=2. returns 1.
//   accessPage("P2", 0) -> fault, free frame 2 -> load. pagedIn=3. returns 2.
//   accessPage("P2", 1) -> fault, free frame 3 -> load. pagedIn=4. returns 3.
//   accessPage("P3", 0) -> fault, NO free frame -> evict FIFO victim (P1,pg0,
//                          frame 0) to backing (pagedOut=1), load into frame 0
//                          (pagedIn=5). returns 0.
//   accessPage("P1", 0) -> was evicted -> fault again, evict next FIFO victim,
//                          reload. Demonstrates thrashing under pressure.

#include "MemoryManager.h"

#include <fstream>

void MemoryManager::configure(uint32_t maxOverallMem, uint32_t memPerFrame) {
    maxOverallMem_ = maxOverallMem;
    memPerFrame_ = memPerFrame;
    frames_.clear();
    fifo_.clear();
    pageTables_.clear();
    pagedIn_ = 0;
    pagedOut_ = 0;

    if (isConfigured()) {
        frames_.assign(totalFrames(), Frame{});
    }
}

bool MemoryManager::isConfigured() const {
    return maxOverallMem_ > 0 && memPerFrame_ > 0 && maxOverallMem_ >= memPerFrame_;
}

uint32_t MemoryManager::totalFrames() const {
    if (memPerFrame_ == 0) {
        return 0;
    }
    return maxOverallMem_ / memPerFrame_;
}

// Returns the first unoccupied frame index, or -1 if all are in use.
int MemoryManager::findFreeFrame() const {
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        if (!frames_[i].occupied) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// FIFO eviction: takes the oldest occupied frame, marks its page swapped-out
// (present=false, inBacking=true), frees the frame, and returns its index.
int MemoryManager::evictOneFrame() {
    while (!fifo_.empty()) {
        const int frame = fifo_.front();
        fifo_.pop_front();
        if (frame < 0 || frame >= static_cast<int>(frames_.size())) {
            continue;
        }
        if (!frames_[frame].occupied) {
            continue;  // stale fifo entry (frame already freed)
        }

        // Mark the victim page as evicted to the backing store.
        auto ptIt = pageTables_.find(frames_[frame].proc);
        if (ptIt != pageTables_.end()) {
            auto entryIt = ptIt->second.find(frames_[frame].page);
            if (entryIt != ptIt->second.end()) {
                entryIt->second.present = false;
                entryIt->second.frame = -1;
                entryIt->second.inBacking = true;
            }
        }

        frames_[frame].occupied = false;
        frames_[frame].proc.clear();
        frames_[frame].page = -1;
        ++pagedOut_;
        return frame;
    }
    return -1;
}

int MemoryManager::accessPage(const std::string& proc, int page) {
    if (!isConfigured()) {
        return -1;
    }

    PageEntry& entry = pageTables_[proc][page];
    if (entry.present) {
        return entry.frame;  // hit — no fault
    }

    // Page fault: obtain a frame (free one, or evict a FIFO victim).
    bool backingChanged = false;
    int frame = findFreeFrame();
    if (frame < 0) {
        frame = evictOneFrame();
        backingChanged = true;  // a victim was written out
        if (frame < 0) {
            return -1;  // no frames at all (should not happen when configured)
        }
    }

    frames_[frame].occupied = true;
    frames_[frame].proc = proc;
    frames_[frame].page = page;
    fifo_.push_back(frame);

    if (entry.inBacking) {
        backingChanged = true;  // page returns from the backing store
    }
    entry.present = true;
    entry.frame = frame;
    entry.inBacking = false;
    ++pagedIn_;

    if (backingChanged) {
        writeBackingStoreFile();
    }
    return frame;
}

void MemoryManager::releaseProcess(const std::string& proc) {
    auto ptIt = pageTables_.find(proc);
    if (ptIt == pageTables_.end()) {
        return;
    }

    // Free all frames this process occupies.
    for (auto& frame : frames_) {
        if (frame.occupied && frame.proc == proc) {
            frame.occupied = false;
            frame.proc.clear();
            frame.page = -1;
        }
    }

    // Drop any now-stale FIFO entries (frames that are no longer occupied).
    std::deque<int> kept;
    for (int frame : fifo_) {
        if (frame >= 0 && frame < static_cast<int>(frames_.size()) &&
            frames_[frame].occupied) {
            kept.push_back(frame);
        }
    }
    fifo_.swap(kept);

    pageTables_.erase(ptIt);
    writeBackingStoreFile();
}

uint32_t MemoryManager::usedFrames() const {
    uint32_t count = 0;
    for (const auto& frame : frames_) {
        if (frame.occupied) {
            ++count;
        }
    }
    return count;
}

uint32_t MemoryManager::usedMemoryBytes() const {
    return usedFrames() * memPerFrame_;
}

uint32_t MemoryManager::freeMemoryBytes() const {
    return maxOverallMem_ - usedMemoryBytes();
}

uint32_t MemoryManager::residentBytes(const std::string& proc) const {
    uint32_t frames = 0;
    for (const auto& frame : frames_) {
        if (frame.occupied && frame.proc == proc) {
            ++frames;
        }
    }
    return frames * memPerFrame_;
}

// Writes csopesy-backing-store.txt: one line per page currently swapped out.
void MemoryManager::writeBackingStoreFile() const {
    std::ofstream file("csopesy-backing-store.txt", std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file << "CSOPESY Backing Store\n";
    file << "Frame size (bytes): " << memPerFrame_ << "\n";
    file << "----------------------------------------\n";
    file << "Process            Page\n";

    std::size_t count = 0;
    for (const auto& procEntry : pageTables_) {
        for (const auto& pageEntry : procEntry.second) {
            if (pageEntry.second.inBacking && !pageEntry.second.present) {
                file << procEntry.first;
                if (procEntry.first.size() < 18) {
                    file << std::string(18 - procEntry.first.size(), ' ');
                } else {
                    file << " ";
                }
                file << pageEntry.first << "\n";
                ++count;
            }
        }
    }

    file << "----------------------------------------\n";
    file << "Pages in backing store: " << count << "\n";
}
