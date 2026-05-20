#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Placeholder instruction types for future backend (not executed yet).
// PRINT(msg)       - console output visible only on attached screen
// DECLARE(var,val) - uint16 variable declaration
// ADD / SUBTRACT   - arithmetic on variables or literals
// SLEEP(X)         - sleep X uint8 CPU ticks
// FOR([...], n)    - nested loops up to 3 levels

struct ProcessLogEntry {
    std::string timestamp;
    int core = 0;
    std::string message;
};

enum class ProcessStatus { Running, Finished };

struct Process {
    std::string name;
    int id = 0;
    std::string creationTimestamp;
    int assignedCore = 0;
    uint32_t currentInstructionLine = 0;
    uint32_t totalLinesOfCode = 0;
    ProcessStatus status = ProcessStatus::Running;
    std::vector<ProcessLogEntry> logs;
    bool hasExitedAfterFinish = false;

    void initializeDummy(const std::string& processName, int processId,
                         uint32_t minIns, uint32_t maxIns, int numCpu);
    void advanceDummyState(int numCpu);
    std::string formatProcessSmi() const;
};
