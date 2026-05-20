#include "ProcessManager.h"

#include <algorithm>

namespace {

bool isAccessible(const Process& process) {
    if (process.status == ProcessStatus::Running) {
        return true;
    }
    return process.status == ProcessStatus::Finished && !process.hasExitedAfterFinish;
}

}  // namespace

void ProcessManager::setConfig(const Config& config) {
    config_ = config;
}

bool ProcessManager::createProcess(const std::string& name, std::string& errorMessage) {
    if (name.empty()) {
        errorMessage = "Process name cannot be empty.";
        return false;
    }

    const auto existing = findProcess(name);
    if (existing && isAccessible(*existing)) {
        errorMessage = "Process " + name + " already exists.";
        return false;
    }

    auto process = std::make_shared<Process>();
    process->initializeDummy(name, nextProcessId_++, config_.minIns, config_.maxIns, config_.numCpu);
    processes_[name] = process;
    activeProcess_ = process.get();
    errorMessage.clear();
    return true;
}

Process* ProcessManager::attachProcess(const std::string& name, std::string& errorMessage) {
    const auto process = findProcess(name);
    if (!process || !isAccessible(*process)) {
        errorMessage = "Process " + name + " not found.";
        return nullptr;
    }
    activeProcess_ = process.get();
    errorMessage.clear();
    return activeProcess_;
}

void ProcessManager::detachActiveProcess(bool markExitedIfFinished) {
    if (activeProcess_ && activeProcess_->status == ProcessStatus::Finished && markExitedIfFinished) {
        activeProcess_->hasExitedAfterFinish = true;
    }
    activeProcess_ = nullptr;
}

void ProcessManager::advanceActiveProcess() {
    if (activeProcess_) {
        activeProcess_->advanceDummyState(config_.numCpu);
    }
}

bool ProcessManager::processExistsAndAccessible(const std::string& name) const {
    const auto process = findProcess(name);
    return process && isAccessible(*process);
}

std::vector<const Process*> ProcessManager::getRunningProcesses() const {
    std::vector<const Process*> result;
    for (const auto& entry : processes_) {
        if (entry.second->status == ProcessStatus::Running) {
            result.push_back(entry.second.get());
        }
    }
    return result;
}

std::vector<const Process*> ProcessManager::getFinishedProcesses() const {
    std::vector<const Process*> result;
    for (const auto& entry : processes_) {
        if (entry.second->status == ProcessStatus::Finished) {
            result.push_back(entry.second.get());
        }
    }
    return result;
}

int ProcessManager::countRunning() const {
    return static_cast<int>(getRunningProcesses().size());
}

int ProcessManager::countCoresUsed() const {
    int used = 0;
    for (const auto* process : getRunningProcesses()) {
        if (process->assignedCore >= 0 && process->assignedCore < config_.numCpu) {
            ++used;
        }
    }
    return std::min(used, config_.numCpu);
}

void ProcessManager::addExternalProcess(const std::string& name) {
    if (findProcess(name)) {
        return;
    }
    auto process = std::make_shared<Process>();
    process->initializeDummy(name, nextProcessId_++, config_.minIns, config_.maxIns, config_.numCpu);
    processes_[name] = process;
}

std::shared_ptr<Process> ProcessManager::findProcess(const std::string& name) const {
    const auto it = processes_.find(name);
    if (it == processes_.end()) {
        return nullptr;
    }
    return it->second;
}
