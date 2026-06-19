#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Instruction types the emulator is designed to support.
// For this homework only PRINT is actually executed; the rest are reserved
// so the model can be extended later without changing the scheduler.
enum class InstructionType { Print, Declare, Add, Subtract, Sleep, For };

struct Instruction {
    InstructionType type = InstructionType::Print;
    std::string arg;  // PRINT message; empty means use the default message.
};

enum class ProcessStatus { Ready, Running, Finished };

class Process {
public:
    Process(int id, std::string name, std::string creationTimestamp);

    void addPrintInstruction(const std::string& message = "");

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& creationTimestamp() const { return creationTimestamp_; }
    int totalLines() const { return static_cast<int>(instructions_.size()); }

    int currentLine() const { return currentLine_.load(); }
    int assignedCore() const { return assignedCore_.load(); }
    ProcessStatus status() const { return status_.load(); }

    const std::vector<Instruction>& instructions() const { return instructions_; }

    void setCurrentLine(int line) { currentLine_.store(line); }
    void setAssignedCore(int core) { assignedCore_.store(core); }
    void setStatus(ProcessStatus status) { status_.store(status); }

    void appendLog(const std::string& line);
    std::vector<std::string> snapshotLogs();

    void setFinishTimestamp(const std::string& timestamp);
    std::string finishTimestamp();

    // Builds the process-smi view from the current live state.
    std::string formatSmi();

    // Default PRINT message used when an instruction has no explicit message.
    std::string defaultPrintMessage() const;

private:
    int id_;
    std::string name_;
    std::string creationTimestamp_;
    std::vector<Instruction> instructions_;

    std::atomic<int> currentLine_{0};
    std::atomic<int> assignedCore_{-1};
    std::atomic<ProcessStatus> status_{ProcessStatus::Ready};

    std::mutex stateMutex_;
    std::vector<std::string> logs_;
    std::string finishTimestamp_;
};
