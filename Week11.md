# Week 11 — Memory Manager Design (Paging Prep)

This document answers the Week 11 worksheet using the course `IMemoryAllocator` interface and our current CSOPESY emulator. It explains **what is meant to happen**, **how to test it**, and how the design connects to code that already works today (MCO2 flat allocator) versus what comes next (paging + backing store).

---

## Current status vs Week 11 goal

| Layer | Status in this repo | Week 11 expectation |
|---|---|---|
| Flat first-fit allocator (`MemoryManager`) | **Working** (MCO2) | Keep as one concrete `IMemoryAllocator` implementation |
| Memory stamps in `memory-stamps/` | **Working** | Equivalent of `visualizeMemory()` output |
| `IMemoryAllocator` abstract interface | **Design (this doc)** | Show how the memory manager plugs into the interface |
| Paging allocator + backing store | **Design (this doc)** | Required for the next paging milestone |
| `num-paged-in` / `num-paged-out` | **Design (this doc)** | Tallied in paging path only |

**Bottom line:** MCO2 already proves allocation, release, admission control, and stamp visualization. Week 11 designs the interface + paging path so the scheduler can swap flat vs paging without rewriting the rest of the emulator.

---

## Course interface (from the worksheet)

```cpp
class IMemoryAllocator {
public:
    enum MemoryAllocatorType {
        FLAT_MEMORY_ALLOCATOR,
        PAGING
    };

    virtual void* allocate(size_t size) = 0;
    virtual void deallocate(void* ptr) = 0;
    virtual String visualizeMemory() = 0;

protected:
    MemoryAllocatorType memoryAllocatorType;

    struct MemoryBlock {
        size_t start;
        size_t size;
        bool operator<(const MemoryBlock& other) const {
            return start < other.start;
        }
    };

    size_t maximumSize;
    size_t currentAllocatedSize;
};
```

---

## Q1 — How the memory manager uses `IMemoryAllocator`

### What is meant to happen

The scheduler should **not** talk to a concrete class name forever. It should own a pointer/reference to `IMemoryAllocator*` and call:

1. `allocate(size)` when a process is admitted
2. `deallocate(ptr)` when a process finishes (or a page is freed)
3. `visualizeMemory()` every `quantum-cycles` for memory stamps / `vmstat`

Two concrete classes then implement the interface:

```text
IMemoryAllocator
 ├── FlatMemoryAllocator   (type = FLAT_MEMORY_ALLOCATOR)
 └── PagingAllocator       (type = PAGING)
```

### Mapping to our current code

Today we have `MemoryManager` with:

- `allocate(processName)` → first-fit contiguous block
- `release(processName)` → free that block
- `snapshotDescending()` + stamp writer → visualization

That is already the **behavior** of a flat allocator. Week 11 wraps it as:

| Interface method | Flat implementation (ours) | Paging implementation (next) |
|---|---|---|
| `allocate(size)` | Find first free contiguous gap ≥ `mem-per-proc` | Allocate enough **frames** for `size`, fill PTEs |
| `deallocate(ptr)` | Free the contiguous block | Free frames / invalidate PTEs / maybe write backing store |
| `visualizeMemory()` | Stamp format (end → process blocks → start) | Same stamp idea, but show pages/frames + maybe page-in/out stats |

Suggested ownership in `Scheduler`:

```cpp
std::unique_ptr<IMemoryAllocator> memoryAllocator_;
// On start():
//   if paging enabled → make_unique<PagingAllocator>(...)
//   else              → make_unique<FlatMemoryAllocator>(...)
```

### How to test

1. Keep current MCO2 config and confirm flat path still produces stamps.
2. After the interface refactor, inject a fake allocator in a unit/integration harness and assert the scheduler still:
   - refuses admission when `allocate` fails
   - calls `deallocate` on Finished
   - calls `visualizeMemory` every quantum

---

## Q2 — Paging: how `allocate` and `deallocate` work

### What is meant to happen

Paging divides physical memory into fixed frames of size `mem-per-frame` (currently **16 bytes** in `config.txt`). A process needing `mem-per-proc` (**4096**) needs:

```text
pagesNeeded = ceil(mem-per-proc / mem-per-frame)
            = 4096 / 16
            = 256 pages/frames
```

Physical memory capacity:

