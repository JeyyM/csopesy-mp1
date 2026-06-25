#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class InstructionType { Print, Declare, Add, Subtract, Sleep, For };

struct Instruction {
    InstructionType type = InstructionType::Print;
    std::string arg;
};

enum class ProcessStatus { Ready, Running, Finished };

class Process {
public:
    Process(int id, std::string name, std::string creationTimestamp);

    void initializeStandardVariables();
    void addStandardForProgram(int targetLineCount);
    void addPrintInstruction(const std::string& message = "");
    void addInstruction(InstructionType type, const std::string& text);

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& creationTimestamp() const { return creationTimestamp_; }
    int totalLines() const { return static_cast<int>(instructions_.size()); }

    int currentLine() const { return currentLine_.load(); }

    int assignedCore() const { return assignedCore_.load(); }
    ProcessStatus status() const { return status_.load(); }

    const std::vector<Instruction>& instructions() const { return instructions_; }
    std::string instructionTextAt(int line) const;

    void setCurrentLine(int line) { currentLine_.store(line); }
    void setAssignedCore(int core) { assignedCore_.store(core); }
    void setStatus(ProcessStatus status) { status_.store(status); }

    void appendLog(const std::string& line);
    std::vector<std::string> snapshotLogs();

    void setFinishTimestamp(const std::string& timestamp);
    std::string finishTimestamp();

    uint16_t getVariable(const std::string& name) const;
    void setVariable(const std::string& name, uint16_t value);

    void setSleepUntilCycle(uint64_t cycle);
    uint64_t sleepUntilCycle() const;
    bool isSleeping() const;

    std::string formatSmi();
    std::string defaultPrintMessage() const;

private:
    int id_;
    std::string name_;
    std::string creationTimestamp_;
    std::vector<Instruction> instructions_;

    std::atomic<int> currentLine_{0};
    std::atomic<int> assignedCore_{-1};
    std::atomic<ProcessStatus> status_{ProcessStatus::Ready};
    std::atomic<uint64_t> sleepUntilCycle_{0};

    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, uint16_t> variables_;
    std::vector<std::string> logs_;
    std::string finishTimestamp_;
};
