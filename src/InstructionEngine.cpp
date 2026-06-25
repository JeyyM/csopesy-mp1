#include "InstructionEngine.h"

#include "TimeUtil.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

bool isDigitChar(char ch) {
    return ch >= '0' && ch <= '9';
}

bool isIdentifierChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

}  // namespace

std::string InstructionEngine::trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::vector<std::string> InstructionEngine::splitTopLevelArgs(const std::string& args) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool inQuotes = false;

    for (char ch : args) {
        if (ch == '"' && parenDepth == 0 && bracketDepth == 0) {
            inQuotes = !inQuotes;
            current.push_back(ch);
            continue;
        }
        if (!inQuotes) {
            if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')') {
                --parenDepth;
            } else if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == ',' && parenDepth == 0 && bracketDepth == 0) {
                parts.push_back(trim(current));
                current.clear();
                continue;
            }
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        parts.push_back(trim(current));
    }
    return parts;
}

uint16_t InstructionEngine::parseUint16Literal(const std::string& token, bool& ok) {
    ok = false;
    if (token.empty()) {
        return 0;
    }
    for (char ch : token) {
        if (!isDigitChar(ch)) {
            return 0;
        }
    }
    try {
        const unsigned long value = std::stoul(token);
        if (value > 0xFFFFUL) {
            return 0;
        }
        ok = true;
        return static_cast<uint16_t>(value);
    } catch (...) {
        return 0;
    }
}

uint16_t InstructionEngine::resolveOperand(Process& process, const std::string& token,
                                           bool& ok) {
    const std::string trimmed = trim(token);
    if (trimmed.empty()) {
        ok = false;
        return 0;
    }
    if (isDigitChar(trimmed.front())) {
        return parseUint16Literal(trimmed, ok);
    }
    ok = true;
    return process.getVariable(trimmed);
}

std::string InstructionEngine::resolvePrintMessage(Process& process,
                                                   const std::string& rawMessage) {
    const std::string trimmed = trim(rawMessage);
    if (trimmed.empty()) {
        return process.defaultPrintMessage();
    }

    std::string message = trimmed;
    if (message.front() == '"' && message.back() == '"' && message.size() >= 2) {
        message = message.substr(1, message.size() - 2);
    }

    const std::string plusSpaced = " + ";
    auto plusPos = message.find(plusSpaced);
    if (plusPos == std::string::npos) {
        plusPos = message.find('+');
    }
    if (plusPos != std::string::npos) {
        const std::string left = trim(message.substr(0, plusPos));
        const std::string right = trim(message.substr(plusPos + 1));
        std::string leftText = left;
        if (left.size() >= 2 && left.front() == '"' && left.back() == '"') {
            leftText = left.substr(1, left.size() - 2);
        }
        bool ok = false;
        const uint16_t value = resolveOperand(process, right, ok);
        if (ok) {
            return leftText + std::to_string(value);
        }
    }

    return message;
}

Instruction InstructionEngine::parseSingleInstruction(const std::string& text) {
    std::string trimmed = trim(text);
    Instruction instruction;

    if (trimmed.empty()) {
        return instruction;
    }

    const auto parenPos = trimmed.find('(');
    if (parenPos != std::string::npos && parenPos > 0) {
        const std::string keyword = trim(trimmed.substr(0, parenPos));
        const std::string rest = trimmed.substr(parenPos);
        if (keyword == "ADD" || keyword == "SUBTRACT" || keyword == "DECLARE" ||
            keyword == "SLEEP" || keyword == "FOR" || keyword == "PRINT") {
            trimmed = keyword + rest;
        }
    }

    if (trimmed == "PRINT" || trimmed.rfind("PRINT(", 0) == 0) {
        instruction.type = InstructionType::Print;
        if (trimmed == "PRINT") {
            instruction.arg.clear();
        } else {
            const auto open = trimmed.find('(');
            const auto close = trimmed.rfind(')');
            if (open != std::string::npos && close > open) {
                instruction.arg = trimmed.substr(open + 1, close - open - 1);
            }
        }
        return instruction;
    }

    if (trimmed.rfind("DECLARE(", 0) == 0) {
        instruction.type = InstructionType::Declare;
        instruction.arg = trimmed;
        return instruction;
    }
    if (trimmed.rfind("ADD(", 0) == 0) {
        instruction.type = InstructionType::Add;
        instruction.arg = trimmed;
        return instruction;
    }
    if (trimmed.rfind("SUBTRACT(", 0) == 0) {
        instruction.type = InstructionType::Subtract;
        instruction.arg = trimmed;
        return instruction;
    }
    if (trimmed.rfind("SLEEP(", 0) == 0) {
        instruction.type = InstructionType::Sleep;
        instruction.arg = trimmed;
        return instruction;
    }
    if (trimmed.rfind("FOR(", 0) == 0) {
        instruction.type = InstructionType::For;
        instruction.arg = trimmed;
        return instruction;
    }

    instruction.type = InstructionType::Print;
    instruction.arg = trimmed;
    return instruction;
}

std::vector<Instruction> InstructionEngine::parseInstructionList(const std::string& text) {
    std::vector<Instruction> instructions;
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return instructions;
    }

    std::string current;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool inQuotes = false;

    for (char ch : trimmed) {
        if (ch == '"' && parenDepth == 0 && bracketDepth == 0) {
            inQuotes = !inQuotes;
            current.push_back(ch);
            continue;
        }
        if (!inQuotes) {
            if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')') {
                --parenDepth;
            } else if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == ',' && parenDepth == 0 && bracketDepth == 0) {
                instructions.push_back(parseSingleInstruction(current));
                current.clear();
                continue;
            }
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        instructions.push_back(parseSingleInstruction(current));
    }
    return instructions;
}

