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

// Point-in-time memory/CPU stats for process-smi and vmstat.
struct MemoryStatsSnapshot {
    bool configured = false;
    uint32_t totalMemory = 0;   // bytes
    uint32_t usedMemory = 0;    // bytes resident in frames
    uint32_t freeMemory = 0;    // bytes
    uint32_t totalFrames = 0;
    uint32_t usedFrames = 0;
    uint64_t pagedIn = 0;
    uint64_t pagedOut = 0;

    uint64_t activeCpuTicks = 0;
    uint64_t idleCpuTicks = 0;
    uint64_t totalCpuTicks = 0;

    int numCpu = 1;
    int coresUsed = 0;

    // One entry per non-finished process (for the process-smi listing).
    struct ProcMem {
        std::string name;
        uint32_t residentBytes = 0;
        uint32_t totalBytes = 0;
    };
    std::vector<ProcMem> processes;
};

// What happened after one runProcessSlice / runInstructionTree call.
enum class ProcessSliceResult {
    Finished,   // process completed all instructions (or instruction step done)
    Preempted,  // RR quantum exhausted — requeue to back of readyQueue_
    Sleeping,   // SLEEP instruction — moved to sleepingProcesses_
    Aborted,    // engine stopped mid-slice — requeue
    Terminated  // memory access violation — process is shut down (no requeue)
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

    // screen -s path: create named process with standard program + given memory
    // size, enqueue ready.
    std::shared_ptr<Process> createUserProcess(const std::string& name,
                                               uint32_t memoryBytes);

    // screen -c path: create named process with a caller-supplied program and
    // memory size, enqueue ready.
    std::shared_ptr<Process> createCustomProcess(const std::string& name,
                                                 uint32_t memoryBytes,
                                                 const std::vector<Instruction>& program);

    // screen -s auto-starts engine if scheduler-start was not run yet.
    void ensureEngineRunning(const Config& config);

    // Create process01..processNN if names are free (initial batch).
    int generateBatch(int count);
    int generateInitialBatch(int count);

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    // Thread-safe copy of allProcesses_ + cpu stats for reports.
    SchedulerStatusSnapshot statusSnapshot() const;

    // Thread-safe copy of memory + CPU-tick stats for process-smi and vmstat.
    MemoryStatsSnapshot memoryStatsSnapshot() const;

    bool memoryConfigured() const { return memoryManager_.isConfigured(); }

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

    std::shared_ptr<Process> createProcessLocked(const std::string& name,
                                                 uint32_t memoryBytes);
    void addStandardProgram(const std::shared_ptr<Process>& process);
    bool processExistsLocked(const std::string& name) const;
    std::shared_ptr<Process> spawnAutoBatchProcessLocked();

    // Rolls a power-of-two memory size in [minMemPerProc, maxMemPerProc].
    uint32_t rollProcessMemory();

    // Demand-paging: translate touched byte addresses to pages and fault them in.
    void applyMemoryAccesses(const std::shared_ptr<Process>& process,
                             const std::vector<uint32_t>& addresses);

    uint32_t cyclesPerInstruction() const;
    uint32_t quantumBudgetCycles() const;
    int instructionDelayMs() const;
    int globalTickMs() const;

    Config config_{};
    MemoryManager memoryManager_{};

    std::atomic<bool> engineRunning_{false};
    std::atomic<bool> batchGenerationActive_{false};
    std::atomic<bool> stopTickThread_{false};
    std::atomic<bool> gracefulStopRequested_{false};
    std::atomic<uint64_t> cpuCycles_{0};

    // CPU-tick accounting for vmstat (summed across all cores).
    std::atomic<uint64_t> activeCpuTicks_{0};  // core-ticks spent executing
    std::atomic<uint64_t> idleCpuTicks_{0};    // core-ticks spent idle

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

    std::mt19937 rng_{std::random_device{}()};

    static constexpr int kDefaultCycleMs = 30;
    static constexpr int kFastCycleMs = 1;
};
