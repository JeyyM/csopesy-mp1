#pragma once

#include <cstdint>
#include <string>

// Structure loaded from config.txt.
// Right now possible scheduler types are FCFS and RR.
enum class SchedulerType { FCFS, RR };

struct Config {
    int numCpu = 0;
    SchedulerType scheduler = SchedulerType::FCFS;
    uint32_t quantumCycles = 0;
    uint32_t batchProcessFreq = 0;

    // Minimum and maximum print instructions per process.
    uint32_t minIns = 0;
    uint32_t maxIns = 0;

    // Extra CPU cycles to wait between executing consecutive instructions.
    uint32_t delayPerExec = 0;

    // Optional: processes created immediately on scheduler-start (0 = batch-only).
    uint32_t initialProcessCount = 0;

    // True only after config.txt was parsed successfully.
    bool loaded = false;
};

class ConfigLoader {
public:
    static bool loadFromFile(const std::string& path, Config& out, std::string& errorMessage);
};
