// Scheduler.cpp
//
// Multi-threaded scheduler: assigns processes to CPU cores and runs their programs.
//
// ========== WORKED EXAMPLES (read this first) ==========
//
// Thread model (num-cpu = 2 example):
//
//   tickThread:        cpuCycles++ every globalTickMs -> maybeSpawnBatchProcess
//   schedulerLoop:     readyQueue_ -> coreCurrent_[0 or 1] when slot free
//   coreLoop(0):       runProcessSlice on job in coreCurrent_[0]
//   coreLoop(1):       runProcessSlice on job in coreCurrent_[1]
//
// 1) scheduler-start + batch-process-freq 1
//      Every cpu cycle tick spawns process01, process02, ... into readyQueue_.
//
// 2) screen -s proc-01
//      createUserProcess("proc-01") -> createProcessLocked -> readyQueue_
//      OutputManager creates outputs/proc-01.txt immediately.
//
// 3) One RR time slice (quantum-cycles = 20)
//      coreLoop runs up to 20 instruction-cycles via runProcessSlice.
//      If process not done -> Preempted -> requeueProcess (back of readyQueue_).
//
// 4) FCFS vs RR quantum
//      FCFS: quantumBudgetCycles() = max uint32 -> run until process finishes.
//      RR:   quantumBudgetCycles() = config.quantumCycles -> preempt after budget.
//
// 5) scheduler-stop
//      disableBatchGeneration + stop tick -> no new spawns.
//      Existing readyQueue_ + running processes continue until all Finished.
//
// 6) screen -ls
//      statusSnapshot() copies allProcesses_ for ReportManager.
//
// ========================================================

#include "Scheduler.h"

#include "InstructionEngine.h"
#include "OutputManager.h"
#include "TimeUtil.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <random>
#include <sstream>
#include <thread>

Scheduler::~Scheduler() {
    stopImmediate();
}

// ---------------------------------------------------------------------------
// Timing helpers (from config.txt)
// ---------------------------------------------------------------------------

// Real-time delay per instruction when delay-per-exec > 0.
int Scheduler::instructionDelayMs() const {
    return config_.delayPerExec == 0 ? kFastCycleMs : kDefaultCycleMs;
}

// How often tickLoop wakes to increment cpuCycles_.
int Scheduler::globalTickMs() const {
    return config_.delayPerExec == 0 ? kFastCycleMs : instructionDelayMs();
}

// CPU cycles consumed by one instruction (1 + delay-per-exec).
uint32_t Scheduler::cyclesPerInstruction() const {
    return 1U + config_.delayPerExec;
}

// Max cycles one coreLoop slice may consume before RR preemption.
// Example: RR quantum-cycles 20 -> maxCycles=20. FCFS -> unlimited.
uint32_t Scheduler::quantumBudgetCycles() const {
    if (config_.scheduler == SchedulerType::FCFS) {
        return std::numeric_limits<uint32_t>::max();
    }
    return config_.quantumCycles;
}

// ---------------------------------------------------------------------------
// Lifecycle: start / stop
// ---------------------------------------------------------------------------

// Step 1: Join any prior graceful-stop thread. Step 2: Load config, reset counters.
// Step 3: Clear queues under mutex. Step 4: Set engineRunning, spawn threads.
void Scheduler::start(const Config& config) {
    if (gracefulStopThread_.joinable()) {
        gracefulStopThread_.join();
    }

    if (engineRunning_.load()) {
        return;
    }

    config_ = config;
    if (config_.numCpu < 1) {
        config_.numCpu = 1;
    }

    cpuCycles_.store(0);
    lastBatchSpawnCycle_ = 0;
    lastStampCycle_ = 0;
    stopTickThread_.store(false);
    nextProcessNumber_ = 1;
    nextId_ = 1;

    // Configure memory manager if the new MCO2 parameters are present.
    if (config_.maxOverallMem > 0 && config_.memPerProc > 0) {
        memoryManager_.configure(config_.maxOverallMem, config_.memPerProc);
        // Ensure the output directory for memory stamps exists.
        std::filesystem::create_directories("memory-stamps");
    } else {
        memoryManager_.configure(0, 0);  // disabled
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        coreCurrent_.assign(config_.numCpu, nullptr);
        readyQueue_.clear();
        sleepingProcesses_.clear();
        allProcesses_.clear();
    }

    engineRunning_.store(true);

    coreThreads_.clear();
    for (int core = 0; core < config_.numCpu; ++core) {
        coreThreads_.emplace_back(&Scheduler::coreLoop, this, core);
    }
    schedulerThread_ = std::thread(&Scheduler::schedulerLoop, this);
    tickThread_ = std::thread(&Scheduler::tickLoop, this);
}