bool InstructionEngine::parseForInstruction(const std::string& text,
                                            std::vector<Instruction>& bodyOut,
                                            int& repeatsOut) {
    const std::string trimmed = trim(text);
    if (trimmed.rfind("FOR(", 0) != 0) {
        return false;
    }

    const auto open = trimmed.find('(');
    const auto close = trimmed.rfind(')');
    if (open == std::string::npos || close <= open) {
        return false;
    }

    const std::vector<std::string> args =
        splitTopLevelArgs(trimmed.substr(open + 1, close - open - 1));
    if (args.size() != 2) {
        return false;
    }

    std::string bodyText = trim(args[0]);
    if (bodyText.front() == '[' && bodyText.back() == ']') {
        bodyText = bodyText.substr(1, bodyText.size() - 2);
    }

    bool ok = false;
    const uint16_t repeats = parseUint16Literal(trim(args[1]), ok);
    if (!ok || repeats < 1) {
        return false;
    }

    bodyOut = parseInstructionList(bodyText);
    repeatsOut = repeats;
    return !bodyOut.empty();
}

InstructionEngine::ExecuteResult InstructionEngine::executeBlock(
    Process& process, const std::vector<Instruction>& body, int coreId, int nestingDepth) {
    ExecuteResult aggregate;
    for (const Instruction& instruction : body) {
        if (instruction.type == InstructionType::For) {
            if (nestingDepth >= kMaxForDepth) {
                continue;
            }
            std::vector<Instruction> innerBody;
            int repeats = 0;
            if (!parseForInstruction(instruction.arg, innerBody, repeats)) {
                continue;
            }
            for (int iteration = 0; iteration < repeats; ++iteration) {
                ExecuteResult inner =
                    executeBlock(process, innerBody, coreId, nestingDepth + 1);
                if (inner.producedLog) {
                    aggregate.producedLog = true;
                    aggregate.logLine = std::move(inner.logLine);
                }
                if (inner.relinquishCpu) {
                    aggregate.relinquishCpu = true;
                    aggregate.sleepTicks = inner.sleepTicks;
                    return aggregate;
                }
            }
            continue;
        }

        ExecuteResult step = execute(process, instruction, coreId);
        if (step.producedLog) {
            aggregate.producedLog = true;
            aggregate.logLine = std::move(step.logLine);
        }
        if (step.relinquishCpu) {
            aggregate.relinquishCpu = true;
            aggregate.sleepTicks = step.sleepTicks;
            return aggregate;
        }
    }
    return aggregate;
}

InstructionEngine::ExecuteResult InstructionEngine::execute(Process& process,
                                                            const Instruction& instruction,
                                                            int coreId) {
    ExecuteResult result;

    switch (instruction.type) {
        case InstructionType::Print: {
            const std::string message = resolvePrintMessage(process, instruction.arg);
            std::ostringstream logLine;
            logLine << "(" << TimeUtil::formatNow() << ") Core:" << coreId << " \"" << message
                    << "\"";
            result.producedLog = true;
            result.logLine = logLine.str();
            break;
        }
        case InstructionType::Declare: {
            const auto open = instruction.arg.find('(');
            const auto close = instruction.arg.rfind(')');
            if (open == std::string::npos || close <= open) {
                break;
            }
            const std::vector<std::string> args =
                splitTopLevelArgs(instruction.arg.substr(open + 1, close - open - 1));
            if (args.size() != 2) {
                break;
            }
            bool ok = false;
            const uint16_t value = parseUint16Literal(args[1], ok);
            if (ok) {
                process.setVariable(args[0], value);
            }
            break;
        }
        case InstructionType::Add:
        case InstructionType::Subtract: {
            const auto open = instruction.arg.find('(');
            const auto close = instruction.arg.rfind(')');
            if (open == std::string::npos || close <= open) {
                break;
            }
            const std::vector<std::string> args =
                splitTopLevelArgs(instruction.arg.substr(open + 1, close - open - 1));
            if (args.size() != 3) {
                break;
            }
            bool ok1 = false;
            bool ok2 = false;
            const uint16_t left = resolveOperand(process, args[1], ok1);
            const uint16_t right = resolveOperand(process, args[2], ok2);
            if (!ok1 || !ok2) {
                break;
            }
            uint32_t computed = instruction.type == InstructionType::Add ? left + right
                                                                         : left - right;
            if (computed > 0xFFFFU) {
                computed = 0xFFFFU;
            }
            process.setVariable(args[0], static_cast<uint16_t>(computed));
            break;
        }
        case InstructionType::Sleep: {
            const auto open = instruction.arg.find('(');
            const auto close = instruction.arg.rfind(')');
            if (open == std::string::npos || close <= open) {
                break;
            }
            bool ok = false;
            const uint16_t ticks =
                parseUint16Literal(trim(instruction.arg.substr(open + 1, close - open - 1)), ok);
            if (ok && ticks > 0) {
                result.relinquishCpu = true;
                result.sleepTicks = ticks;
            }
            break;
        }
        case InstructionType::For:
            break;
    }

    return result;
}
