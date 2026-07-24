# MCO2 Specifications — Multi-tasking OS with Memory Management

> Updated as of: June 24, 2025
> **[100 pts] General Instructions:** The final part is your multi-tasking OS with memory management.

This document captures the MCO2 requirements as provided. More specifications may be added later.

---

## Overview

The final deliverable is a multi-tasking OS emulator with **memory management**, extending the MCO1 CPU scheduler. It must support a **demand paging allocator**, a **backing store**, memory-access instructions, per-process memory requirements, and memory-debugging commands (`process-smi`, `vmstat`).

---

## Checklist of Requirements

Your system must have ALL the following features implemented properly.

### 1. Main menu console

The main menu shows the CSOPESY ASCII banner, welcome text, developer info, and a `root:\>` prompt.

```
  ______  _______  ______  ______  _______  _______ __   __
 /      ||       ||      ||      ||       ||       |  | |  |
|  ,----'|  _____||  ,---'|  ,---'|    ___||  _____|  |_|  |
|  |     | |_____ |  |    |  |    |   |___ | |_____|       |
|  `----.|_____  ||  `---.|  `---.|    ___||_____  |_     _|
 \______||_______||______||______||_______||_______| |___|

Welcome to CSOPESY Emulator!

Developers:
Del Gallego, Neil Patrick