void Scheduler::stop() {
    stopImmediate();
}

// Join tick, dispatcher, and all core worker threads (blocks until they exit).
void Scheduler::joinWorkerThreads() {
    if (tickThread_.joinable()) {
        tickThread_.join();
    }
    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
    for (std::thread& worker : coreThreads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    coreThreads_.clear();
}

// Hard shutdown: stop batch, stop tick, engineRunning=false, join all threads.
void Scheduler::stopImmediate() {
    disableBatchGeneration();
    stopTickThread_.store(true);
    gracefulStopRequested_.store(false);

    const bool wasRunning = engineRunning_.exchange(false);
    cv_.notify_all();

    if (gracefulStopThread_.joinable()) {
        gracefulStopThread_.join();
    }

    joinWorkerThreads();

    if (wasRunning) {
        std::lock_guard<std::mutex> lock(mutex_);
        finalizeRunningProcessesLocked();
    }
}

// Soft shutdown (scheduler-stop): stop spawning, let processes finish in background.
// Step 1: Disable batch + tick. Step 2: Wake sleepers. Step 3: Background finishGracefulStop.
void Scheduler::stopGracefully() {
    if (!engineRunning_.load()) {
        return;
    }
    if (gracefulStopRequested_.exchange(true)) {
        return;
    }

    disableBatchGeneration();
    stopTickThread_.store(true);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        wakeAllSleepingProcessesLocked();
    }
    cv_.notify_all();

    if (gracefulStopThread_.joinable()) {
        gracefulStopThread_.join();
    }
    gracefulStopThread_ = std::thread(&Scheduler::finishGracefulStop, this);
}

// Background thread body: wait until all Finished, then join workers and stop engine.
void Scheduler::finishGracefulStop() {
    waitForAllProcessesFinished();

    engineRunning_.store(false);
    cv_.notify_all();
    joinWorkerThreads();
    gracefulStopRequested_.store(false);
}

bool Scheduler::shouldExecuteWork() const {
    return engineRunning_.load();
}

// Poll every 10ms until every process in allProcesses_ has status Finished.
void Scheduler::waitForAllProcessesFinished() {
    for (;;) {
        if (!engineRunning_.load()) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool allFinished = true;
            for (const auto& process : allProcesses_) {
                if (process->status() != ProcessStatus::Finished) {
                    allFinished = false;
                    break;
                }
            }
            if (allFinished) {
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Move all sleeping processes back to readyQueue_ (used during graceful stop).
void Scheduler::wakeAllSleepingProcessesLocked() {
    for (const auto& entry : sleepingProcesses_) {
        entry.process->setSleepUntilCycle(0);
        entry.process->setStatus(ProcessStatus::Ready);
        readyQueue_.push_back(entry.process);
    }
    sleepingProcesses_.clear();
}

// On hard stop: release cores and clear ready queue.
void Scheduler::finalizeRunningProcessesLocked() {
    for (std::shared_ptr<Process>& slot : coreCurrent_) {
        if (slot && slot->status() == ProcessStatus::Running) {
            slot->setAssignedCore(-1);
            slot->setStatus(ProcessStatus::Ready);
        }
        slot = nullptr;
    }
    readyQueue_.clear();
}

void Scheduler::enableBatchGeneration() {
    batchGenerationActive_.store(true);
    cv_.notify_all();
}

void Scheduler::disableBatchGeneration() {
    batchGenerationActive_.store(false);
}

// ---------------------------------------------------------------------------
// Global CPU tick (batch spawn + SLEEP wake)
// ---------------------------------------------------------------------------

// Step 1: Sleep globalTickMs. Step 2: If still running, advanceGlobalCpuCycles(1).
void Scheduler::tickLoop() {
    while (engineRunning_.load() && !stopTickThread_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(globalTickMs()));
        if (!engineRunning_.load() || stopTickThread_.load()) {
            break;
        }
        advanceGlobalCpuCycles(1);
    }
}

// Step 1: Increment cpuCycles_. Step 2: Wake sleepers due. Step 3: Maybe spawn batch process.
// Step 4: Maybe write memory stamp file.
void Scheduler::advanceGlobalCpuCycles(uint32_t count) {
    if (count == 0) {
        return;
    }
    cpuCycles_.fetch_add(count);
    wakeSleepingProcesses();
    maybeSpawnBatchProcess();
    maybeWriteMemoryStamp();
}

// Step 1: Compare wakeAtCycle to now. Step 2: Move due processes to readyQueue_.
void Scheduler::wakeSleepingProcesses() {
    const uint64_t now = cpuCycles_.load();
    bool wokeAny = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = sleepingProcesses_.begin(); it != sleepingProcesses_.end();) {
            if (it->wakeAtCycle <= now) {
                it->process->setSleepUntilCycle(0);
                it->process->setStatus(ProcessStatus::Ready);
                readyQueue_.push_back(it->process);
                it = sleepingProcesses_.erase(it);
                wokeAny = true;
            } else {
                ++it;
            }
        }
    }
    if (wokeAny) {
        cv_.notify_all();
    }
}

