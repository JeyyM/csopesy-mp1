#pragma once

#include "Config.h"
#include "ProcessModel.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Scheduler {
public:
    ~Scheduler();

    void start(const Config& config);
    void stop();

    bool isEngineRunning() const;

    std::shared_ptr<Process> createProcess(const std::string& name, int instructionCount);
    int generateBatch(int count, int instructionCount);

    std::shared_ptr<Process> findProcess(const std::string& name);
    bool processExists(const std::string& name);

    std::string buildStatusReport();

    int numCpu() const { return config_.numCpu; }

private:
    std::shared_ptr<Process> createProcessLocked(const std::string& name,
                                                 int instructionCount);
    std::shared_ptr<Process> addMockProcessLocked(const std::string& name, int id,
                                                  const std::string& timestamp,
                                                  int core, int currentLine,
                                                  int totalLines, bool finished);
    void addSchedulerMockProcessesLocked();
    bool processExistsLocked(const std::string& name) const;

    Config config_{};
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Process>> allProcesses_;

    int nextId_ = 1;
    bool schedulerRunning_ = false;
    bool schedulerMockProcessesGenerated_ = false;
};
