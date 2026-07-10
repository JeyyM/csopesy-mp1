# MCO2 — First-Fit Flat Memory Allocator with Round-Robin Scheduler

## Overview

Extend the existing OS emulator to simulate a **first-fit, flat memory allocator** integrated with the Round-Robin CPU scheduler. Main memory has a fixed size of **16,384 bytes**. Each process requires a fixed allocation of **4,096 bytes** to run. Memory is only released when a process finishes execution.

---

## Configuration Parameters

Add the following new parameters to `config.txt` (in addition to existing ones):

| Parameter        | Required Value | Description                                              |
|------------------|----------------|----------------------------------------------------------|
| `num-cpu`        | 2              | Number of CPU cores                                      |
| `scheduler`      | `"rr"`         | Scheduling algorithm (Round Robin)                       |
| `quantum-cycles` | 4              | RR time slice in CPU cycles                              |
| `batch-process-freq` | 1          | Spawn one new process every CPU tick                     |
| `min-ins`        | 100            | Minimum instructions per process                         |
| `max-ins`        | 100            | Maximum instructions per process (fixed at 100)          |
| `max-overall-mem`| 16384          | Total physical memory in bytes                           |
| `mem-per-frame`  | 16             | Size of each memory frame in bytes                       |
| `mem-per-proc`   | 4096           | Fixed memory requirement per process in bytes            |

### Derived Values
- Total frames: `max-overall-mem / mem-per-frame` = 16384 / 16 = **1024 frames**
- Max processes in memory simultaneously: `max-overall-mem / mem-per-proc` = 16384 / 4096 = **4 processes**

---

## Memory Allocator Rules

### 1. First-Fit Allocation
- Scan physical memory from address 0 upward
- Allocate the **first contiguous block** large enough to fit `mem-per-proc` bytes
- Each process gets exactly `mem-per-proc` (4096) bytes — no more, no less

### 2. Fixed Allocation Per Process
- Every process requires exactly `mem-per-proc` bytes
- The allocation is determined by `mem-per-proc` in config, not per-process size

### 3. Memory Held for Entire Execution
- Once a process is allocated memory, it **keeps that memory until it finishes**
- Memory is **only freed when the process reaches `Finished` status**
- No partial releases or mid-run reallocations

### 4. No Backing Store (No Swapping)
- If memory is **full** when a process is about to be scheduled onto a core:
  - The process is **moved to the tail of the ready queue**
  - It will be retried the next time the scheduler picks it up
  - No disk swapping or paging occurs

### 5. Process Address Space
- Each process has a **lower limit** (start address) and **upper limit** (start + mem-per-proc)
- Example: process allocated at address 12288 → lower=12288, upper=16384
- Processes **can occupy different address spaces** across different runs (addresses are not fixed to a process)

---

## External Fragmentation

External fragmentation = total free memory that **cannot** satisfy a new allocation request because it is not contiguous.

With first-fit flat allocation and fixed-size `mem-per-proc` blocks:
- Fragmentation occurs when free gaps are smaller than `mem-per-proc` (4096 bytes)
- Report fragmentation in **KB** (divide bytes by 1024)

Formula:
```
Total external fragmentation = sum of all free contiguous blocks < mem-per-proc bytes
```

Example: 2 processes in memory (2 × 4096 = 8192 bytes used), leaving 8192 free bytes.
If free space is split into two 4096-byte gaps: fragmentation = 0 (each can still fit a process).
If free space is one contiguous 8192-byte block: also 0 fragmentation.

---

## Memory Stamp Files

### Trigger
Write one memory stamp file **every `quantum-cycles` CPU ticks**.

- At CPU cycle 4: write `memory_stamp_04.txt`
- At CPU cycle 8: write `memory_stamp_08.txt`
- At CPU cycle 12: write `memory_stamp_12.txt`
- And so on...

File naming: `memory_stamp_<qq>.txt` where `qq` = the current quantum cycle number (zero-padded as needed).

### File Format

