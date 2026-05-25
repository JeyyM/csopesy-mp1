// Include Guard to prevent compilation errors
#pragma once

#include <cstdint>
// Provides fixed-width integer types such as uint32_t

#include <memory>
// Provides smart pointers: std::shared_ptr and std::unique_ptr.

#include <string>
// Provides std::string.

#include <unordered_map>
// Provides std::unordered_map, a hash table.
// For O(1) process lookup by name.

#include <vector>
// Provides std::vector, a dynamic array.

// Q1
// Attributes:
//   name - Process identifier (e.g. "process01")
//   id - Unique ID assigned at creation
//   creationTimestamp - Time when the process was created
//   assignedCore - Which CPU core the process runs on
//   currentInstructionLine - The instruction the process is currently executing
//   totalLinesOfCode - Total number of instructions in the process
//   status - Whether the process is Running or Finished
//   logs - Ordered list of log entries produced during execution
//   hasExitedAfterFinish - True once the user exits the finished process screen

struct ProcessLogEntry {
    std::string timestamp; // Time the log was produced
    int core = 0; // Core that created the entry
    std::string message; // Log text (e.g. "Hello world from process01!")
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
};

// Q2
// To track which instruction line is currently being executed we store two
// counters directly on the Process: currentInstructionLine and totalLinesOfCode.
// The helper function below advances the counter and marks the process Finished
// once all instructions have been executed. formatProcessSmi() then reads these
// fields to print the "current line number" in the console, e.g.:
//
//   Current instruction line: 42
//   Lines of code: 100
//
// When the process is done it prints "Finished instead.

struct ProcessTracker {
    // The two fields that represent execution progress
    uint32_t currentInstructionLine = 0;
    uint32_t totalLinesOfCode = 0;
    ProcessStatus status = ProcessStatus::Running;

    // Advance execution by one step (simulate one CPU tick)
    void advanceInstruction(uint32_t step = 1) {
        if (status == ProcessStatus::Finished) return;

        currentInstructionLine += step;
        if (currentInstructionLine >= totalLinesOfCode) {
            currentInstructionLine = totalLinesOfCode;
            status = ProcessStatus::Finished;
        }
    }

    // Format the execution progress for console display
    std::string formatProgress() const;
};


// QUESTION 3
// To store N processes and traverse them in linear time we use a vector
// of processes. It is then walked from start to finish. 

struct ProcessList {
    std::vector<std::shared_ptr<Process>> processes;

    // Add a new process
    void add(std::shared_ptr<Process> process) {
        processes.push_back(std::move(process));
    }

    // Example: print all running processes
    std::vector<const Process*> getRunning() const {
        std::vector<const Process*> result;
        for (const auto& p : processes) { // O(n) walk
            if (p->status == ProcessStatus::Running) {
                result.push_back(p.get());
            }
        }
        return result;
    }
};

// Q4
// To store N processes AND retrieve any single process in O(1) time we use a
// std::unordered_map keyed by process name. Hash-map lookup is O(1).

struct ProcessMap {
    std::unordered_map<std::string, std::shared_ptr<Process>> processes;

    // Insert - O(1) average
    void add(std::shared_ptr<Process> process) {
        processes[process->name] = std::move(process);
    }

    // O(1) average retrieval by name
    Process* find(const std::string& name) const {
        auto it = processes.find(name);
        if (it == processes.end()) return nullptr;
        return it->second.get();
    }
};