Last updated: 01-18-2024
root:\>
```

Additional commands that must be recognized in the main menu:

- **`process-smi`** — provides a summarized view of the available/used memory, as well as the list of processes and memory occupied. This is similar to the `nvidia-smi` command.
- **`vmstat`** — provides a detailed view of the active/inactive processes, available/used memory, and pages.

---

### 2. Memory manager

- It must support a **demand paging allocator**.
- For the demand paging allocator, **pages are loaded into physical memory frames on demand**. When a process references a virtual memory page that is not currently in a frame, a **page fault** occurs, and the required page is brought from the **backing store** into a free frame.
- If no frames are free, a **page replacement algorithm** selects a page to be **evicted** to the backing store.

Additional detail (from "The memory manager" section):

- The system simulates memory in the background, limited by the maximum amount of main memory allocated by the original OS. Memory spaces are bound within the running program's memory address.
- Memory spaces are **pre-allocated** and free to use by any processes upon startup.
- The memory space is typically limited to **N bytes**, and each process utilizes a fraction of the memory.
- The memory manager must support **backing store operations** when in low memory — context-switching processes in and out of the backing store (writing/reading in a file).

---

### 3. Memory visualization and backing store access

- The application must have some way to debug memory, such as `vmstat` and `process-smi`.
- The **backing store** is represented as a text file that can be accessed at any given time. It is saved in a text file **`csopesy-backing-store.txt`**.

---

### 4. Required memory per process

When creating processes via the `screen -s` command, a **memory size is required**. The new `screen -s` command is:

```
screen -s <process_name> <process_memory_size>
```

This creates a new process with a given name and memory allocation.

**NOTES:**

- All memory ranges are **[2^6, 2^16] bytes** and in the **power of 2** format. The console will throw an **"invalid memory allocation"** message to the user if it's outside of range.
- Sample usage: `screen -s process1 256` (allocates 256 bytes to the process).
- Processes must require memory of **at least 64 bytes** to store variables.

---

### 5. Simulating memory access via process instruction

In addition to previous process instructions (e.g. PRINT, DECLARE, etc.), there must be a mechanism to simulate memory access:

- **`READ <var>, <memory_address>`** — performs a retrieval of a `uint16` value from memory and stores it to a variable, `var`. If the memory block isn't initialized, the `uint16` value is **0**.
- **`WRITE <memory_address>, <value>`** — writes a `uint16` value to the specified memory address.

**NOTES:**

- Variables are tied to a process, stored in memory, and will **not be released until the process finishes**.
- `uint16` variables are clamped between `(0, max(uint16))` and consume **2 bytes** of memory.
- `uint16` variables are stored in the **symbol table segment** of the process.
- The symbol table segment has a **fixed size of 64 bytes**. The program can store a maximum of **32 variables**. If the limit is reached, succeeding instructions involving variable declarations will be **ignored**.
- `memory_address` is a **hexadecimal** value. Example usage:
  1. `READ my_var 0x1000` — Read the `uint16` value at address `0x1000` and store it in `my_var`. If `0x1000` is uninitialized, returns a 0.
  2. `WRITE 0x2000 42` — Writes the `uint16` value 42 to address `0x2000`.
  3. `READ my_var_2 0x2000` — Reads the `uint16` value at address `0x2000`. Since this is initialized already, it returns a value of 42.
- Attempting to read/write to an **invalid memory address** (e.g. outside the dedicated memory space) will throw an **access violation error** and shut down the process, akin to a memory access violation error in user programs.
- Memory addresses and representation of memory are **emulated**. It is **not** a 1:1 mapping of the physical memory/RAM when running the program.
- Read/write memory operations are now included in **generating process instructions** via the `scheduler-start` command.

---

### 6. User-defined instructions during process creation

Ability to add a set of user-defined instructions when creating a process. The `screen -c` command is:

```
screen -c <process_name> <process_memory_size> "<instructions>"
```

- This sends a string of **1 – 50 instructions** to be executed by the specified process.
- Instructions are **semicolon-separated**.
- Throws **"invalid command"** if the instruction size is not met.
- The process memory size follows the same validation as `screen -s` (power of two, [2^6, 2^16]).

> **⚠️ Inconsistency in the PDF:** The formal command definition includes `<process_memory_size>`, but the PDF's own **sample** omits it (shown below, verbatim). Implement per the formal definition (**include** the memory size, e.g. `screen -c process2 4096 "..."`) unless the instructor issues a corrected syntax.

**Sample usage (verbatim from PDF — note the missing size argument):**

```
screen -c process2 "DECLARE varA 10; DECLARE varB 5; ADD varA varA varB; WRITE 0x500 varA; READ varC 0x500; PRINT(\"Result: \" + varC)"
```

**Corrected form (with required memory size):**

```
screen -c process2 4096 "DECLARE varA 10; DECLARE varB 5; ADD varA varA varB; WRITE 0x500 varA; READ varC 0x500; PRINT(\"Result: \" + varC)"
```

1. `DECLARE varA 10` — Declares a `uint16` variable "varA" and sets it to 10.
2. `DECLARE varB 5` — Declares a `uint16` variable "varB" and sets it to 5.
3. `ADD varA varA varB` — Adds varA and varB, storing the result (15) in varA.
4. `WRITE 0x500 varA` — Writes the value of varA (15) to memory address `0x500`.
5. `READ varC 0x500` — Reads the `uint16` value from memory address `0x500` and stores it in varC.
6. `PRINT("Result: " + varC)` — Prints the string "Result: " followed by the value of varC (which should be 15).

---

### 7. Previous features from MO1

All implemented features from the MO1, but with additional features, focused on **memory management** and **file system interface**.

---

### 8. `screen -r` update (memory access violation)

To indicate memory access violation errors, the `screen -r` command must be updated:

- **From MO1:** The user can access the screen anytime by typing `screen -r <process name>` in the main menu. If the process name is not found/finished execution, the console prints:
  ```
  Process <process name> not found.
  ```
- **Addition for MO2:** If the process name has **prematurely shut down** due to a memory access violation error, the console should print:
  ```
  Process <process name> shut down due to memory access violation error that occurred at <HH:MM:SS>. <Hex memory address> invalid.
  ```

---

## Memory Visualization Details

There must be a mechanism to visualize and debug memory. The user can use either `process-smi` (high-level overview of available/used memory) or `vmstat` (fine-grained memory details).

### `process-smi`

Similar to the `nvidia-smi` command; prints a summarized view of the memory allocation and utilization of the processor (CPU for your program / GPU for nvidia-smi). Sample mockup:

```
root:\> process-smi

