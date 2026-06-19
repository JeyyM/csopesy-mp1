#pragma once

#include "Config.h"
#include "ProcessModel.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// FCFS scheduler with a real multi-threaded engine:
//   - one scheduler thread that dispatches ready processes to free cores
//   - one worker thread per CPU core that runs an assigned process to completion
class Scheduler {
public:
    ~Scheduler();

    void start(const Config& config);
    void stop();
    bool isEngineRunning() const { return engineRunning_.load(); }

    // Creates a process with the given number of PRINT instructions and queues it.
    std::shared_ptr<Process> createProcess(const std::string& name, int printInstructions);

    // Generates `count` processes (process01, process02, ...), each with
    // `printInstructions` PRINT commands. Returns how many were created.
    int generateBatch(int count, int printInstructions);

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    std::string buildStatusReport();

    int numCpu() const { return config_.numCpu; }

private:
    void schedulerLoop();
    void coreLoop(int coreId);
    void executeProcess(const std::shared_ptr<Process>& process, int coreId);

    std::shared_ptr<Process> createProcessLocked(const std::string& name, int printInstructions);

    Config config_{};
    std::atomic<bool> engineRunning_{false};

    std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<std::shared_ptr<Process>> readyQueue_;
    std::vector<std::shared_ptr<Process>> allProcesses_;
    std::vector<std::shared_ptr<Process>> coreCurrent_;

    std::thread schedulerThread_;
    std::vector<std::thread> coreThreads_;

    int nextId_ = 1;

    // Real time used to represent a single CPU cycle, so the demo is observable.
    static constexpr int kCycleMs = 30;
};
