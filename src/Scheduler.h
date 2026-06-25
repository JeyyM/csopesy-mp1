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

struct SchedulerStatusSnapshot {
    std::vector<std::shared_ptr<Process>> processes;
    int numCpu = 1;
};

//   1. A First-Come-First-Serve (FCFS) scheduler — processes are run
//      strictly in the order they arrive, no skipping the line.
//   2. A real multi-threaded engine:
//         - ONE "scheduler" thread whose only job is to look at the
//           waiting line of processes and the available CPU cores,
//           and decide "this process goes on that core now".
//         - ONE "worker" thread PER CPU core. Each worker thread just
//           sits there, and whenever the scheduler thread hands it a
//           process, it runs that process from start to finish.
//
// FCFS ordering comes from using a std::deque as a strict queue:
// new processes are always added to the BACK (push_back) and the
// scheduler always takes from the FRONT (pop_front) — so whoever
// arrived first is helped first, in order, with no jumping the queue.

// FCFS scheduler with a real multi-threaded engine:
//   - one scheduler thread that dispatches ready processes to free cores
//   - one worker thread per CPU core that runs an assigned process to completion
class Scheduler {
public:
    ~Scheduler();

    // STEP 1: Starts the whole multi-threaded engine.
    //   - Stores the config (how many CPU cores, etc.)
    //   - Spins up one worker thread per CPU core (coreLoop)
    //   - Spins up exactly one scheduler/dispatcher thread (schedulerLoop)
    void start(const Config& config);

    // STEP 2 (when the user types "exit" / "scheduler-stop"): Signals
    // every thread to stop and waits for them all to finish cleanly, so
    // the program doesn't quit while a thread is still mid-work.
    void stop();

    bool isEngineRunning() const { return engineRunning_.load(); }

    // Creates ONE new process with the given number of PRINT
    // instructions and adds it to the back of the FCFS ready queue.
    std::shared_ptr<Process> createProcess(const std::string& name, int printInstructions);

    // Convenience helper used for the test case in this assignment:
    // generates `count` processes named process01, process02, ... each
    // with `printInstructions` PRINT commands, and queues them all up
    // at once (e.g. 10 processes x 100 prints each).
    int generateBatch(int count, int printInstructions);

    // Person 4: adds p01-p05 on first scheduler-start (returns how many were added).
    int generateSchedulerDummyProcesses();

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    // Live process/core data for Person 3 report generation (screen -ls / report-util).
    SchedulerStatusSnapshot statusSnapshot() const;

    int numCpu() const { return config_.numCpu; }

private:

    // The dispatcher thread's main job: repeatedly check "is there a
    // waiting process AND a free core?" and if so, assign the process
    // at the FRONT of the queue (FCFS!) to that free core.
    void schedulerLoop();

    // One of these runs per CPU core. Each core thread just waits until
    // the scheduler hands it a process, then calls executeProcess() to
    // actually run it, then goes back to waiting for the next one.
    void coreLoop(int coreId);

    // THIS is where a process's instructions are actually carried out,
    // and where the PRINT command's required text-file output is
    // produced. Full step-by-step explanation is in Scheduler.cpp.
    void executeProcess(const std::shared_ptr<Process>& process, int coreId);

    // Internal helper shared by createProcess() and generateBatch():
    // builds a Process object, gives it `printInstructions` PRINT
    // commands, and pushes it onto the back of the ready queue.
    // Assumes the caller already holds mutex_.
    std::shared_ptr<Process> createProcessLocked(const std::string& name, int printInstructions,
                                                 bool includeMockPreview);

    void addMockInstructionPreview(const std::shared_ptr<Process>& process);
    std::shared_ptr<Process> addSchedulerDummyProcessLocked(const std::string& name, int id,
                                                            const std::string& timestamp,
                                                            int totalLines);
    void addSchedulerDummyProcessesLocked();
    bool processExistsLocked(const std::string& name) const;

    Config config_{};

    // True while the scheduler engine (all threads) should keep running.
    // Threads check this flag to know when to stop.
    std::atomic<bool> engineRunning_{false};

    // mutex_ + cv_ (condition variable) are the synchronization tools
    // that let the scheduler thread and the core threads safely share
    // the queue and the "which core is doing what" list, and let
    // threads sleep efficiently instead of constantly spinning/checking.
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    // The FCFS waiting line. New processes go in at the back
    // (push_back); the scheduler always takes the next one to run from
    // the front (pop_front) — first come, first served.
    std::deque<std::shared_ptr<Process>> readyQueue_;

    // Every process ever created (running, ready, or finished) — used
    // for things like "screen -ls" and "screen -r <name>" lookups.
    std::vector<std::shared_ptr<Process>> allProcesses_;

    // coreCurrent_[i] = the process currently assigned to core i, or
    // nullptr if that core is free. One slot per CPU core.
    std::vector<std::shared_ptr<Process>> coreCurrent_;

    std::thread schedulerThread_; // the single dispatcher thread
    std::vector<std::thread> coreThreads_; // one thread per CPU core

    int nextId_ = 1;
    bool schedulerDummyProcessesGenerated_ = false;

    // How many real milliseconds represent one simulated "CPU cycle".
    // This just slows the emulator down enough that a human can watch
    // "screen -ls" update in real time; it has no effect on the FCFS
    // ordering logic itself.
    static constexpr int kCycleMs = 30;
};
