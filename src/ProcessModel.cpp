#include "ProcessModel.h"

#include <sstream>
#include <utility>

Process::Process(int id, std::string name, std::string creationTimestamp)
    : id_(id), name_(std::move(name)), creationTimestamp_(std::move(creationTimestamp)) {}

void Process::addPrintInstruction(const std::string& message) {
    instructions_.push_back({InstructionType::Print, message});
}

void Process::appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    logs_.push_back(line);
}

std::vector<std::string> Process::snapshotLogs() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return logs_;
}

void Process::setFinishTimestamp(const std::string& timestamp) {
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