```text
totalFrames = max-overall-mem / mem-per-frame
            = 16384 / 16
            = 1024 frames
```

So at most `1024 / 256 = 4` fully-resident processes — same capacity as MCO2, but allocation is **by frames**, not one contiguous 4096-byte slab.

#### `allocate(size)` (paging)

1. Compute `nPages = ceil(size / frameSize)`.
2. Look up / create the process page table.
3. For each virtual page `0 .. nPages-1`:
   - Find a free physical frame (free-frame list / bitmap).
   - If a free frame exists:
     - Map PTE: `VPN → PFN`, set Present=1.
     - Increment `currentAllocatedSize` / used-frame count.
     - Optionally increment `num-paged-in` if content was loaded from backing store.
   - If no free frame:
     - Choose a victim frame (e.g. FIFO / LRU / random — course choice).
     - Write victim page to backing store if dirty.
     - Increment `num-paged-out`.
     - Reassign the frame to the new page; increment `num-paged-in`.
4. Return a handle/`void*` that identifies this process allocation (see Q5).

#### `deallocate(ptr)` (paging)

1. Resolve `ptr` → process page table (see Q5).
2. For every PTE that is Present:
   - Mark the physical frame free.
   - Clear Present bit.
3. Remove / invalidate backing-store pages for that process (or leave them until process exits fully).
4. Decrease `currentAllocatedSize`.

### How this differs from our current flat `allocate` / `release`

| Flat (working now) | Paging (Week 11 design) |
|---|---|
| One contiguous block of 4096 bytes | Up to 256 frame mappings |
| Fail if no contiguous gap (`-1`) | Can page out a victim instead of failing (if backing store exists) |
| No disk | Backing store file/table for swapped pages |
| External fragmentation matters | Internal fragmentation matters (see Q6) |

### How to test

Use a small config first so you can reason by hand:

```text
max-overall-mem 64
mem-per-frame   16
mem-per-proc    32
```

Expected:

- Each process needs `32/16 = 2` frames.
- Memory holds `64/16 = 4` frames → at most 2 fully-resident processes.
- Force a third process → victim is paged out; `num-paged-out` increases.
- When the victim runs again → `num-paged-in` increases.

---

## Q3 — Role of `MemoryBlock` in a paging environment

### Role in the interface

`MemoryBlock { start, size }` describes a contiguous region. In the **flat** allocator it is perfect: each process owns one block, and `operator<` keeps blocks sorted by address for first-fit scanning. That matches our current `MemoryManager::Allocation { base, size }`.

### Is it wise in paging? **No (as the primary structure).**

**Why not:**

- Paging allocations are **non-contiguous** in physical memory. A process’s 256 pages may sit in frames `{3, 40, 7, 900, ...}`.
- A single `{start, size}` cannot represent that scattered mapping.
- Sorting `MemoryBlock` by `start` assumes contiguous layout; page tables already provide the real map.

### Better alternative for paging

Use page-oriented structures instead of (or in addition to) `MemoryBlock`:

```cpp
struct PageTableEntry {
    int frameNumber = -1;   // physical frame, or -1 if not in memory
    bool present = false;
    bool dirty = false;
    // optional: backingStoreOffset
};

struct ProcessAddressSpace {
    std::string processName;
    std::vector<PageTableEntry> pageTable;  // index = VPN
};

// Global free-frame tracker
std::vector<bool> frameOccupied;            // size = totalFrames
// or std::queue<int> freeFrames;
```

`MemoryBlock` can still be useful **inside the flat allocator only**, or as a free-list node for contiguous frame ranges in a hybrid allocator — but not as the main paging model.

---

## Q4 — Where the backing store lives

### What is meant to happen

Backing store is the “disk” side of virtual memory: pages that are not in RAM are stored there so they can be brought back later.

### Exact recommended location

Put backing-store logic **inside the paging allocator class**, not in `Scheduler` and not in the flat allocator.

```text
src/
  IMemoryAllocator.h          // interface only
  FlatMemoryAllocator.*       // no backing store
  PagingAllocator.h/.cpp      // owns BackingStore
      └── BackingStore.h/.cpp // file or in-memory page slots
```

**Justification:**

1. Only `PAGING` needs it; flat MCO2 explicitly has **no swapping**.
2. `allocate` / `deallocate` are exactly where page-in and page-out occur.
3. Keeps the scheduler unchanged: it still just calls allocate/deallocate/visualize.
4. Matches OS design: VM subsystem owns swap, not the CPU scheduler.