// Example: batch-process-freq 1 -> spawn every tick at cycles 1,2,3,...
// Example: batch-process-freq 2 -> spawn at cycles 2,4,6,...
// Backpressure: skip spawning when unfinished processes already exceed the cap,
// so a single CPU is not buried under thousands of queued 1000-instruction jobs.
void Scheduler::maybeSpawnBatchProcess() {
    if (!batchGenerationActive_.load()) {
        return;
    }

    const uint64_t cycles = cpuCycles_.load();
    if (config_.batchProcessFreq < 1 || cycles % config_.batchProcessFreq != 0) {
        return;
    }
    if (cycles == lastBatchSpawnCycle_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Cap unfinished processes at 64× the number of cores (minimum 64).
        // This prevents the ready queue from growing unboundedly when spawn rate
        // exceeds execution rate (e.g. 1 CPU, 1000-instruction processes, freq=1).
        const size_t maxActive = std::max(64, config_.numCpu * 64);
        const size_t unfinished = std::count_if(
            allProcesses_.begin(), allProcesses_.end(),
            [](const std::shared_ptr<Process>& p) {
                return p->status() != ProcessStatus::Finished;
            });
        if (unfinished >= maxActive) {
            return;  // wait for existing work to progress before adding more
        }

        lastBatchSpawnCycle_ = cycles;
        spawnAutoBatchProcessLocked();
    }
    cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Memory stamp writer
// ---------------------------------------------------------------------------

// Fires every quantum-cycles CPU ticks (when cpuCycles_ is a multiple of quantumCycles).
// Writes memory_stamp_<cycle>.txt with a snapshot of the current memory layout.
// Format (top-down, highest address first):
//
//   Timestamp: (MM/DD/YYYY HH:MM:SSAM)
//   Number of processes in memory: N
//   Total external fragmentation in KB: F
//
//   ----end---- = 16384
//
//   <upper addr>
//   <name>
//   <lower addr>
//   ...
//   ----start---- = 0
void Scheduler::maybeWriteMemoryStamp() {
    if (!memoryManager_.isConfigured()) {
        return;
    }

    const uint64_t cycles = cpuCycles_.load();
    if (cycles == 0 || cycles % config_.quantumCycles != 0) {
        return;
    }
    if (cycles == lastStampCycle_) {
        return;
    }
    lastStampCycle_ = cycles;

    // Take a snapshot under the scheduler mutex so the printout is consistent.
    std::vector<MemoryManager::Allocation> snapshot;
    int count = 0;
    uint32_t fragBytes = 0;
    uint32_t totalMem = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot   = memoryManager_.snapshotDescending();
        count      = memoryManager_.allocatedCount();
        fragBytes  = memoryManager_.externalFragmentationBytes();
        totalMem   = memoryManager_.totalMemory();
    }

    // Write the file outside the mutex so I/O latency doesn't stall the scheduler.
    std::ostringstream oss;
    oss << "memory-stamps/memory_stamp_" << std::setfill('0') << std::setw(2) << cycles << ".txt";
    const std::string filename = oss.str();
    std::ofstream file(filename);
    if (!file.is_open()) {
        return;
    }

    file << "Timestamp: (" << TimeUtil::formatNow() << ")\n";
    file << "Number of processes in memory: " << count << "\n";
    file << "Total external fragmentation in KB: " << fragBytes << "\n";
    file << "\n";
    file << "----end---- = " << totalMem << "\n";

    for (const auto& alloc : snapshot) {
        file << "\n";
        file << (alloc.base + alloc.size) << "\n";  // upper limit (exclusive)
        file << alloc.processName << "\n";
        file << alloc.base << "\n";                  // lower limit (inclusive)
    }

    file << "\n----start---- = 0\n";
}

