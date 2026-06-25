#pragma once

#include "Config.h"
#include "ProcessModel.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

struct SchedulerStatusSnapshot {
    std::vector<std::shared_ptr<Process>> processes;
    int numCpu = 1;
    uint64_t cpuCycles = 0;
};

enum class ProcessSliceResult { Finished, Preempted, Sleeping, Aborted };

// Multi-threaded scheduler supporting FCFS and Round Robin.
//   - one dispatcher thread assigns ready processes to free cores
//   - one worker thread per CPU core executes instruction slices
//   - one global tick thread advances cpuCycles (batch-process-freq, SLEEP)
class Scheduler {
public:
    ~Scheduler();

    void start(const Config& config);
    void stop();
    void stopGracefully();

    bool isEngineRunning() const { return engineRunning_.load(); }
    bool isBatchGenerationActive() const { return batchGenerationActive_.load(); }

    void enableBatchGeneration();
    void disableBatchGeneration();

    std::shared_ptr<Process> createProcess(const std::string& name, int printInstructions);
    std::shared_ptr<Process> createUserProcess(const std::string& name, int printInstructions);
    void ensureEngineRunning(const Config& config);
    int generateBatch(int count, int printInstructions);
    int generateInitialBatch(int count, int printInstructions);

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    SchedulerStatusSnapshot statusSnapshot() const;
    int numCpu() const { return config_.numCpu; }
    uint64_t cpuCycles() const { return cpuCycles_.load(); }
    SchedulerType schedulerType() const { return config_.scheduler; }

private:
    struct SleepingEntry {
        std::shared_ptr<Process> process;
        uint64_t wakeAtCycle = 0;
    };

    void schedulerLoop();
    void coreLoop(int coreId);
    void tickLoop();

    ProcessSliceResult runProcessSlice(const std::shared_ptr<Process>& process, int coreId,
                                       uint32_t maxCycles);
    ProcessSliceResult runInstructionTree(const std::shared_ptr<Process>& process,
                                          const Instruction& instruction, int coreId,
                                          std::ofstream& logFile, uint32_t& usedCycles,
                                          uint32_t maxCycles, int forDepth);
    void requeueProcess(const std::shared_ptr<Process>& process);
    void markProcessSleeping(const std::shared_ptr<Process>& process, uint32_t sleepTicks);

    void advanceGlobalCpuCycles(uint32_t count);
    void wakeSleepingProcesses();
    void maybeSpawnBatchProcess();
    void seedPracticeBatchProcessesLocked();
    bool usesPracticeSeedLayout() const;
    bool usesStandardForProgram() const;
    std::shared_ptr<Process> createStandardForProcessLocked(const std::string& name);
    void waitForAllProcessesFinished();
    void finalizeRunningProcessesLocked();
    void stopImmediate();
    bool shouldExecuteWork() const;

    std::shared_ptr<Process> createProcessLocked(const std::string& name, int printInstructions,
                                                 bool includeMockPreview);
    void addMockInstructionPreview(const std::shared_ptr<Process>& process);
    bool processExistsLocked(const std::string& name) const;
    std::shared_ptr<Process> spawnAutoBatchProcessLocked();
    int randomInstructionCountLocked();

    uint32_t cyclesPerInstruction() const;
    uint32_t quantumBudgetCycles() const;
    int instructionDelayMs() const;
    int globalTickMs() const;

    Config config_{};

    std::atomic<bool> engineRunning_{false};
    std::atomic<bool> batchGenerationActive_{false};
    std::atomic<bool> stopTickThread_{false};
    std::atomic<uint64_t> cpuCycles_{0};

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<std::shared_ptr<Process>> readyQueue_;
    std::vector<SleepingEntry> sleepingProcesses_;
    std::vector<std::shared_ptr<Process>> allProcesses_;

    std::vector<std::shared_ptr<Process>> coreCurrent_;

    std::thread schedulerThread_;
    std::thread tickThread_;
    std::vector<std::thread> coreThreads_;

    int nextId_ = 1;
    int nextProcessNumber_ = 1;
    uint64_t lastBatchSpawnCycle_ = 0;
    bool practiceBatchSeeded_ = false;

    std::mt19937 rng_{std::random_device{}()};

    static constexpr int kDefaultCycleMs = 30;
    static constexpr int kFastCycleMs = 1;
    static constexpr int kPracticeProcessCount = 8;
};
