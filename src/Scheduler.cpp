#include "Scheduler.h"

#include "TimeUtil.h"

#include <iomanip>
#include <sstream>

namespace {

std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

std::string buildPrintLog(const std::string& timestamp, int core,
                          const std::string& processName) {
    std::ostringstream log;
    log << "(" << timestamp << ") Core:" << core << " \"Hello world from "
        << processName << "!\"";
    return log.str();
}

}  // namespace

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (schedulerRunning_) {
        return;
    }

    config_ = config;
    if (config_.numCpu < 1) {
        config_.numCpu = 1;
    }

    schedulerRunning_ = true;
    addSchedulerMockProcessesLocked();
}

void Scheduler::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    schedulerRunning_ = false;
}

bool Scheduler::isEngineRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return schedulerRunning_;
}

std::shared_ptr<Process> Scheduler::createProcessLocked(const std::string& name,
                                                        int instructionCount) {
    if (instructionCount < 1) {
        instructionCount = 1;
    }

    const int core = config_.numCpu > 0
                         ? static_cast<int>(allProcesses_.size() % config_.numCpu)
                         : 0;

    auto process = std::make_shared<Process>(nextId_++, name, TimeUtil::formatNow());
    process->addPrintInstruction();
    process->addInstruction(InstructionType::Declare, "DECLARE(counter, 0)");
    process->addInstruction(InstructionType::Add, "ADD(counter, counter, 1)");
    process->addInstruction(InstructionType::Subtract, "SUBTRACT(counter, counter, 1)");
    process->addInstruction(InstructionType::Sleep, "SLEEP(1)");
    process->addInstruction(InstructionType::For, "FOR([PRINT], 2)");

    while (process->totalLines() < instructionCount) {
        process->addPrintInstruction();
    }

    process->setAssignedCore(core);
    process->setStatus(ProcessStatus::Running);
    process->appendLog(buildPrintLog(process->creationTimestamp(), core, name));
    allProcesses_.push_back(process);
    return process;
}

std::shared_ptr<Process> Scheduler::createProcess(const std::string& name,
                                                  int instructionCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    return createProcessLocked(name, instructionCount);
}

int Scheduler::generateBatch(int count, int instructionCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < count; ++i) {
        std::ostringstream name;
        name << "process" << std::setw(2) << std::setfill('0') << (i + 1);
        if (!processExistsLocked(name.str())) {
            createProcessLocked(name.str(), instructionCount);
        }
    }
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
    std::lock_guard<std::mutex> lock(mutex_);
    return processExistsLocked(name);
}

bool Scheduler::processExistsLocked(const std::string& name) const {
    for (const auto& process : allProcesses_) {
        if (process->name() == name) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Process> Scheduler::addMockProcessLocked(const std::string& name,
                                                         int id,
                                                         const std::string& timestamp,
                                                         int core, int currentLine,
                                                         int totalLines,
                                                         bool finished) {
    auto process = std::make_shared<Process>(id, name, timestamp);

    process->addPrintInstruction();
    process->addInstruction(InstructionType::Declare, "DECLARE(counter, 0)");
    process->addInstruction(InstructionType::Add, "ADD(counter, counter, 1)");
    process->addInstruction(InstructionType::Subtract, "SUBTRACT(counter, counter, 1)");
    process->addInstruction(InstructionType::Sleep, "SLEEP(1)");
    process->addInstruction(InstructionType::For, "FOR([PRINT], 2)");

    while (process->totalLines() < totalLines) {
        process->addPrintInstruction();
    }

    process->setAssignedCore(core);
    process->setCurrentLine(currentLine);
    process->appendLog(buildPrintLog(timestamp, core, name));

    if (finished) {
        process->setStatus(ProcessStatus::Finished);
        process->setCurrentLine(process->totalLines());
        process->setFinishTimestamp(timestamp);
    } else {
        process->setStatus(ProcessStatus::Running);
    }

    allProcesses_.push_back(process);
    return process;
}

void Scheduler::addSchedulerMockProcessesLocked() {
    if (schedulerMockProcessesGenerated_) {
        return;
    }

    addMockProcessLocked("p01", 101, "01/18/2024 09:20:00AM", 0, 0, 1000, false);
    addMockProcessLocked("p02", 102, "01/18/2024 09:20:01AM", 1, 0, 1200, false);
    addMockProcessLocked("p03", 103, "01/18/2024 09:20:02AM", 2, 0, 1400, false);
    addMockProcessLocked("p04", 104, "01/18/2024 09:20:03AM", 3, 0, 1600, false);
    addMockProcessLocked("p05", 105, "01/18/2024 09:20:04AM", 0, 0, 1800, false);

    schedulerMockProcessesGenerated_ = true;
}

std::string Scheduler::buildStatusReport() {
    std::vector<std::shared_ptr<Process>> processes;
    int cpuCount = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processes = allProcesses_;
        cpuCount = config_.numCpu;
    }

    if (cpuCount < 1) {
        cpuCount = 1;
    }

    int coresUsed = 0;
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            ++coresUsed;
        }
    }
    if (coresUsed > cpuCount) {
        coresUsed = cpuCount;
    }

    const int coresAvailable = cpuCount - coresUsed;
    const int utilization = (coresUsed * 100) / cpuCount;

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
