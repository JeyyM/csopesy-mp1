#pragma once

// InstructionEngine.h
//
// Parses and runs CSOPESY process instructions against a Process object.
// See InstructionEngine.cpp top-of-file "WORKED EXAMPLES" for IN/OUT samples
// of every function (splitTopLevelArgs, execute, PRINT, ADD, FOR, etc.).
//
// Supported opcodes (CSOPESY spec):
//   PRINT(msg)              — append a timestamped log line
//   DECLARE(var, value)     — set variable to literal
//   ADD(dest, a, b)         — dest = a + b  (vars or literals)
//   SUBTRACT(dest, a, b)    — dest = a - b
//   SLEEP(ticks)            — yield CPU for ticks global cycles
//   FOR([body...], repeats) — loop; nest up to kMaxForDepth (3)
//
// Typical call path:
//   Scheduler::runInstructionTree -> InstructionEngine::execute (one step)
//   or executeBlock / parseForInstruction for FOR bodies
//
// ExecuteResult tells Scheduler what happened (log line? need to sleep?).

#include "ProcessModel.h"

#include <cstdint>
#include <string>
#include <vector>

class InstructionEngine {
public:
    // Returned after each execute() / executeBlock() call.
    struct ExecuteResult {
        bool producedLog = false;   // true if PRINT ran — logLine is set
        std::string logLine;        // e.g. (06/26/2026 ...) Core:0 "Value from: 1"
        bool relinquishCpu = false; // true if SLEEP — Scheduler parks the process
        uint32_t sleepTicks = 0;    // SLEEP argument (global CPU ticks to wait)

        // MCO2 demand paging: byte addresses this instruction touched. The
        // Scheduler converts each to a page and calls MemoryManager::accessPage
        // so faults are counted. Symbol-table (variable) access is reported as
        // address 0, which always lives in page 0.
        std::vector<uint32_t> accessedAddresses;

        // Set when a READ/WRITE targets an address outside the process's memory.
        // The Scheduler terminates the process with a memory access violation.
        bool memoryViolation = false;
        uint32_t violationAddress = 0;
    };

    // Run one Instruction against process. coreId appears in PRINT log lines.
    static ExecuteResult execute(Process& process, const Instruction& instruction, int coreId);

    // Parse a semicolon-separated user program (screen -c). Returns the parsed
    // instructions; count is the caller's responsibility to validate (1..50).
    static std::vector<Instruction> parseUserProgram(const std::string& text);

    // Run a sequence of instructions (FOR body or inline list). nestingDepth
    // tracks FOR nesting for the 3-level limit.
    static ExecuteResult executeBlock(Process& process, const std::vector<Instruction>& body,
                                      int coreId, int nestingDepth);

    // Split comma-separated instruction text into parsed Instruction objects.
    // Respects quotes, parentheses, and brackets (for FOR bodies).
    static std::vector<Instruction> parseInstructionList(const std::string& text);

    // Parse FOR([...], N) into body instructions and repeat count.
    static bool parseForInstruction(const std::string& text, std::vector<Instruction>& bodyOut,
                                    int& repeatsOut);

private:
    static constexpr int kMaxForDepth = 3;

    static std::string trim(const std::string& value);
    static std::vector<std::string> splitTopLevelArgs(const std::string& args);
    static uint16_t parseUint16Literal(const std::string& token, bool& ok);
    static uint16_t resolveOperand(Process& process, const std::string& token, bool& ok);
    static std::string resolvePrintMessage(Process& process, const std::string& rawMessage);
    static Instruction parseSingleInstruction(const std::string& text);

    // Operand extraction that accepts BOTH function-style "ADD(a, b, c)" and
    // space-style "ADD a b c" (the latter used by screen -c user programs).
    static std::vector<std::string> extractOperands(const std::string& text);

    // Parses a hex ("0x1F") or decimal ("42") address token. Returns false on
    // malformed input.
    static bool parseAddressToken(const std::string& token, uint32_t& out);

    // True when a token is a variable name (starts with a letter/underscore),
    // as opposed to a numeric literal.
    static bool isVariableToken(const std::string& token);
};
