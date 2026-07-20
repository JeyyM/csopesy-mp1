# MCO2 Implementation Notes

## What Was Added

### 1. `src/MemoryManager.h` and `src/MemoryManager.cpp` (NEW)

A **first-fit flat memory allocator** that manages a single contiguous address
space of `maxOverallMem` bytes. Every allocation is exactly `memPerProc` bytes.

| Method | What it does |
|---|---|
| `configure(total, perProc)` | Initialise (or re-initialise) with given sizes. Clears any prior allocations. |
| `allocate(name)` | First-fit scan from address 0. Returns base address on success, `-1` if memory is full. |
| `release(name)` | Frees the block owned by `name`. No-op if not found. |
| `snapshotDescending()` | Returns all current allocations sorted highest-address-first (for stamp files). |
| `externalFragmentationBytes()` | Returns `totalMemory - (allocatedCount × memPerProc)` (total free bytes). |
| `isConfigured()` | True when both `totalMemory > 0` and `memPerProc > 0`. |

**Thread safety:** MemoryManager has **no internal mutex**. All callers hold
`Scheduler::mutex_` before calling into it, which is sufficient and avoids
nested-lock complexity.

---

### 2. `src/Config.h` and `src/Config.cpp` (MODIFIED)

Three new **optional** fields added to `struct Config`:

| Field | Config key | Meaning |
|---|---|---|
| `maxOverallMem` | `max-overall-mem` | Total physical memory in bytes |
| `memPerFrame` | `mem-per-frame` | Frame size in bytes (display only) |
| `memPerProc` | `mem-per-proc` | Bytes each process needs to run |

Parsing: same key-value `while (file >> key)` loop; all three keys are accepted
but **not required** — existing configs without them still work fine.

---

### 3. `src/ProcessModel.h` (MODIFIED)

Added one new atomic field:

```cpp
std::atomic<int> memoryBase_{-1};   // -1 = not in memory
```

And two accessors:

```cpp
int  memoryBase() const     { return memoryBase_.load(); }
void setMemoryBase(int base){ memoryBase_.store(base); }
```

The field is `-1` when a process is waiting in the ready queue without memory,
and set to the base address once the scheduler successfully allocates for it.

---

### 4. `src/Scheduler.h` (MODIFIED)

- `#include "MemoryManager.h"` added
- `MemoryManager memoryManager_{}` member added
- `uint64_t lastStampCycle_ = 0` member added (deduplicate stamp writes)
- `void maybeWriteMemoryStamp()` private method declared

---

### 5. `src/Scheduler.cpp` (MODIFIED)

**`start()`** — configures the memory manager on scheduler start:
```cpp
if (config_.maxOverallMem > 0 && config_.memPerProc > 0)
    memoryManager_.configure(config_.maxOverallMem, config_.memPerProc);
else
    memoryManager_.configure(0, 0);   // disabled
lastStampCycle_ = 0;
```

**`schedulerLoop()`** — memory admission gate added before assigning a process
to a core:
```cpp
if (memoryManager_.isConfigured() && next->memoryBase() == -1) {
    const int base = memoryManager_.allocate(next->name());
    if (base == -1) {
        readyQueue_.push_back(next);   // memory full → tail of queue
        continue;
    }
    next->setMemoryBase(base);
}
```

**`coreLoop()`** — memory released when a process finishes, combined with the
existing core-slot clear under `mutex_`:
```cpp
if (result == ProcessSliceResult::Finished && memoryManager_.isConfigured()) {
    memoryManager_.release(job->name());
    job->setMemoryBase(-1);
}
coreCurrent_[coreId] = nullptr;
```

**`advanceGlobalCpuCycles()`** — calls `maybeWriteMemoryStamp()` after each
tick in addition to the existing batch-spawn and sleep-wake calls.

**`maybeWriteMemoryStamp()`** — fires when:
- `memoryManager_.isConfigured()` is true
- `cpuCycles_ % quantumCycles == 0`
- This cycle has not already been stamped (`lastStampCycle_`)

It takes a locked snapshot of the memory layout, then writes the file
**outside the lock** to avoid stalling the scheduler during disk I/O.

---

### 6. `CMakeLists.txt` (MODIFIED)

`src/MemoryManager.cpp` added to the `add_executable` source list.

---

### 7. `config.txt` (UPDATED)

Set to the MCO2 required values:
```
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

---

## How to Verify

### Step 1 — Launch and initialise
```
.\build\csopesy_os_mp.exe
initialize
```
Expected: `System initialized successfully … 2 CPU cores. Scheduler: Round Robin.`

### Step 2 — Run the scheduler
```
scheduler-start
```
Expected: `Scheduler started. Generating processes.`

### Step 3 — Check `screen -ls` immediately
```
screen -ls
```
Expected:
- `CPU utilization: 100%` (processes running)
- Up to **4 processes** listed as Running (memory only holds 4 × 4096 = 16384 bytes)
- Additional processes waiting in the ready queue (not shown in Running)

### Step 4 — Verify memory stamp files are created
After 4 CPU ticks (`quantum-cycles = 4`), the file `memory_stamp_4.txt` should
appear in the working directory. After 8 ticks, `memory_stamp_8.txt`, and so on.

Open any stamp file. Verify format:
```
Timestamp: (MM/DD/YYYY HH:MM:SSAM)
Number of processes in memory: N     ← 1 to 4
Total external fragmentation in KB: F

----end---- = 16384

<upper>
<name>
<lower>
...
----start---- = 0
```

Each process block satisfies `upper - lower == 4096`.

### Step 5 — Stop and drain
```
scheduler-stop
```
Wait ~10 seconds, then repeat `screen -ls` every 2 seconds.
Processes should progressively move to Finished.
Memory stamp files continue being written until all processes finish.

### Step 6 — Exit and collect stamps
```
exit
```
Collect all `memory_stamp_*.txt` files from the working directory into a ZIP.

---

## Run Order (Test Case)

```
1.  .\build\csopesy_os_mp.exe
2.  initialize
3.  scheduler-start
4.  [wait 5 seconds]
5.  scheduler-stop
6.  screen -ls          ← repeat every 2 seconds
7.  screen -ls
8.  screen -ls          ← continue until all Finished (or 1 minute)
9.  exit
10. ZIP all memory_stamp_*.txt files
```

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| MemoryManager has no internal mutex | All callers hold `Scheduler::mutex_` already; a second mutex would introduce lock-ordering risk |
| File I/O happens **outside** the scheduler mutex | Disk writes can block for milliseconds; releasing the lock first keeps core threads unblocked |
| `memoryBase_ = -1` means "not in memory" | Atomic, so the report manager and process screen can read it lock-free without a separate flag field |
| Memory released in `coreLoop` (not `runProcessSlice`) | `coreLoop` already acquires `mutex_` to clear the core slot; combining the release avoids a second lock acquisition |
| Optional config fields | Existing configs (without the three new keys) still load and run correctly; memory management simply stays disabled |
| External fragmentation = total free bytes | Matches the spec sample output: 16384 − (2 × 4096) = 8192 |
