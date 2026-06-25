#pragma once

#include "ProcessModel.h"

#include <cstdint>
#include <string>
#include <vector>

// Parses and executes CSOPESY process instructions (PRINT, DECLARE, ADD,
// SUBTRACT, SLEEP, FOR). FOR bodies may nest up to three levels deep.
class InstructionEngine {
public:
    struct ExecuteResult {
        bool producedLog = false;
        std::string logLine;
        bool relinquishCpu = false;
        uint32_t sleepTicks = 0;
    };

    static ExecuteResult execute(Process& process, const Instruction& instruction, int coreId);
    static ExecuteResult executeBlock(Process& process, const std::vector<Instruction>& body,
                                      int coreId, int nestingDepth);

    static std::vector<Instruction> parseInstructionList(const std::string& text);
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
};