-----------------------------------------------
| PROCESS-SMI V01.00 Driver Version: 01.00 |
-----------------------------------------------
CPU-Util: 100%
Memory Usage: 1245MiB / 4799MiB
Memory Util: 26%

===============================================
Running processes and memory usage:
-----------------------------------------------
process05 134MiB
process06 134MiB
process07 977MiB
-----------------------------------------------
root:\>
```

### `vmstat`

The `vmstat` command provides a more detailed view. The following information is included:

| Field | Description |
|---|---|
| Total memory | Total main memory in **bytes**. |
| Used memory | Total active memory used by processes. |
| Free memory | Total free memory that can still be used by other processes. |
| Idle cpu ticks | Number of ticks wherein CPU cores remained idle. |
| Active cpu ticks | Number of ticks wherein CPU cores are actually executing instructions. |
| Total cpu ticks | Number of ticks that passed for all CPU cores. |
| Num paged in | Accumulated number of pages paged in. |
| Num paged out | Accumulated number of pages paged out. |

Sample layout (following Linux `vmstat -s`):

```
      16366040 K total memory
       5522924 K used memory
       6847600 K active memory
       5176984 K inactive memory
       3595752 K free memory
        370116 K buffer memory
       6877248 K swap cache
      16715772 K total swap
             0 K used swap
      16715772 K free swap
       4346370 non-nice user cpu ticks
         10222 nice user cpu ticks
        602720 system cpu ticks
      76488300 idle cpu ticks
         74043 IO-wait cpu ticks
             0 IRQ cpu ticks
          7043 softirq cpu ticks
             0 stolen cpu ticks
       5643394 pages paged in
      19691626 pages paged out
             0 pages swapped in
             0 pages swapped out
     136447937 interrupts
     518085297 CPU context switches
    1528741508 boot time
        145536 forks
