#pragma once

// Scheduler.h
//
// Multi-threaded CPU scheduler for the CSOPESY emulator (FCFS and Round Robin).
// See Scheduler.cpp top-of-file "WORKED EXAMPLES" for flow diagrams and samples.
//
// Three background thread roles:
//   tickThread_       — advances global cpuCycles_; spawns batch processes; wakes SLEEP
//   schedulerThread_  — moves processes from readyQueue_ onto free cores
//   coreThreads_[i]   — runs instructions on core i until quantum expires or process ends
//
// Key data structures (all guarded by mutex_ except atomics):
//   readyQueue_       — FIFO of processes waiting for a core
//   coreCurrent_[i]   — process currently assigned to core i (or nullptr)
//   allProcesses_     — every process ever created (for screen -ls / findProcess)
//   sleepingProcesses_ — processes in SLEEP waiting for wakeAtCycle
//
// Called from: main.cpp, ScreenManager.cpp, ReportManager.cpp

#include "Config.h"
#include "MemoryManager.h"
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

// Point-in-time copy for screen -ls and report-util.
struct SchedulerStatusSnapshot {
    std::vector<std::shared_ptr<Process>> processes;  // creation order
    int numCpu = 1;
    uint64_t cpuCycles = 0;
};

// What happened after one runProcessSlice / runInstructionTree call.
enum class ProcessSliceResult {
    Finished,   // process completed all instructions (or instruction step done)
    Preempted,  // RR quantum exhausted — requeue to back of readyQueue_
    Sleeping,   // SLEEP instruction — moved to sleepingProcesses_
    Aborted     // engine stopped mid-slice — requeue
};

class Scheduler {
public:
    ~Scheduler();

    // Start worker threads with config. Clears queues and process lists.
    void start(const Config& config);

    // Hard stop: kill threads immediately, clear assignments.
    void stop();

    // Soft stop: stop batch spawn + tick; drain existing processes in background.
    void stopGracefully();

    bool isEngineRunning() const { return engineRunning_.load(); }
    bool isBatchGenerationActive() const { return batchGenerationActive_.load(); }

    // scheduler-start enables continuous spawn via maybeSpawnBatchProcess.
    void enableBatchGeneration();
    void disableBatchGeneration();

    // screen -s path: create named process with standard program, enqueue ready.
    std::shared_ptr<Process> createUserProcess(const std::string& name);

    // screen -s auto-starts engine if scheduler-start was not run yet.
    void ensureEngineRunning(const Config& config);

    // Create process01..processNN if names are free (initial batch).
    int generateBatch(int count);
    int generateInitialBatch(int count);

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    // Thread-safe copy of allProcesses_ + cpu stats for reports.
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
    void waitForAllProcessesFinished();
    void wakeAllSleepingProcessesLocked();
    void finishGracefulStop();
    void finalizeRunningProcessesLocked();
    void stopImmediate();
    void joinWorkerThreads();
    bool shouldExecuteWork() const;

    std::shared_ptr<Process> createProcessLocked(const std::string& name);
    void addStandardProgram(const std::shared_ptr<Process>& process);
    bool processExistsLocked(const std::string& name) const;
    std::shared_ptr<Process> spawnAutoBatchProcessLocked();

    uint32_t cyclesPerInstruction() const;
    uint32_t quantumBudgetCycles() const;
    int instructionDelayMs() const;
    int globalTickMs() const;

    // Writes memory_stamp_<cycle>.txt when cpuCycles_ is a multiple of quantumCycles.
    void maybeWriteMemoryStamp();

    Config config_{};
    MemoryManager memoryManager_{};

    std::atomic<bool> engineRunning_{false};
    std::atomic<bool> batchGenerationActive_{false};
    std::atomic<bool> stopTickThread_{false};
    std::atomic<bool> gracefulStopRequested_{false};
    std::atomic<uint64_t> cpuCycles_{0};

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<std::shared_ptr<Process>> readyQueue_;
    std::vector<SleepingEntry> sleepingProcesses_;
    std::vector<std::shared_ptr<Process>> allProcesses_;
    std::vector<std::shared_ptr<Process>> coreCurrent_;

    std::thread schedulerThread_;
    std::thread tickThread_;
    std::thread gracefulStopThread_;
    std::vector<std::thread> coreThreads_;

    int nextId_ = 1;
    int nextProcessNumber_ = 1;
    uint64_t lastBatchSpawnCycle_ = 0;
    uint64_t lastStampCycle_ = 0;      // last cycle at which a memory stamp was written

    std::mt19937 rng_{std::random_device{}()};

    static constexpr int kDefaultCycleMs = 30;
    static constexpr int kFastCycleMs = 1;
};
