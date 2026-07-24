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
// Read/Write added for MCO2 demand-paging memory access.
enum class InstructionType { Print, Declare, Add, Subtract, Sleep, For, Read, Write };

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

// Why a process left the Running/Ready lifecycle. Used by screen -r to decide
// whether to print the normal "finished" info or a memory-violation message.
enum class TerminationReason {
    None,             // still running or completed normally
    Completed,        // ran all instructions successfully
    MemoryViolation   // shut down after accessing an invalid address
};

class Process {
public:
    // id          : unique numeric id assigned by Scheduler (1, 2, 3, ...)
    // name        : user or batch name (e.g. proc-01, process05)
    // creationTimestamp : formatted time string for screen -ls
    Process(int id, std::string name, std::string creationTimestamp);

    // Adds the required x variable with an initial value of 0.
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

    // ── MCO2 memory model ────────────────────────────────────────────────────

    // Total virtual memory size (bytes) this process was created with.
    uint32_t memoryBytes() const { return memoryBytes_; }
    void setMemoryBytes(uint32_t bytes) { memoryBytes_ = bytes; }

    // Number of pages this process spans given a frame size (rounded up).
    int pageCount(uint32_t frameSize) const;

    // DECLARE / implicit variable creation, capped at 32 symbol-table entries.
    // Returns false (and ignores the write) when the cap is reached for a new
    // variable name. Existing names always update successfully.
    bool trySetVariable(const std::string& name, uint16_t value);
    int variableCount() const;

    // READ/WRITE emulated memory (byte-addressed, uint16 little-endian).
    // Returns false when [address, address+2) is outside [0, memoryBytes_):
    // the caller treats this as a memory access violation.
    bool readMemory(uint32_t address, uint16_t& valueOut) const;
    bool writeMemory(uint32_t address, uint16_t value);

    // Memory-access-violation bookkeeping (for screen -r message).
    void recordMemoryViolation(uint32_t address, const std::string& timestamp);
    TerminationReason terminationReason() const { return terminationReason_.load(); }
    void setTerminationReason(TerminationReason reason) { terminationReason_.store(reason); }
    uint32_t violationAddress() const { return violationAddress_.load(); }
    std::string violationTime();

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

    // --- MCO2 memory model ---
    uint32_t memoryBytes_ = 0;                                  // total virtual size
    std::atomic<TerminationReason> terminationReason_{TerminationReason::None};
    std::atomic<uint32_t> violationAddress_{0};

    // Symbol-table cap per CSOPESY spec: 64-byte segment / 2 bytes = 32 variables.
    static constexpr int kMaxVariables = 32;

    // --- Protected by stateMutex_ (logs + variables + finish time + memory) ---
    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, uint16_t> variables_;
    std::unordered_map<uint32_t, uint8_t> memory_;  // byte-addressed emulated RAM
    std::vector<std::string> logs_;
    std::string finishTimestamp_;
    std::string violationTime_;
};
