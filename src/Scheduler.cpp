#include "Scheduler.h"

#include "InstructionEngine.h"
#include "OutputManager.h"
#include "TimeUtil.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

Scheduler::~Scheduler() {
    stopImmediate();
}

int Scheduler::instructionDelayMs() const {
    return config_.delayPerExec == 0 ? kFastCycleMs : kDefaultCycleMs;
}

int Scheduler::globalTickMs() const {
    return config_.delayPerExec == 0 ? kFastCycleMs : instructionDelayMs();
}

uint32_t Scheduler::cyclesPerInstruction() const {
    return 1U + config_.delayPerExec;
}

uint32_t Scheduler::quantumBudgetCycles() const {
    if (config_.scheduler == SchedulerType::FCFS) {
        return std::numeric_limits<uint32_t>::max();
    }
    return config_.quantumCycles;
}

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
    stopTickThread_.store(false);
    nextProcessNumber_ = 1;
    nextId_ = 1;

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

void Scheduler::wakeAllSleepingProcessesLocked() {
    for (const auto& entry : sleepingProcesses_) {
        entry.process->setSleepUntilCycle(0);
        entry.process->setStatus(ProcessStatus::Ready);
        readyQueue_.push_back(entry.process);
    }
    sleepingProcesses_.clear();
}

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

void Scheduler::tickLoop() {
    while (engineRunning_.load() && !stopTickThread_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(globalTickMs()));
        if (!engineRunning_.load() || stopTickThread_.load()) {
            break;
        }
        advanceGlobalCpuCycles(1);
    }
}

void Scheduler::advanceGlobalCpuCycles(uint32_t count) {
    if (count == 0) {
        return;
    }
    cpuCycles_.fetch_add(count);
    wakeSleepingProcesses();
    maybeSpawnBatchProcess();
}

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
        lastBatchSpawnCycle_ = cycles;
        spawnAutoBatchProcessLocked();
    }
    cv_.notify_all();
}

void Scheduler::addStandardProgram(const std::shared_ptr<Process>& process) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        process->addInstruction(InstructionType::Add, "ADD(x, x, 1)");
        process->addInstruction(InstructionType::Print, "\"Value from: \" + x");
        process->addInstruction(InstructionType::Add, "ADD(y, y, 1)");
        process->addInstruction(InstructionType::Print, "\"Value from: \" + y");
        process->addInstruction(InstructionType::Add, "ADD(z, z, 1)");
        process->addInstruction(InstructionType::Print, "\"Value from: \" + z");
    }
}

std::shared_ptr<Process> Scheduler::spawnAutoBatchProcessLocked() {
    std::ostringstream name;
    name << "process" << std::setw(2) << std::setfill('0') << nextProcessNumber_++;
    return createProcessLocked(name.str());
}

std::shared_ptr<Process> Scheduler::createProcessLocked(const std::string& name) {
    auto process = std::make_shared<Process>(nextId_++, name, TimeUtil::formatNow());
    addStandardProgram(process);
    allProcesses_.push_back(process);
    readyQueue_.push_back(process);
    return process;
}

void Scheduler::ensureEngineRunning(const Config& config) {
    if (!engineRunning_.load()) {
        start(config);
    }
}

std::shared_ptr<Process> Scheduler::createUserProcess(const std::string& name) {
    std::shared_ptr<Process> process;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process = createProcessLocked(name);
    }
    cv_.notify_all();
    return process;
}

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

void Scheduler::requeueProcess(const std::shared_ptr<Process>& process) {
    std::lock_guard<std::mutex> lock(mutex_);
    process->setAssignedCore(-1);
    process->setStatus(ProcessStatus::Ready);
    readyQueue_.push_back(process);
}

void Scheduler::markProcessSleeping(const std::shared_ptr<Process>& process,
                                      uint32_t sleepTicks) {
    const uint64_t wakeAt = cpuCycles_.load() + sleepTicks;
    std::lock_guard<std::mutex> lock(mutex_);
    process->setAssignedCore(-1);
    process->setStatus(ProcessStatus::Ready);
    process->setSleepUntilCycle(wakeAt);
    sleepingProcesses_.push_back({process, wakeAt});
}

ProcessSliceResult Scheduler::runInstructionTree(const std::shared_ptr<Process>& process,
                                                 const Instruction& instruction, int coreId,
                                                 std::ofstream& logFile, uint32_t& usedCycles,
                                                 uint32_t maxCycles, int forDepth) {
    if (instruction.type == InstructionType::For) {
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

    const uint32_t instructionCost = cyclesPerInstruction();
    if (usedCycles + instructionCost > maxCycles) {
        return ProcessSliceResult::Preempted;
    }

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

    if (step.relinquishCpu) {
        markProcessSleeping(process, step.sleepTicks);
        return ProcessSliceResult::Sleeping;
    }

    return ProcessSliceResult::Finished;
}

ProcessSliceResult Scheduler::runProcessSlice(const std::shared_ptr<Process>& process,
                                              int coreId, uint32_t maxCycles) {
    const std::string fileName = OutputManager::processLogPath(process->name());
    const bool isNewRun = process->currentLine() == 0;
    std::ofstream logFile;
    if (isNewRun) {
        OutputManager::ensureOutputsDirectory();
        logFile.open(fileName, std::ios::trunc);
        if (logFile.is_open()) {
            logFile << "Process name: " << process->name() << "\n";
            logFile << "Logs:\n\n";
        }
    } else {
        logFile.open(fileName, std::ios::app);
    }

    const int total = process->totalLines();
    const auto& instructions = process->instructions();
    uint32_t usedCycles = 0;

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

    process->setStatus(ProcessStatus::Finished);
    process->setFinishTimestamp(TimeUtil::formatNow());
    return ProcessSliceResult::Finished;
}

void Scheduler::schedulerLoop() {
    while (engineRunning_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
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

            for (std::size_t core = 0; core < coreCurrent_.size() && !readyQueue_.empty();
                 ++core) {
                if (coreCurrent_[core] == nullptr) {
                    std::shared_ptr<Process> next = readyQueue_.front();
                    readyQueue_.pop_front();
                    next->setAssignedCore(static_cast<int>(core));
                    next->setStatus(ProcessStatus::Running);
                    coreCurrent_[core] = next;
                }
            }
        }
        cv_.notify_all();
    }
}

void Scheduler::coreLoop(int coreId) {
    while (true) {
        std::shared_ptr<Process> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
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

        const ProcessSliceResult result =
            runProcessSlice(job, coreId, quantumBudgetCycles());

        if (result == ProcessSliceResult::Preempted) {
            requeueProcess(job);
        } else if (result == ProcessSliceResult::Aborted) {
            requeueProcess(job);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            coreCurrent_[coreId] = nullptr;
        }
        cv_.notify_all();
    }
}

SchedulerStatusSnapshot Scheduler::statusSnapshot() const {
    SchedulerStatusSnapshot snapshot;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.processes = allProcesses_;
    snapshot.numCpu = config_.numCpu < 1 ? 1 : config_.numCpu;
    snapshot.cpuCycles = cpuCycles_.load();
    return snapshot;
}