Concrete placement inside `PagingAllocator`:

- Member: `BackingStore backingStore_;`
- On victim eviction: `backingStore_.write(process, vpn, pageBytes);` then `++numPagedOut_`
- On fault / allocate miss: `backingStore_.read(...)` then `++numPagedIn_`

Suggested on-disk file (when implemented): `backing-store.bin` or a `backing-store/` folder of page files, created beside `memory-stamps/`.

---

## Q5 — Mapping `void* ptr` back to a PTE / frame

### What is meant to happen

`deallocate(void* ptr)` receives an opaque handle. The allocator must recover:

1. which process / address space it belongs to
2. which page table entries to clear
3. which physical frames to free

### Practical mapping strategy for the emulator

Do **not** treat `ptr` as a raw host `malloc` pointer. Treat it as an emulator handle:

**Option A (recommended for this project):**  
`ptr` encodes / points to a small allocation record:

```cpp
struct AllocationHandle {
    std::string processName;
    size_t virtualSize;
};
// allocate() returns static_cast<void*>(new AllocationHandle{...})
```

Then `deallocate(ptr)`:

1. Cast back to `AllocationHandle*`.
2. Look up `pageTables_[handle->processName]`.
3. For each present PTE, free `frameNumber`, clear Present.
4. Delete the handle.

**Option B:**  
`ptr` is a virtual base address (e.g. `0`). Deallocate scans a global map `processName → pageTable` using scheduler context. This is weaker because `deallocate` alone may not know the process.

**PTE → frame path:**

```text
void* ptr
  → AllocationHandle / process id
  → pageTable[VPN]
  → if present: PFN = pte.frameNumber
  → freeFrames.push(PFN)
```

In a real OS, hardware walks PGD→P4D→PUD→PMD→PTE from the virtual address; our emulator simulates that with a 1-level (or multi-level) software page table.

---

## Q6 — Internal fragmentation in paging

### What is meant to happen

**Internal fragmentation** = unused space **inside** an allocated page/frame.

Example with our config:

- Frame size = 16 bytes
- Process asks for 20 bytes → needs 2 frames (32 bytes)
- Wasted = `32 - 20 = 12` bytes inside the last page

Even when `mem-per-proc` is an exact multiple of `mem-per-frame` (4096 ÷ 16 = 256), internal fragmentation is **zero for the process image size**, but can still appear if:

- instruction working sets / declared symbols don’t fill the last page, or
- the emulator allocates whole pages for smaller objects later (e.g. symbol tables)

### Contrast with MCO2

MCO2 flat allocation reports **external** fragmentation (free holes that may or may not fit the next process). Paging mostly eliminates external fragmentation of process blocks, but introduces internal waste inside pages.

### How to test / observe

1. Set `mem-per-proc` not divisible by `mem-per-frame` (e.g. proc=20, frame=16).
2. After allocate, compute `allocatedFrames * frameSize - requestedSize`.
3. Confirm visualize/`vmstat` can report that waste if required by the next MCO.

---

## Q7 — Where `num-paged-in` and `num-paged-out` are tallied

### What is meant to happen

These counters measure traffic with the backing store:

| Counter | Increment when |
|---|---|
| `num-paged-in` | A page’s contents are loaded from backing store into a frame (or first load into RAM from “disk”) |
| `num-paged-out` | A victim page is written out to backing store to free a frame |

### Exact code locations (design)

Inside **`PagingAllocator` only**:

```text
PagingAllocator::allocate(...)
  └── on page fault / need frame:
        if victim needed:
            backingStore.write(victim)   → ++numPagedOut_
            install new page into frame  → ++numPagedIn_
        else if loading existing swapped page:
            backingStore.read(...)       → ++numPagedIn_

PagingAllocator::deallocate(...)
  └── usually does NOT increment page-in/out
      (it frees frames; optional cleanup of backing pages)
```

Expose them via:

- `visualizeMemory()` / `vmstat` output
- getters used by `report-util` / a future `vmstat` command

Flat allocator must leave both counters at **0** forever.

### How to test

1. Fill all frames with processes.
2. Admit one more process that needs a frame → expect `num-paged-out >= 1`.
3. Run the victim again → expect `num-paged-in` to rise.
4. Confirm flat mode still shows 0/0.