```
Timestamp: (MM/DD/YYYY HH:MM:SSAM)
Number of processes in memory: <N>
Total external fragmentation in KB: <F>

----end---- = 16384

<upper address of topmost process>
<process name>
<lower address of topmost process>

<upper address of next process>
<process name>
<lower address of next process>

...

----start---- = 0
```

### Layout Rules
- Print from **highest address to lowest** (top-down memory map)
- For each process currently in memory: print its **upper limit**, **name**, then **lower limit**
- Print `----end---- = <max-overall-mem>` at the top
- Print `----start---- = 0` at the bottom
- Free gaps between processes are **not printed** — only occupied blocks appear

### Example Output (`memory_stamp_168.txt`)
```
Timestamp: (08/06/2024 09:15:22AM)
Number of processes in memory: 2
Total external fragmentation in KB: 8192

----end---- = 16384

16384
P1
12288

8192
P9
4096

----start---- = 0
```

**Explanation of example:**
- P1 occupies addresses 12288–16384 (upper=16384, lower=12288)
- P9 occupies addresses 4096–8192 (upper=8192, lower=4096)
- Gap from 0–4095 is free (4096 bytes = 4KB)
- Gap from 8193–12287 is free (4096 bytes = 4KB)
- Total free = 8192 bytes = 8KB but split into two 4KB gaps
- External fragmentation = 8192 bytes (reported in KB as 8192 — note: check if spec means bytes or KB here)

---

## Scheduler Integration Changes

### Admission Control (Memory Gate)
Before assigning a process from the ready queue to a CPU core:

```
if process has no memory allocated:
    attempt first-fit allocation
    if allocation fails (memory full):
        move process to tail of ready queue
        skip this process
    else:
        proceed with scheduling
```

### Memory Release on Finish
When a process transitions to `Finished`:
- Immediately free its memory block
- The freed block becomes available for the next first-fit allocation

### Memory Stamp Writer
A background task (or hook in the tick loop) that:
- Fires every `quantum-cycles` CPU ticks
- Takes a snapshot of the current memory layout
- Writes the formatted stamp file to the working directory

---

## Test Case to Record

**Step-by-step sequence for the video submission:**

1. Launch the emulator (press **Run/Debug** from IDE — must be visible on video)
2. Type `initialize`
3. Type `scheduler-start`
4. **Wait 5 seconds** (let processes spawn and run)
5. Type `scheduler-stop`
6. Type `screen -ls` every **2 seconds** to show scheduling information
7. Continue until **all processes show as Finished** (or 1 minute has passed — then move on)
8. Type `exit` to close the emulator
9. **Collect all `memory_stamp_*.txt` files and submit them in a ZIP**

---

## Expected Result

> The emulator correctly simulates a round-robin scheduler with a first-fit, flat memory allocator. Several processes will finish execution. Since total memory only holds **at most 4 processes** simultaneously (4 × 4096 = 16,384 bytes), there will be visible memory pressure and fragmentation. Processes will occupy different address spaces across different runs as memory is freed and reallocated.

---

## Implementation Checklist

- [ ] **Config.h / Config.cpp** — add `maxOverallMem`, `memPerFrame`, `memPerProc` fields and parsing
- [ ] **MemoryManager.h / .cpp** — new class implementing first-fit flat allocator
  - `allocate(processName)` → returns start address or -1 if full
  - `free(processName)` → releases the block
  - `snapshot()` → returns sorted list of allocated blocks for stamp writer
  - `externalFragmentation()` → computes total fragmentation in bytes
- [ ] **Scheduler.cpp** — memory admission gate before core assignment
- [ ] **Scheduler.cpp** — free memory when process finishes
- [ ] **MemoryStampWriter** (new or inside Scheduler) — write `memory_stamp_<qq>.txt` every `quantum-cycles` ticks
- [ ] **ProcessModel** — store `memoryBase` (lower limit) once allocated
- [ ] **screen -ls / report-util** — optionally show memory info alongside process list

---

## File Output Location

Memory stamp files should be written to the **current working directory** (same folder as the executable), named:
```
memory_stamp_4.txt
memory_stamp_8.txt
memory_stamp_12.txt
...
```

Submit all generated stamp files in a single ZIP file for grading.