// ---------------------------------------------------------------------------
// Process creation
// ---------------------------------------------------------------------------

// Builds a program with exactly totalInstructions instructions, drawn uniformly
// from [config_.minIns, config_.maxIns]. Pattern: PRINT, ADD, PRINT, ADD, ...
// The ADD amount is randomised between 1 and 10 for each ADD instruction.
// Only variable x is used (initialised to 0 at process creation).
// Called under mutex_ so rng_ access is safe.
void Scheduler::addStandardProgram(const std::shared_ptr<Process>& process) {
    const uint32_t minIns = std::max(1u, config_.minIns);
    const uint32_t maxIns = std::max(minIns, config_.maxIns);

    uint32_t totalInstructions = minIns;
    if (maxIns > minIns) {
        std::uniform_int_distribution<uint32_t> dist(minIns, maxIns);
        totalInstructions = dist(rng_);
    }

    std::uniform_int_distribution<int> addAmountDist(1, 10);

    // Alternating: PRINT("Value from: " +x), ADD(x, x, N), PRINT, ADD, ...
    for (uint32_t i = 0; i < totalInstructions; ++i) {
        if (i % 2 == 0) {
            process->addInstruction(InstructionType::Print, "\"Value from: \" + x");
        } else {
            const int amount = addAmountDist(rng_);
            process->addInstruction(InstructionType::Add,
                                    "ADD(x, x, " + std::to_string(amount) + ")");
        }
    }
}

// Auto name: process01, process02, ... then createProcessLocked.
std::shared_ptr<Process> Scheduler::spawnAutoBatchProcessLocked() {
    std::ostringstream name;
    name << "process" << std::setw(2) << std::setfill('0') << nextProcessNumber_++;
    return createProcessLocked(name.str());
}

// Step 1: new Process. Step 2: addStandardProgram. Step 3: outputs/<name>.txt header.
// Step 4: append to allProcesses_ and readyQueue_. Caller must hold mutex_.
std::shared_ptr<Process> Scheduler::createProcessLocked(const std::string& name) {
    auto process = std::make_shared<Process>(nextId_++, name, TimeUtil::formatNow());
    addStandardProgram(process);
    OutputManager::initializeProcessLog(name);
    allProcesses_.push_back(process);
    readyQueue_.push_back(process);
    return process;
}

void Scheduler::ensureEngineRunning(const Config& config) {
    if (!engineRunning_.load()) {
        start(config);
    }
}

// screen -s entry: lock, createProcessLocked, notify cores.
std::shared_ptr<Process> Scheduler::createUserProcess(const std::string& name) {
    std::shared_ptr<Process> process;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process = createProcessLocked(name);
    }
    cv_.notify_all();
    return process;
}

// Create process01..process0N for initial-process-count in config.
int Scheduler::generateBatch(int count) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < count; ++i) {
            std::ostringstream name;
            name << "process" << std::setw(2) << std::setfill('0') << (i + 1);
            if (!processExistsLocked(name.str())) {
                createProcessLocked(name.str());
            }
        }
        nextProcessNumber_ = count + 1;
    }
    cv_.notify_all();
    return count;
}

int Scheduler::generateInitialBatch(int count) {
    return generateBatch(count);
}

bool Scheduler::processExistsLocked(const std::string& name) const {
    for (const auto& process : allProcesses_) {
        if (process->name() == name) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Process> Scheduler::findProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& process : allProcesses_) {
        if (process->name() == name) {
            return process;
        }
    }
    return nullptr;
}

bool Scheduler::processExists(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return processExistsLocked(name);
}

