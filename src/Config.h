// Defines the settings the emulator needs to run, and the loader that reads
// them from config.txt when the user types "initialize".

//   There are two things in here:
//     1. Config struct  — a plain container holding every setting value
//                         (number of CPUs, scheduler type, timing values, etc.)
//     2. ConfigLoader   — a class with one job: open config.txt, read it line
//                         by line, validate every value, and fill in a Config
//
//   Together they are the implementation of the "initialize" command.
//   When the user types "initialize", main.cpp calls ConfigLoader::loadFromFile,
//   which either fills in the Config and returns true, or returns false with
//   a human-readable error message explaining exactly what went wrong.

// How "initialize" uses this file:
//
//   User types:  initialize
//   main.cpp calls:  ConfigLoader::loadFromFile("config.txt", config, error)
//   ConfigLoader:
//     opens config.txt
//     reads key-value pairs one at a time  (e.g. "num-cpu 4")
//     validates each value                 (e.g. num-cpu must be 1-128)
//     fills matching field in Config struct
//     checks all required keys were present
//     returns true (success) or false + error message (failure)
//   main.cpp:
//     on success: prints confirmation, unlocks other commands
//     on failure: prints error (e.g. "config.txt is missing num-cpu")

// After config is loaded::
//   main.cpp        — reads numCpu, scheduler, quantumCycles for the confirmation message
//   Scheduler.cpp   — uses all fields to configure worker threads and process generation
//   ScreenManager.cpp — passes config to Scheduler when auto-starting the engine via "screen -s"


#pragma once

#include <cstdint>
#include <string>

// Two scheduling algorithms are supported.
// FCFS: First-Come First-Served — processes run to completion in arrival order.
// RR:   Round Robin — each process gets a fixed time slice (quantum), then yields.
enum class SchedulerType { FCFS, RR };

// Plain data container holding every value loaded from config.txt.
// Created empty in main() and filled by ConfigLoader::loadFromFile.
// Passed by reference or const-reference to Scheduler and ScreenManager.
struct Config {
     // How many CPU worker threads the scheduler will run simultaneously.
    // Each core runs one process at a time. Range: 1-128.
    int numCpu = 0;

    // Which scheduling algorithm to use. Determined by the "scheduler" key.
    // Defaults to FCFS but is always overwritten by a successful load.
    SchedulerType scheduler = SchedulerType::FCFS;

    // For Round Robin only: how many CPU ticks each process gets before
    // being preempted and sent back to the ready queue. Ignored by FCFS.
    uint32_t quantumCycles = 0;

    // How often (in global CPU ticks) the scheduler auto-spawns a new batch process.
    // Example: batchProcessFreq=5 means one new process every 5 ticks.
    uint32_t batchProcessFreq = 0;

    // The minimum number of PRINT instructions a generated process will have.
    // Actual count is chosen randomly between minIns and maxIns.
    uint32_t minIns = 0;

    // The maximum number of PRINT instructions a generated process will have.
    // Must be >= minIns. If minIns == maxIns, every process has the same count.
    uint32_t maxIns = 0;

    // Extra CPU ticks to idle between executing consecutive instructions.
    // 0 = run as fast as possible (one instruction per tick).
    // Higher values slow things down and make timing easier to observe.
    uint32_t delayPerExec = 0;

    // OPTIONAL: number of processes to create immediately when scheduler-start runs.
    // 0 means rely on batch-process-freq alone (no instant burst at start).
    // Range: 1-128 if specified.
    uint32_t initialProcessCount = 0;

    // Set to true only after loadFromFile succeeds. Lets other code check
    // whether a Config object is in its default-zero state or actually loaded.
    bool loaded = false;
};

// Reads config.txt and fills a Config struct. Used exclusively by the
// "initialize" command handler in main.cpp.
class ConfigLoader {
public:
    // Opens `path` (always "config.txt"), parses every key-value pair,
    // validates all values, and writes the result into `out`.
    //
    // Returns true  + filled `out`          on success.
    // Returns false + message in `errorMessage` on any failure.
    //
    // Failure reasons (examples):
    //   "Could not open config file: config.txt"   — file missing
    //   "config.txt is missing num-cpu"            — required key absent
    //   "num-cpu must be in range [1, 128]."       — value out of range
    //   "min-ins cannot be greater than max-ins."  — cross-field rule violated
    //   "Unknown config parameter: badkey"         — unrecognized key in file
    static bool loadFromFile(const std::string& path, Config& out, std::string& errorMessage);
};
