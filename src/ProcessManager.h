#pragma once

#include "Config.h"
#include "Process.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ProcessManager {
public:
    void setConfig(const Config& config);

    bool createProcess(const std::string& name, std::string& errorMessage);
    Process* attachProcess(const std::string& name, std::string& errorMessage);
    Process* getActiveProcess() const { return activeProcess_; }

    void detachActiveProcess(bool markExitedIfFinished);
    void advanceActiveProcess();

    bool processExistsAndAccessible(const std::string& name) const;
    std::vector<const Process*> getRunningProcesses() const;
    std::vector<const Process*> getFinishedProcesses() const;

    int countRunning() const;
    int countCoresUsed() const;

    void addExternalProcess(const std::string& name);

private:
    Config config_{};
    std::unordered_map<std::string, std::shared_ptr<Process>> processes_;
    Process* activeProcess_ = nullptr;
    int nextProcessId_ = 1;

    std::shared_ptr<Process> findProcess(const std::string& name) const;
};
