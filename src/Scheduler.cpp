#include "Scheduler.h"

#include "TimeUtil.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

}  // namespace

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start(const Config& config) {
    if (engineRunning_.load()) {
        return;
    }

    config_ = config;
    if (config_.numCpu < 1) {
        config_.numCpu = 1;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        coreCurrent_.assign(config_.numCpu, nullptr);
    }

    engineRunning_.store(true);

    coreThreads_.clear();
    for (int core = 0; core < config_.numCpu; ++core) {
        coreThreads_.emplace_back(&Scheduler::coreLoop, this, core);
    }
    schedulerThread_ = std::thread(&Scheduler::schedulerLoop, this);
}

void Scheduler::stop() {
    if (!engineRunning_.exchange(false)) {
        return;
    }

    cv_.notify_all();

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

std::shared_ptr<Process> Scheduler::createProcessLocked(const std::string& name,
                                                        int printInstructions) {
    auto process = std::make_shared<Process>(nextId_++, name, TimeUtil::formatNow());
    for (int i = 0; i < printInstructions; ++i) {
        process->addPrintInstruction();
    }
    allProcesses_.push_back(process);
    readyQueue_.push_back(process);
    return process;
}

std::shared_ptr<Process> Scheduler::createProcess(const std::string& name,
                                                  int printInstructions) {
    std::shared_ptr<Process> process;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process = createProcessLocked(name, printInstructions);
    }
    cv_.notify_all();
    return process;
}

int Scheduler::generateBatch(int count, int printInstructions) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < count; ++i) {
            std::ostringstream name;
            name << "process" << std::setw(2) << std::setfill('0') << (i + 1);
            createProcessLocked(name.str(), printInstructions);
        }
    }
    cv_.notify_all();
    return count;
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
    return findProcess(name) != nullptr;
}

void Scheduler::schedulerLoop() {
    while (engineRunning_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                if (!engineRunning_.load()) {
                    return true;
                }
                if (readyQueue_.empty()) {
                    return false;
                }
                for (const auto& slot : coreCurrent_) {
                    if (slot == nullptr) {
                        return true;
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

        if (job) {
            executeProcess(job, coreId);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                coreCurrent_[coreId] = nullptr;
            }
            cv_.notify_all();
        }
    }
}

void Scheduler::executeProcess(const std::shared_ptr<Process>& process, int coreId) {
    const std::string fileName = process->name() + ".txt";
    std::ofstream logFile(fileName, std::ios::trunc);
    if (logFile.is_open()) {
        logFile << "Process name: " << process->name() << "\n";
        logFile << "Logs:\n\n";
    }

    const int total = process->totalLines();
    const auto& instructions = process->instructions();

    for (int line = process->currentLine(); line < total; ++line) {
        if (!engineRunning_.load()) {
            return;  // Abort promptly on shutdown; leaves the process unfinished.
        }

        const Instruction& instruction = instructions[line];
        std::string message = instruction.arg.empty() ? process->defaultPrintMessage()
                                                       : instruction.arg;

        std::ostringstream logLine;
        logLine << "(" << TimeUtil::formatNow() << ") Core:" << coreId << " \"" << message
                << "\"";

        process->appendLog(logLine.str());
        if (logFile.is_open()) {
            logFile << logLine.str() << "\n";
            logFile.flush();
        }

        process->setCurrentLine(line + 1);

        const int cycles = static_cast<int>(config_.delayPerExec) + 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(kCycleMs * cycles));
    }

    process->setStatus(ProcessStatus::Finished);
    process->setFinishTimestamp(TimeUtil::formatNow());
}

std::string Scheduler::buildStatusReport() {
    std::vector<std::shared_ptr<Process>> processes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processes = allProcesses_;
    }

    int coresUsed = 0;
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            ++coresUsed;
        }
    }
    if (coresUsed > config_.numCpu) {
        coresUsed = config_.numCpu;
    }
    const int coresAvailable = config_.numCpu - coresUsed;
    const int utilization =
        config_.numCpu > 0 ? (coresUsed * 100) / config_.numCpu : 0;

    std::ostringstream report;
    report << "CPU utilization: " << utilization << "%\n";
    report << "Cores used: " << coresUsed << "\n";
    report << "Cores available: " << coresAvailable << "\n";
    report << "\n";
    report << "---------------------------------------------\n";
    report << "Running processes:\n";
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            report << padRight(process->name(), 12) << " (" << process->creationTimestamp()
                   << ")    Core: " << process->assignedCore() << "    "
                   << process->currentLine() << " / " << process->totalLines() << "\n";
        }
    }

    report << "\nFinished processes:\n";
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Finished) {
            report << padRight(process->name(), 12) << " (" << process->finishTimestamp()
                   << ")    Finished    " << process->totalLines() << " / "
                   << process->totalLines() << "\n";
        }
    }
    report << "---------------------------------------------\n";
    return report.str();
}
