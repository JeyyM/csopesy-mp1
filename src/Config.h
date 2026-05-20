#pragma once

#include <cstdint>
#include <string>

enum class SchedulerType { FCFS, RR };

struct Config {
    int numCpu = 4;
    SchedulerType scheduler = SchedulerType::RR;
    uint32_t quantumCycles = 5;
    uint32_t batchProcessFreq = 1;
    uint32_t minIns = 1000;
    uint32_t maxIns = 2000;
    uint32_t delayPerExec = 0;

    bool loaded = false;
};

class ConfigLoader {
public:
    static bool loadFromFile(const std::string& path, Config& out, std::string& errorMessage);
};