---

## Q8 — Why Linux uses multi-level page tables (64-bit)

### Short answer

A **single flat page table** for a 64-bit virtual address space is impossibly large. Multi-level tables allocate only the branches that are actually used.

### Math

Assume 4 KiB pages and 8-byte PTEs.

- Usable user/kernel virtual space is huge (canonical 48-bit or 57-bit on x86-64).
- Even a 48-bit VA space has `2^48 / 2^12 = 2^36` pages.
- Flat table of 8-byte entries ≈ `2^36 * 8 = 2^39` bytes ≈ **512 GiB** of page-table memory **per address space**, mostly empty.

Multi-level trees store only populated regions. Empty high-level entries mean entire subtrees are absent.

### Linux evidence (x86-64)

Linux uses up to **5 levels**:

```text
PGD → P4D → PUD → PMD → PTE
```

From the kernel:

- Types / shifts: [`arch/x86/include/asm/pgtable_64_types.h`](https://github.com/torvalds/linux/blob/master/arch/x86/include/asm/pgtable_64_types.h)
  - `PTRS_PER_PGD 512`
  - `P4D_SHIFT 39`
  - `PUD_SHIFT 30`
  - `PMD_SHIFT 21`
  - `PTRS_PER_PTE 512`
- Walker that descends those levels: [`mm/pagewalk.c`](https://github.com/torvalds/linux/blob/master/mm/pagewalk.c) (`walk_p4d_range` → `walk_pud_range` → `walk_pmd_range` → PTEs)

That file is the clearest “multi-page / multi-level” implementation to cite for the worksheet.

---

## Q9 — Linux analogues of `MemoryBlock`

Our worksheet `MemoryBlock { start, size }` is a contiguous-region descriptor. Closest Linux ideas:

### 1. `struct page` — metadata for one physical page frame

- Declared across `include/linux/mm_types.h`
- One `struct page` per physical page; tracks allocation state, mapping, flags
- Closest to “one unit of physical memory,” like one frame record in our paging design

### 2. `struct free_area` — free contiguous buddy blocks

From `include/linux/mmzone.h` / used in `mm/page_alloc.c`:

```cpp
struct free_area {
    struct list_head free_list[MIGRATE_TYPES];
    unsigned long nr_free;
};
```

Each order in `zone->free_area[order]` represents free blocks of size `2^order` pages — i.e. contiguous `{start, size}` style free memory, managed by the buddy allocator.

### 3. Buddy free-list operations in `mm/page_alloc.c`

Functions such as adding pages to free lists / coalescing buddies implement the same idea as sorting/merging free `MemoryBlock`s: track contiguous free physical ranges and split/merge them on allocate/free.

**Why these relate:**  
`MemoryBlock` ≈ “contiguous physical range descriptor.” Linux splits that idea into:

- per-page metadata (`struct page`)
- per-order free contiguous runs (`struct free_area` + buddy lists)

For paging **virtual** maps, the better analogue is the page-table entries themselves (`pte_t`, etc.), not `MemoryBlock`.

---

## Q10 — What `walk_pud_range` does

Prototype (Linux `mm/pagewalk.c`):

```cpp
static int walk_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
                          struct mm_walk *walk);
```

### Functionality

It is one step in Linux’s **page-table walk API**:

1. Given a **P4D** entry covering `[addr, end)`, locate the **PUD** table (`pud_offset`).
2. Iterate each PUD entry covering the next chunk of the address range (`pud_addr_end`).
3. If the PUD is empty (`pud_none`):
   - optionally call `pte_hole`, or
   - allocate lower tables if the walk is installing mappings.
4. If a `pud_entry` callback exists, invoke it.
5. Otherwise descend further into **PMD/PTE** walking (`walk_pmd_range` / huge-page helpers).
6. Return error codes from callbacks so higher levels can abort.

In plain language: **`walk_pud_range` walks the Page Upper Directory level of a virtual address range and either handles that level or continues down the multi-level page table.**

Source: [torvalds/linux `mm/pagewalk.c`](https://github.com/torvalds/linux/blob/master/mm/pagewalk.c)

---

## End-to-end: what should happen in the emulator

### Already working (verify any time) — flat MCO2 path

Config (`config.txt`):

```text
num-cpu 2
scheduler "rr"
quantum-cycles 4
batch-process-freq 1
min-ins 100
max-ins 100
delay-per-exec 0
max-overall-mem 16384
mem-per-frame 16
mem-per-proc 4096
```

Expected behavior:

1. At most **4** processes in memory at once.
2. Extra processes wait in the ready queue (no backing store yet).
3. Every **4** CPU cycles, a stamp is written to `memory-stamps/memory_stamp_XX.txt` (zero-padded).
4. `scheduler-stop` stops **new stamps** because the tick loop stops; processes may still finish and release memory, but no new stamp files appear afterward. That is normal.
5. `memory-clear` empties `memory-stamps/`.
6. `exit` does **not** auto-delete outputs or stamps.

### Designed next — paging path

1. Scheduler still admits via `allocate`.
2. When frames are exhausted, paging **evicts** to backing store instead of only rotating the ready queue.
3. `visualizeMemory` / stamps / `vmstat` show resident pages and page-in/out counts.
4. Flat mode remains selectable for MCO2-style demos.

---

## How to test (checklist)

### A. Prove current flat path still works

```text
memory-clear
initialize
scheduler-start
(wait ~5–10 seconds)
scheduler-stop
screen -ls          # repeat until Finished / timeout
exit
```

Check:

- [ ] `memory-stamps/` contains `memory_stamp_04.txt`, `08`, `12`, …
- [ ] Each stamp has ≤ 4 processes
- [ ] Later stamps can show different process names after churn
- [ ] No new stamps appear after `scheduler-stop`
- [ ] `memory-clear` removes stamp files

Build/run:

```powershell
cd "c:\Users\asus\Desktop\CSOPESY OS MP"
cmake --build build
.\build\csopesy_os_mp.exe
```

### B. Tests to run once paging is implemented

| Test | Setup | Expect |
|---|---|---|
| Frame math | mem 64 / frame 16 / proc 32 | 2 frames/process, 4 frames total |
| Capacity | spawn 3 processes | 3rd causes page-out or waits per policy |
| Counters | force eviction + reuse | `num-paged-out` then `num-paged-in` increase |
| Deallocate | finish a process | its frames return to free list |
| Visualize | every quantum | stamp/`vmstat` reflects resident set |
| Flat regression | select flat allocator | counters stay 0; MCO2 stamps still correct |

### C. Interface smoke test (after Q1 refactor)

1. Construct `FlatMemoryAllocator` behind `IMemoryAllocator*`.
2. Allocate until full → next allocate fails / returns null.
3. Deallocate one → allocate succeeds again.
4. `visualizeMemory()` string contains `----end----` / process names / `----start----`.

---

## Suggested file split for the group (implementation later)

| Person | Focus |
|---|---|
| 1 | `IMemoryAllocator` + refactor flat `MemoryManager` behind it |
| 2 | `PagingAllocator` `allocate` / `deallocate` + free-frame list + PTE model |
| 3 | `BackingStore` + `num-paged-in/out` + `visualizeMemory` / `vmstat` wiring |

Week 11 itself is primarily this design + Linux research document; paging code is the follow-up milestone.

---

## Summary answers (quick reference)

1. **Interface:** Scheduler depends on `IMemoryAllocator`; flat and paging are swappable implementations.
2. **Paging allocate/deallocate:** Map VPNs to free frames; on pressure, evict to backing store; deallocate clears PTEs and frees frames.
3. **MemoryBlock in paging?** **No** as the primary model — use page tables + free-frame lists instead.
4. **Backing store location:** Inside `PagingAllocator` (dedicated helper class/file).
5. **`void* ptr` → PTE:** Treat ptr as an allocation handle → process page table → present PTEs → frame numbers.
6. **Internal fragmentation:** Wasted bytes inside the last allocated page when request size is not a multiple of frame size.
7. **Counters:** Increment in paging allocate/evict/load paths only.
8. **Linux multi-level:** Flat 64-bit tables are enormous; PGD/P4D/PUD/PMD/PTE allocate sparsely (`pgtable_64_types.h`, `mm/pagewalk.c`).
9. **Linux analogues:** `struct page`, `struct free_area`, buddy lists in `mm/page_alloc.c`.
10. **`walk_pud_range`:** Walks the PUD level of a VA range during a page-table walk and descends or handles holes/callbacks.