// RR preemption or abort: clear core assignment, push to back of readyQueue_.
void Scheduler::requeueProcess(const std::shared_ptr<Process>& process) {
    std::lock_guard<std::mutex> lock(mutex_);
    process->setAssignedCore(-1);
    process->setStatus(ProcessStatus::Ready);
    readyQueue_.push_back(process);
}

// SLEEP: park until cpuCycles_ reaches wakeAt = now + sleepTicks.
void Scheduler::markProcessSleeping(const std::shared_ptr<Process>& process,
                                    uint32_t sleepTicks) {
    const uint64_t wakeAt = cpuCycles_.load() + sleepTicks;
    std::lock_guard<std::mutex> lock(mutex_);
    process->setAssignedCore(-1);
    process->setStatus(ProcessStatus::Ready);
    process->setSleepUntilCycle(wakeAt);
    sleepingProcesses_.push_back({process, wakeAt});
}

// ---------------------------------------------------------------------------
// Instruction execution (one process on one core)
// ---------------------------------------------------------------------------

// Run one instruction (or expand FOR). Respects quantum via usedCycles vs maxCycles.
//
// Example: maxCycles=20, each instruction costs 1 -> up to 20 instructions then Preempted.
ProcessSliceResult Scheduler::runInstructionTree(const std::shared_ptr<Process>& process,
                                                 const Instruction& instruction, int coreId,
                                                 std::ofstream& logFile, uint32_t& usedCycles,
                                                 uint32_t maxCycles, int forDepth) {
    if (instruction.type == InstructionType::For) {
        // Step 1: Parse FOR body. Step 2: Run each iteration/instruction recursively.
        if (forDepth >= 3) {
            return ProcessSliceResult::Finished;
        }
        std::vector<Instruction> body;
        int repeats = 0;
        if (!InstructionEngine::parseForInstruction(instruction.arg, body, repeats)) {
            return ProcessSliceResult::Finished;
        }
        for (int iteration = 0; iteration < repeats; ++iteration) {
            for (const Instruction& bodyInstruction : body) {
                ProcessSliceResult nested = runInstructionTree(
                    process, bodyInstruction, coreId, logFile, usedCycles, maxCycles,
                    forDepth + 1);
                if (nested != ProcessSliceResult::Finished) {
                    return nested;
                }
            }
        }
        return ProcessSliceResult::Finished;
    }

    // Step 3: Check quantum budget before running next leaf instruction.
    const uint32_t instructionCost = cyclesPerInstruction();
    if (usedCycles + instructionCost > maxCycles) {
        return ProcessSliceResult::Preempted;
    }

    // Step 4: Execute via InstructionEngine; append log to process + file.
    InstructionEngine::ExecuteResult step =
        InstructionEngine::execute(*process, instruction, coreId);

    if (step.producedLog) {
        process->appendLog(step.logLine);
        if (logFile.is_open()) {
            logFile << step.logLine << "\n";
            logFile.flush();
        }
    }

    usedCycles += instructionCost;
    const int delayMs = instructionDelayMs() * static_cast<int>(instructionCost);
    if (delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // Step 5: SLEEP relinquishes core until wakeSleepingProcesses moves it back.
    if (step.relinquishCpu) {
        markProcessSleeping(process, step.sleepTicks);
        return ProcessSliceResult::Sleeping;
    }

    return ProcessSliceResult::Finished;
}

// Run one quantum slice: from currentLine until done, preempted, sleeping, or aborted.
//
// Example: process at line 40/600, quantum 20 -> runs lines 40-59, returns Preempted,
//          currentLine stays 60 on next entry (or line+1 after full instruction).
ProcessSliceResult Scheduler::runProcessSlice(const std::shared_ptr<Process>& process,
                                              int coreId, uint32_t maxCycles) {
    const std::string fileName = OutputManager::processLogPath(process->name());
    std::ofstream logFile(fileName, std::ios::app);

    const int total = process->totalLines();
    const auto& instructions = process->instructions();
    uint32_t usedCycles = 0;

    // Step 1: Loop program lines starting at currentLine.
    for (int line = process->currentLine(); line < total; ++line) {
        if (!engineRunning_.load()) {
            return ProcessSliceResult::Aborted;
        }

        const Instruction& instruction = instructions[line];
        ProcessSliceResult outcome = runInstructionTree(process, instruction, coreId, logFile,
                                                          usedCycles, maxCycles, 0);

        if (outcome == ProcessSliceResult::Preempted) {
            return ProcessSliceResult::Preempted;
        }
        if (outcome == ProcessSliceResult::Sleeping) {
            process->setCurrentLine(line + 1);
            return ProcessSliceResult::Sleeping;
        }
        if (outcome == ProcessSliceResult::Aborted) {
            return ProcessSliceResult::Aborted;
        }

        process->setCurrentLine(line + 1);
    }

    // Step 2: All lines done — mark Finished for screen -ls / screen -r rules.
    process->setStatus(ProcessStatus::Finished);
    process->setFinishTimestamp(TimeUtil::formatNow());
    return ProcessSliceResult::Finished;
}

// ---------------------------------------------------------------------------
// Dispatcher and core worker threads
// ---------------------------------------------------------------------------

// Assigns processes from readyQueue_ front to any nullptr slot in coreCurrent_.
void Scheduler::schedulerLoop() {
    while (engineRunning_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Step 1: Wait until shutdown OR (ready work AND free core).
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                if (!engineRunning_.load()) {
                    return true;
                }
                if (!readyQueue_.empty()) {
                    for (const auto& slot : coreCurrent_) {
                        if (slot == nullptr) {
                            return true;
                        }
                    }
                }
                return false;
            });

            if (!engineRunning_.load()) {
                break;
            }

            // Step 2: Pop from readyQueue_ front onto each idle core.
            // Memory admission gate: if MemoryManager is active, a process must
            // receive a memory allocation before it may run.  If memory is full,
            // the process is moved to the tail and the core stays idle this round.
            for (std::size_t core = 0; core < coreCurrent_.size() && !readyQueue_.empty();
                 ++core) {
                if (coreCurrent_[core] == nullptr) {
                    std::shared_ptr<Process> next = readyQueue_.front();
                    readyQueue_.pop_front();

                    if (memoryManager_.isConfigured() && next->memoryBase() == -1) {
                        const int base = memoryManager_.allocate(next->name());
                        if (base == -1) {
                            // Memory full — return process to tail of ready queue.
                            readyQueue_.push_back(next);
                            continue;
                        }
                        next->setMemoryBase(base);
                    }

                    next->setAssignedCore(static_cast<int>(core));
                    next->setStatus(ProcessStatus::Running);
                    coreCurrent_[core] = next;
                }
            }
        }
        cv_.notify_all();
    }
}

