#pragma once

// ProcessModel.h
//
// Defines what a "process" is in the emulator: its program (instructions),
// runtime state (variables, logs, status), and the text shown by process-smi.
//
// Process objects are shared between threads:
//   - Scheduler worker threads update status, currentLine, variables, logs
//   - ScreenManager reads formatSmi() on the main CLI thread
//
// Instruction text is stored as strings; InstructionEngine parses and executes them.
//
// Who uses Process:
//   - Scheduler.cpp      : creates processes, runs instructions, updates state
//   - InstructionEngine.cpp : reads/writes variables during ADD, PRINT, etc.
//   - ScreenManager.cpp  : shows formatSmi() inside a process screen
//   - ReportManager.cpp  : lists processes in screen -ls output

#include <atomic>              // thread-safe status, line counter, core id, sleep cycle
#include <cstdint>             // uint16_t for variable values
#include <mutex>               // protects variables_, logs_, finishTimestamp_
#include <string>
#include <unordered_map>       // symbol table: variable name -> value
#include <vector>              // ordered instruction list and log history

// All instruction kinds the emulator supports (matches CSOPESY spec).
enum class InstructionType { Print, Declare, Add, Subtract, Sleep, For };

// One row in a process's program. type selects the opcode; arg holds the
// raw source text, e.g. ADD(x, x, 1) or "Value from: " + x for PRINT.
struct Instruction {
    InstructionType type = InstructionType::Print;
    std::string arg;
};

// Scheduler-visible lifecycle state for screen -ls and screen -r rules.
enum class ProcessStatus {
    Ready,     // in ready queue, waiting for a CPU core
    Running,   // assigned to a core and executing
    Finished   // all instructions done; screen -r will reject
};

class Process {
public:
    // id          : unique numeric id assigned by Scheduler (1, 2, 3, ...)
    // name        : user or batch name (e.g. proc-01, process05)
    // creationTimestamp : formatted time string for screen -ls
    Process(int id, std::string name, std::string creationTimestamp);

    // Sets x, y, z to 0 per test-case spec. Called from constructor.
    void initializeStandardVariables();

    // Convenience wrapper: adds a PRINT instruction to the program.
    // Empty message means "use defaultPrintMessage() at execution time".
    void addPrintInstruction(const std::string& message = "");

    // Appends any instruction type with its full source text in arg.
    void addInstruction(InstructionType type, const std::string& text);

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& creationTimestamp() const { return creationTimestamp_; }

    // Number of instructions in this process's program (lines of code).
    int totalLines() const { return static_cast<int>(instructions_.size()); }

    // Index of the next instruction to run (0-based). Updated by Scheduler.
    int currentLine() const { return currentLine_.load(); }

    // Core this process is running on, or -1 if not on a core.
    int assignedCore() const { return assignedCore_.load(); }
    ProcessStatus status() const { return status_.load(); }

    const std::vector<Instruction>& instructions() const { return instructions_; }

    // Human-readable text for one program line (used in process-smi output).
    std::string instructionTextAt(int line) const;

    void setCurrentLine(int line) { currentLine_.store(line); }
    void setAssignedCore(int core) { assignedCore_.store(core); }
    void setStatus(ProcessStatus status) { status_.store(status); }

    // Append one log line from a PRINT execution (thread-safe).
    void appendLog(const std::string& line);

    // Copy of all log lines for display without holding the lock long.
    std::vector<std::string> snapshotLogs();

    void setFinishTimestamp(const std::string& timestamp);
    std::string finishTimestamp();

    // Symbol table access. Missing names read as 0 (ADD auto-declares via setVariable).
    uint16_t getVariable(const std::string& name) const;
    void setVariable(const std::string& name, uint16_t value);

    // SLEEP support: global CPU cycle when this process may run again.
    void setSleepUntilCycle(uint64_t cycle);
    uint64_t sleepUntilCycle() const;
    bool isSleeping() const;

    // Builds the full process-smi screen text (name, id, logs, progress, vars).
    std::string formatSmi();

    // Default PRINT message when no custom string is provided.
    std::string defaultPrintMessage() const;

private:
    // --- Immutable after creation ---
    int id_;
    std::string name_;
    std::string creationTimestamp_;
    std::vector<Instruction> instructions_;  // the process "program"

    // --- Updated by scheduler threads (atomics = safe concurrent reads) ---
    std::atomic<int> currentLine_{0};
    std::atomic<int> assignedCore_{-1};
    std::atomic<ProcessStatus> status_{ProcessStatus::Ready};
    std::atomic<uint64_t> sleepUntilCycle_{0};  // 0 = not sleeping

    // --- Protected by stateMutex_ (logs + variables + finish time) ---
    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, uint16_t> variables_;
    std::vector<std::string> logs_;
    std::string finishTimestamp_;
};
