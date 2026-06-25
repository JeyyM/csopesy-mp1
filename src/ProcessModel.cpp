#include "ProcessModel.h"

#include <sstream>
#include <utility>

Process::Process(int id, std::string name, std::string creationTimestamp)
    : id_(id), name_(std::move(name)), creationTimestamp_(std::move(creationTimestamp)) {
    initializeStandardVariables();
}

void Process::initializeStandardVariables() {
    setVariable("x", 0);
    setVariable("y", 0);
    setVariable("z", 0);
}

void Process::addStandardForProgram(int targetLineCount) {
    static const std::string kStandardFor =
        "FOR([ADD(x, x, 1), PRINT(\"Value from: \" +x), ADD(y, y, 1), "
        "PRINT(\"Value from: \" +y), ADD(z, z, 1), PRINT(\"Value from: \" +z)], 100)";
    addInstruction(InstructionType::For, kStandardFor);
    while (totalLines() < targetLineCount) {
        addInstruction(InstructionType::Declare, "DECLARE(pad, 0)");
    }
}

void Process::addPrintInstruction(const std::string& message) {
    instructions_.push_back({InstructionType::Print, message});
}

void Process::addInstruction(InstructionType type, const std::string& text) {
    instructions_.push_back({type, text});
}

std::string Process::instructionTextAt(int line) const {
    if (line < 0 || line >= static_cast<int>(instructions_.size())) {
        return "";
    }
    const Instruction& instruction = instructions_[line];
    switch (instruction.type) {
        case InstructionType::Print:
            if (instruction.arg.empty()) {
                return "PRINT(\"" + defaultPrintMessage() + "\")";
            }
            return "PRINT(" + instruction.arg + ")";
        case InstructionType::Declare:
        case InstructionType::Add:
        case InstructionType::Subtract:
        case InstructionType::Sleep:
        case InstructionType::For:
            return instruction.arg;
    }
    return instruction.arg;
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

uint16_t Process::getVariable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = variables_.find(name);
    if (it == variables_.end()) {
        return 0;
    }
    return it->second;
}

void Process::setVariable(const std::string& name, uint16_t value) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    variables_[name] = value;
}

void Process::setSleepUntilCycle(uint64_t cycle) {
    sleepUntilCycle_.store(cycle);
}

uint64_t Process::sleepUntilCycle() const {
    return sleepUntilCycle_.load();
}

bool Process::isSleeping() const {
    return sleepUntilCycle_.load() > 0;
}

std::string Process::defaultPrintMessage() const {
    return "Hello world from " + name_ + "!";
}

std::string Process::formatSmi() {
    const std::vector<std::string> logs = snapshotLogs();
    const ProcessStatus currentStatus = status_.load();
    const int line = currentLine_.load();

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

    output << "\nCurrent instruction line: " << line << "\n";
    output << "Lines of code: " << totalLines() << "\n";
    output << "Variables: x=" << getVariable("x") << " y=" << getVariable("y")
           << " z=" << getVariable("z") << "\n";
    if (line >= 0 && line < totalLines()) {
        output << "Instruction: " << instructionTextAt(line) << "\n";
    }
    if (isSleeping()) {
        output << "Status: sleeping until CPU cycle " << sleepUntilCycle() << "\n";
    }
    return output.str();
}