// One thread per CPU core: wait for assignment, run slice, requeue if preempted, release core.
void Scheduler::coreLoop(int coreId) {
    while (true) {
        std::shared_ptr<Process> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Step 1: Block until this core has a process or engine is shutting down.
            cv_.wait(lock, [this, coreId] {
                return !engineRunning_.load() || coreCurrent_[coreId] != nullptr;
            });

            if (!engineRunning_.load() && coreCurrent_[coreId] == nullptr) {
                return;
            }
            job = coreCurrent_[coreId];
        }

        if (!job) {
            continue;
        }

        // Step 2: Run up to quantumBudgetCycles() worth of instructions.
        const ProcessSliceResult result =
            runProcessSlice(job, coreId, quantumBudgetCycles());

        // Step 3: Preempted/aborted -> back of ready queue for another turn.
        if (result == ProcessSliceResult::Preempted) {
            requeueProcess(job);
        } else if (result == ProcessSliceResult::Aborted) {
            requeueProcess(job);
        }

        // Step 4: Clear core slot. If the process just finished, also release
        // its memory so the MemoryManager can reuse that address range.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (result == ProcessSliceResult::Finished && memoryManager_.isConfigured()) {
                memoryManager_.release(job->name());
                job->setMemoryBase(-1);
            }
            coreCurrent_[coreId] = nullptr;
        }
        cv_.notify_all();
    }
}

// Copy all processes + stats for screen -ls (creation order preserved).
SchedulerStatusSnapshot Scheduler::statusSnapshot() const {
    SchedulerStatusSnapshot snapshot;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.processes = allProcesses_;
    snapshot.numCpu = config_.numCpu < 1 ? 1 : config_.numCpu;
    snapshot.cpuCycles = cpuCycles_.load();
    return snapshot;
}
