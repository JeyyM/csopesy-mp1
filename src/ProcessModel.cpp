#include "ProcessModel.h"

#include <sstream>
#include <utility>

// multiple threads (the core thread running this process, and the main
// thread showing its screen) can call these methods at the same time,
// so we lock stateMutex_ whenever we touch the logs_ vector or
// finishTimestamp_ to avoid two threads stepping on each other.

Process::Process(int id, std::string name, std::string creationTimestamp)
    : id_(id), name_(std::move(name)), creationTimestamp_(std::move(creationTimestamp)) {}

void Process::addPrintInstruction(const std::string& message) {
    instructions_.push_back({InstructionType::Print, message});
}

void Process::appendLog(const std::string& line) {
    // 1. Lock the mutex so no other thread can read/write logs_ at the
    //    same time.
    // 2. Add the new log line.
    // 3. The lock automatically releases when this function returns
    //    (that's what std::lock_guard does for us).
    std::lock_guard<std::mutex> lock(stateMutex_);
    logs_.push_back(line);
}

std::vector<std::string> Process::snapshotLogs() {
    // Returns a COPY of the log list while holding the lock, so the
    // caller can safely loop over it afterward without risking another
    // thread changing logs_ mid-loop.
    std::lock_guard<std::mutex> lock(stateMutex_);
    return logs_;
}

void Process::setFinishTimestamp(const std::string& timestamp) {
    // Called once, the moment a process executes its very last
    // instruction (see Scheduler::executeProcess). Recorded so
    // "screen -ls" can show when each finished process completed.
    std::lock_guard<std::mutex> lock(stateMutex_);
    finishTimestamp_ = timestamp;
}

std::string Process::finishTimestamp() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return finishTimestamp_;
}

std::string Process::defaultPrintMessage() const {
    return "Hello world from " + name_ + "!";
}

std::string Process::formatSmi() {
    const std::vector<std::string> logs = snapshotLogs();
    const ProcessStatus currentStatus = status_.load();

    std::ostringstream output;
    output << "Process name: " << name_ << "\n";
    output << "ID: " << id_ << "\n";
    output << "Logs:\n";
    for (const std::string& log : logs) {
        output << log << "\n";
    }

    if (currentStatus == ProcessStatus::Finished) {
        output << "\nFinished!\n";
        return output.str();
    }

    output << "\nCurrent instruction line: " << currentLine_.load() << "\n";
    output << "Lines of code: " << totalLines() << "\n";
    return output.str();
}
