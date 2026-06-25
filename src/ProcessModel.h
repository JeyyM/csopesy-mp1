#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// This file defines what a "process" IS inside our emulator. You can
// think of a Process object as a little folder containing:
//   1. A to-do list of instructions (for this assignment, just a bunch
//      of PRINT commands).
//   2. Some bookkeeping: which CPU core is running it right now, how
//      many instructions it has finished, and whether it's
//      Ready / Running / Finished.
//   3. A running log of everything it has printed so far.
//
// Multiple threads can access the same process data at the same time.
// Without protection, this can lead to crashes or corrupted data.
// To prevent this:
//   - Simple values (like numbers and status flags) use std::atomic,
//     which makes reads and writes safe.
//   - The logs list uses a std::mutex, which allows only one thread
//     to access it at a time.


// Instruction types the emulator is designed to support.
enum class InstructionType { Print, Declare, Add, Subtract, Sleep, For };

struct Instruction {
    InstructionType type = InstructionType::Print;
    std::string arg;  // PRINT message; empty means use the default message.
};

enum class ProcessStatus { Ready, Running, Finished };

class Process {
public:
    // Builds a new process. `id` is a unique number, `name` is something
    // like "process01", and `creationTimestamp` records when it was made
    Process(int id, std::string name, std::string creationTimestamp);

    void addPrintInstruction(const std::string& message = "");

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& creationTimestamp() const { return creationTimestamp_; }
    int totalLines() const { return static_cast<int>(instructions_.size()); }

    // How many instructions this process has already executed.
    // This is the "x" in the "x / total" progress shown by "screen -ls"
    int currentLine() const { return currentLine_.load(); }

    // Which CPU core (0, 1, 2, 3, ...) is currently running this
    // process. -1 means it isn't assigned to any core right now
    // (e.g. it's still waiting in the ready queue, or already finished).
    int assignedCore() const { return assignedCore_.load(); }
    ProcessStatus status() const { return status_.load(); }

    const std::vector<Instruction>& instructions() const { return instructions_; }

    void setCurrentLine(int line) { currentLine_.store(line); }
    void setAssignedCore(int core) { assignedCore_.store(core); }
    void setStatus(ProcessStatus status) { status_.store(status); }

    // Adds one new line to this process's in-memory log history
    void appendLog(const std::string& line);

    // Returns a safe COPY of all log lines collected so far, so the
    // caller can print them without holding a lock the whole time.
    std::vector<std::string> snapshotLogs();

    // Records the timestamp of the moment this process finished its
    // very last instruction (shown in the "Finished processes" list).
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
