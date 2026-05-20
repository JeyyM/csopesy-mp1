#include "Scheduler.h"

#include "MockProcessData.h"

std::string Scheduler::start() {
    if (running_) {
        return "Scheduler is already running.";
    }

    running_ = true;
    MockProcessData::addSchedulerMockProcesses();
    return "Scheduler started. Generating dummy processes.";
}

std::string Scheduler::stop() {
    if (!running_) {
        return "Scheduler is not running.";
    }

    running_ = false;
    return "Scheduler stopped.";
}

void Scheduler::onCommandTick() {
    if (running_) {
        ++cpuCycles_;
    }
}
