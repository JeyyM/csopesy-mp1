// ProcessModel.cpp
//
// Implements the Process class — the in-memory model of one running "process".
//
// A Process holds:
//   - id_, name_, creationTimestamp_  : identity (set once at creation)
//   - instructions_                   : the program (vector of Instruction)
//   - currentLine_                    : which instruction runs next (0-based)
//   - assignedCore_                   : CPU core index, or -1 if not running
//   - status_                         : Ready | Running | Finished
//   - variables_                      : symbol table (x, y, z, etc.) -> uint16
//   - logs_                           : output lines from PRINT instructions
//   - finishTimestamp_                : time when process completed
//   - memoryBase_                     : first byte owned by this process;
//                                       -1 means it is waiting for memory
//   - sleepUntilCycle_                : for SLEEP — wake at this global CPU cycle
//
// Instruction struct (defined in ProcessModel.h):
//   - type : Print | Declare | Add | Subtract | Sleep | For
//   - arg  : raw text, e.g. "ADD(x, x, 1)" or "\"Value from: \" + x" for PRINT
//
// Threading:
//   - Scheduler worker threads write status, currentLine, variables, logs
//   - Main CLI thread reads formatSmi() while user types process-smi
//   - atomics (currentLine_, status_, etc.) need no lock for simple reads
//   - variables_, logs_, finishTimestamp_ use stateMutex_ because std containers
//     are not safe to read/write from two threads at once
//
// Callers:
//   - Scheduler.cpp       : creates Process, runs program, updates state
//   - InstructionEngine.cpp : getVariable / setVariable during execution
//   - ScreenManager.cpp   : formatSmi() inside process screen
//   - ReportManager.cpp   : name, status, currentLine for screen -ls

#include "ProcessModel.h"

#include <sstream>  // std::ostringstream for building formatSmi() text
#include <utility>  // std::move for efficient string transfer in constructor

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// Creates a new process. Scheduler assigns id and picks name/timestamp.
// Immediately seeds x, y, z = 0 via initializeStandardVariables().
Process::Process(int id, std::string name, std::string creationTimestamp)
    : id_(id),
      name_(std::move(name)),
      creationTimestamp_(std::move(creationTimestamp)) {
    initializeStandardVariables();
}

// Puts x into variables_ at 0. Required by the grading / demo spec.
// Uses setVariable so the mutex is locked correctly.
void Process::initializeStandardVariables() {
    setVariable("x", 0);
}

// ---------------------------------------------------------------------------
// Building the program (instructions_ vector)
// ---------------------------------------------------------------------------

// Adds one PRINT line to the end of the program.
// message empty  -> at run time InstructionEngine uses defaultPrintMessage()
// message non-empty -> stored in Instruction.arg, e.g. "\"Value from: \" + x"
// push_back appends one element to the end of the instructions_ vector.
void Process::addPrintInstruction(const std::string& message) {
    instructions_.push_back({InstructionType::Print, message});
}

// Generic version: any InstructionType plus its source text in arg.
// Scheduler::addStandardProgram uses this to add 600 ADD/PRINT lines.
void Process::addInstruction(InstructionType type, const std::string& text) {
    instructions_.push_back({type, text});
}

// ---------------------------------------------------------------------------
// Display helpers (does NOT execute instructions)
// ---------------------------------------------------------------------------

// Returns a readable string for program line `line` (for process-smi "Instruction:").
// Does not run the instruction — only formats it for the user.
// Returns "" if line is out of range.
std::string Process::instructionTextAt(int line) const {
    if (line < 0 || line >= static_cast<int>(instructions_.size())) {
        return "";
    }

    const Instruction& instruction = instructions_[line];

    switch (instruction.type) {
        case InstructionType::Print:
            // PRINT stores only the inside of the parentheses in arg.
            if (instruction.arg.empty()) {
                return "PRINT(\"" + defaultPrintMessage() + "\")";
            }
            return "PRINT(" + instruction.arg + ")";

        // Declare, Add, Subtract, Sleep, For store the full line in arg already.
        case InstructionType::Declare:
        case InstructionType::Add:
        case InstructionType::Subtract:
        case InstructionType::Sleep:
        case InstructionType::For:
            return instruction.arg;
    }

    return instruction.arg;
}

// Default text for PRINT when arg is empty: Hello world from <process name>!
std::string Process::defaultPrintMessage() const {
    return "Hello world from " + name_ + "!";
}

// ---------------------------------------------------------------------------
// Logs (logs_ vector, mutex-protected)
// ---------------------------------------------------------------------------

// Called by Scheduler after each PRINT executes.
// Example line: (06/26/2026 12:02:30AM) Core:0 "Value from: 1"
void Process::appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    logs_.push_back(line);
}

// Returns a copy of all log lines so formatSmi can print them safely
// without holding the lock while building a long string.
std::vector<std::string> Process::snapshotLogs() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return logs_;
}

// ---------------------------------------------------------------------------
// Finish time (finishTimestamp_, mutex-protected)
// ---------------------------------------------------------------------------

// Scheduler sets this when the process runs out of instructions.
void Process::setFinishTimestamp(const std::string& timestamp) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    finishTimestamp_ = timestamp;
}

std::string Process::finishTimestamp() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return finishTimestamp_;
}

// ---------------------------------------------------------------------------
// Symbol table (variables_ map, mutex-protected)
// ---------------------------------------------------------------------------

// Read a variable by name. uint16_t = 0..65535 per CSOPESY spec.
// If the name was never set, returns 0 (spec: implicit zero).
uint16_t Process::getVariable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = variables_.find(name);
    if (it == variables_.end()) {
        return 0;
    }
    return it->second;
}

// Write or create a variable. InstructionEngine calls this for ADD, DECLARE, etc.
void Process::setVariable(const std::string& name, uint16_t value) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    variables_[name] = value;
}

// ---------------------------------------------------------------------------
// Sleep state (sleepUntilCycle_, atomic — no mutex needed)
// ---------------------------------------------------------------------------

// Scheduler sets wake time after SLEEP(N): current global cycle + N.
void Process::setSleepUntilCycle(uint64_t cycle) {
    sleepUntilCycle_.store(cycle);
}

uint64_t Process::sleepUntilCycle() const {
    return sleepUntilCycle_.load();
}

// True while sleepUntilCycle_ > 0. Scheduler clears it when process wakes.
bool Process::isSleeping() const {
    return sleepUntilCycle_.load() > 0;
}

// ---------------------------------------------------------------------------
// process-smi output (main user-facing view of a process)
// ---------------------------------------------------------------------------

// Builds the entire text block shown when the user types process-smi
// or when they first enter a process screen (ScreenManager calls this).
//
// Layout:
//   Process name / ID
//   Logs: (all PRINT output so far)
//   If Finished -> "Finished!" and stop
//   Else -> current line, total lines, x/y/z values, current instruction text
//           optional sleep status
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
    output << "Variables: x=" << getVariable("x") << "\n";

    if (line >= 0 && line < totalLines()) {
        output << "Instruction: " << instructionTextAt(line) << "\n";
    }

    if (isSleeping()) {
        output << "Status: sleeping until CPU cycle " << sleepUntilCycle() << "\n";
    }

    return output.str();
}