```

---

## Configuration (`config.txt` and `initialize`)

The user must first run the `initialize` command. **No other commands should be recognized if the user hasn't typed this first.** Once entered, it reads the `config.txt` file which is space-separated in format, containing the following parameters.

### From MCO1 — OS Scheduler

| Parameter | Description |
|---|---|
| `num-cpu` | Number of CPUs available. The range is [1, 128]. |
| `scheduler` | The scheduler algorithm: `"fcfs"` or `"rr"`. |
| `quantum-cycles` | The time slice given for each processor if a round-robin scheduler is used. Has no effect on other schedulers. The range is [1, 2^32]. |
| `batch-process-freq` | The frequency of generating processes in the `scheduler-test` command in CPU cycles. The range is [1, 2^32]. If one, a new process is generated at the end of each CPU cycle. |
| `min-ins` | The minimum instructions/command per process. The range is [1, 2^32]. |
| `max-ins` | The maximum instructions/command per process. The range is [1, 2^32]. |
| `delays-per-exec` | Delay before executing the next instruction in CPU cycles. The delay is a "busy-waiting" scheme wherein the process remains in the CPU. The range is [0, 2^32]. If zero, each instruction is executed per CPU cycle. |

### New parameters for MCO2 — Multitasking OS

> All memory ranges are **[2^6, 2^16]** and in the **power of 2** format.

| Parameter | Description |
|---|---|
| `max-overall-mem` | Maximum memory available in bytes. |
| `mem-per-frame` | The size of memory in bytes per frame. This is also the memory size per page. The total number of frames is equal to `max-overall-mem / mem-per-frame`. |
| `min-mem-per-proc` | Memory required for each process created via the `scheduler-start` command (lower bound of the rolled value). |
| `max-mem-per-proc` | Upper bound of the rolled value. Let **P** be the number of pages required by a process and **M** be the rolled value between `min-mem-per-proc` and `max-mem-per-proc`. **P** can be computed as `M / mem-per-frame`. |

**Notes on page computation:**
- When a process is auto-generated (via `scheduler-start`), its memory requirement **M** is randomly rolled in the range `[min-mem-per-proc, max-mem-per-proc]`.
- The number of pages the process needs is `P = M / mem-per-frame`.
- `mem-per-frame` doubles as the **page size** (frame size == page size).

---

## Page and Frame Model

Derived quantities:

- **Page size = Frame size = `mem-per-frame`**
- **Number of physical frames = `max-overall-mem / mem-per-frame`**
- **Pages required by a process = `process_memory_size / mem-per-frame`**

### Per-process page table

Each process maintains a page table with at least:

| Field | Purpose |
|---|---|
| Virtual page number (VPN) | Index of the page in the process address space |
| Physical frame number | Frame holding the page, if resident |
| Present / valid bit | Whether the page is currently in physical memory |
| Backing-store location | Where the page lives when not resident |
| (Optional) dirty / reference bits | Used by the page-replacement algorithm |

### Global frame table

The memory manager maintains a frame table with at least:

| Field | Purpose |
|---|---|
| Frame number | Physical frame index |
| Owning process | Which process currently holds the frame (or free) |
| Virtual page stored | Which VPN is mapped here |
| Free / occupied status | Whether the frame is available |

### `initialize` responsibilities

On `initialize`, after reading `config.txt`, the emulator should: validate scheduler + memory values, **build the physical frames**, initialize page/frame tables, and prepare or clear the backing-store representation (`csopesy-backing-store.txt`).

### Process memory release (on finish or access-violation termination)

When a process ends (Finished **or** shut down by access violation):

- All physical frames it owns are freed and returned to the free-frame pool.
- All of its page-table entries are removed.
- Any backing-store entries belonging to it are released / marked unused.
- Its symbol table and emulated data are destroyed.
- **No** process memory should remain allocated afterward.

---

## Scheduler and Memory Interaction

Consistent with a real-world OS, **instructions can only be performed when a valid page has been found**. Page fault handling continuously occurs until a valid page has been returned, before an instruction is performed.

### Example scenario

```
screen -c faulty_process "DECLARE varA 10; DECLARE varB 5; ADD varA varA varB; WRITE 0x500 varA; READ varC 0x500; PRINT(\"Result: \" + varC)"
```

1. As there are only 3 variables required, this only occupies `2 x 3 = 6` bytes of memory, well within the 64-byte symbol table segment size.
2. Assume that the physical memory is full and occupied by other running processes.
3. Assume that `0x500` is not in physical memory, then a **page fault** occurs.
4. The demand pager finds a **victim frame** to be removed.
5. The `0x500` page is brought to a valid frame.
6. **Restart** the WRITE instruction.
7. Steps 3–5 repeat indefinitely until a valid frame is found.

Similarly, **variable declaration commands cannot execute if the symbol table segment is not in physical memory**. Thus, a page fault also occurs in that case.

---

## Shell Reference

Please refer to a general Linux / Windows PowerShell / Windows command line. This serves as a strong reference for the design of the command-line interface. Aside from this, check the memory debugging tools in Linux CLI to give an idea of what to do for the final output.

- https://www.linuxfoundation.org/blog/blog/classic-sysadmin-linux-101-5-commands-for-checking-memory-usage-in-linux

Relevant Linux tools referenced: `top`, `free`, `free -m`, `vmstat -s`.

---

## Assessment Method

- The CLI emulator will be assessed through a **black box quiz system** in a **time-pressure format**. This is to minimize drastic changes or "hacking" of the CLI to ensure the test cases are met.
- You should only modify the **parameters** and **no longer recompile** the CLI when taking the quiz.
- Test cases, parameters, and instructions are provided **per question**, wherein you must submit a **video file (.MP4)** demonstrating your CLI.
- Some questions will require submitting **PowerPoint presentations**, such as cases explaining the details of your implementation.

### Important Dates

See AnimoSpace for specific dates.

| Week | Activity |
|---|---|
| Week 12 | Mockup test case and quiz |
| Week 13 | Actual test case and quiz |

---

## Submission Details

Aside from video files for the quiz, you need to prepare some of the requirements in advance:

- **SOURCE** — Contains your source code. Add a `README.txt` with your name and instructions on running your program. Also, indicate the entry class file where the main function is located. An alternative can be a **GitHub link**.
- **PPT** — A technical report of your system containing:
  - Command recognition
  - Process representation **with an emphasis on memory representation**, such as memory addressing
  - Scheduler implementation
  - Memory management — **demand paging and backing store operation**

---

## Grading Scheme

You are to provide evidence for each test case, recorded through video. Each test case will have some points allocated. The test cases will be graded as follows:

### Robustness

| No points | Partial points | Full points |
|---|---|---|
| The CLI did not pass the test case. **NO WORKAROUND** is available to produce the expected output. | The CLI did not pass the test case. **A workaround** is available to produce the expected output. | The CLI passed the test case using **varying inputs** and produced the expected output. |

---

## Relationship to Prior First-Fit Work (Legacy / Separate)

The earlier `MCO2_Specs.md` (underscore) and the current codebase describe a **first-fit flat allocator** with fixed `mem-per-proc` (4096), external fragmentation reporting, `memory_stamp_*.txt` files, and **no swapping**. That model **conflicts** with this official demand-paging spec and must **not** be the main memory model.

- The official MCO2 model (this document) is **demand paging + backing store**.
- The first-fit flat allocator, fixed `mem-per-proc`, external fragmentation, and memory stamps should be treated as a **separate/optional test-case mode**, not a replacement for `process-smi`, `vmstat`, or `csopesy-backing-store.txt`.
- Do **not** run both models at once without explicitly selecting which mode is active — their allocation rules are incompatible.

---

## Implementation Checklist

- [ ] **Config parser** — `max-overall-mem`, `mem-per-frame`, `min-mem-per-proc`, `max-mem-per-proc`, `delays-per-exec` (plus existing MCO1 params); reject/reclassify `mem-per-proc`.
- [ ] **Process model** — requested memory size, virtual address range, page count, page table, 64-byte symbol table, termination reason, invalid-address record.
- [ ] **Memory manager** — frame table, free-frame search, page loading, page replacement, backing-store read/write, page-in/page-out counters, process cleanup.
- [ ] **Instruction system** — DECLARE, PRINT, ADD/SUBTRACT (MCO1), SLEEP, FOR, **READ**, **WRITE**, hex address validation.
- [ ] **Commands** — `initialize`, `scheduler-start`, `scheduler-stop`, `screen -s <name> <mem>`, `screen -c <name> <mem> "<instructions>"`, `screen -r`, `screen -ls`, `process-smi`, `vmstat`, `exit`.
- [ ] **Error handling** — invalid config, invalid memory allocation, invalid instruction count, invalid hex address, memory access violation, process termination reporting.
- [ ] **Backing store** — `csopesy-backing-store.txt`, page write-out, page read-in, cleanup, consistent formatting.

---

## Status / To Confirm

- [ ] Page replacement algorithm to use (FIFO / LRU / other) — spec says "a page replacement algorithm" without mandating a specific one; confirm choice.
- [ ] Exact units in `process-smi` (MiB in mockup) vs `vmstat` (bytes/KB) for our emulator scale.
- [ ] Precise formatting of `process-smi` and `vmstat` output for grading.
- [ ] Distribution/ratio of generated READ/WRITE instructions in `scheduler-start`.
- [ ] **`screen -c` syntax** — confirm whether the memory-size argument is required (formal definition) or omitted (PDF sample). Assume required until told otherwise.
