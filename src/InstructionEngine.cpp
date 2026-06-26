// InstructionEngine.cpp
//
// Implements parsing and execution of CSOPESY instructions.
//
// Two main jobs:
//   1. PARSING — turn strings like ADD(x, x, 1) into Instruction { type, arg }
//   2. EXECUTION — apply one instruction to a Process (variables, logs, sleep)
//
// Does NOT schedule threads or assign CPU cores — Scheduler.cpp calls this
// when a process is actually running on a core.
//
// ========== WORKED EXAMPLES (read this first) ==========
//
// 1) splitTopLevelArgs — split function arguments on commas
//      IN:  "x, x, 1"           (inside ADD(...))
//      OUT: ["x", "x", "1"]
//
//      IN:  "[ADD(x,x,1), PRINT(\"hi\")], 100"   (inside FOR(...))
//      OUT: ["[ADD(x,x,1), PRINT(\"hi\")]", "100"]
//
// 2) parseSingleInstruction — one text line -> Instruction struct
//      IN:  "ADD(x, x, 1)"
//      OUT: { type=Add, arg="ADD(x, x, 1)" }
//
//      IN:  "PRINT(\"Value from: \" + x)"
//      OUT: { type=Print, arg="\"Value from: \" + x" }   // only inside PRINT(...)
//
// 3) parseInstructionList — comma-separated list inside FOR brackets
//      IN:  "ADD(x, x, 1), PRINT(\"Value from: \" + x)"
//      OUT: [ Add instruction, Print instruction ]  (vector of 2)
//
// 4) parseForInstruction — unpack FOR([body], repeats)
//      IN:  "FOR([ADD(x,x,1), PRINT(\"Value from: \" + x)], 100)"
//      OUT: bodyOut = [ADD..., PRINT...], repeatsOut = 100, return true
//
// 5) resolveOperand — one ADD argument -> number
//      IN:  token "1"     -> 1
//      IN:  token "x"     -> process.getVariable("x")  (e.g. 4 if x was 4)
//
// 6) resolvePrintMessage — build string that PRINT will show
//      IN:  arg empty, process name proc-01
//      OUT: "Hello world from proc-01!"
//
//      IN:  arg "\"Value from: \" + x", x=3
//      OUT: "Value from: 3"
//
// 7) execute(Print) — run one PRINT
//      IN:  Print with x=3, coreId=0
//      OUT: ExecuteResult { producedLog=true,
//            logLine="(06/26/2026 ...) Core:0 \"Value from: 3\"" }
//
// 8) execute(Add) — run ADD(x, x, 1) when x=3
//      OUT: x becomes 4, no log line (producedLog=false)
//
// 9) execute(Sleep) — run SLEEP(5)
//      OUT: ExecuteResult { relinquishCpu=true, sleepTicks=5 }
//      (Scheduler parks process until global cpuCycles + 5)
//
// 10) executeBlock — run a whole list (e.g. one FOR iteration body)
//      Runs instructions in order; if any step returns relinquishCpu, stops early.
//
// ========================================================

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

// ---------------------------------------------------------------------------
// String utilities
// ---------------------------------------------------------------------------

// Removes leading/trailing whitespace.
// Example: IN "  x  "  -> OUT "x"
// Example: IN "   "    -> OUT ""
std::string InstructionEngine::trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// Splits on commas only at the "top level" of the string.
//
// Example: IN "x, y, 1"  (from ADD(x, y, 1) after stripping "ADD(...)")
//          OUT ["x", "y", "1"]
//
// Example: IN "\"hello, world\", x"  (comma inside quotes is NOT a split)
//          OUT ["\"hello, world\"", "x"]
//
// Example: IN "[ADD(a,b), PRINT(c)], 100"  (FOR's two args)
//          OUT ["[ADD(a,b), PRINT(c)]", "100"]
std::vector<std::string> InstructionEngine::splitTopLevelArgs(const std::string& args) {
    // Step 1: Set up trackers — one token being built, depths for nesting, quote flag.
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool inQuotes = false;

    // Step 2: Walk the string character by character.
    for (char ch : args) {
        // Step 2a: At top level, " toggles quote mode (ignore commas inside quotes).
        if (ch == '"' && parenDepth == 0 && bracketDepth == 0) {
            inQuotes = !inQuotes;
            current.push_back(ch);
            continue;
        }

        if (!inQuotes) {
            // Step 2b: Track nesting so commas inside ADD(...) or FOR[...] don't split.
            if (ch == '(') {
                ++parenDepth;
            } else if (ch == ')') {
                --parenDepth;
            } else if (ch == '[') {
                ++bracketDepth;
            } else if (ch == ']') {
                --bracketDepth;
            } else if (ch == ',' && parenDepth == 0 && bracketDepth == 0) {
                // Step 2c: Top-level comma — finish current arg, start a new one.
                parts.push_back(trim(current));
                current.clear();
                continue;
            }
        }

        // Step 2d: Normal character — append to the arg we are building.
        current.push_back(ch);
    }

    // Step 3: Push the final arg (no trailing comma after last token).
    if (!current.empty()) {
        parts.push_back(trim(current));
    }
    return parts;
}

// Converts a text number into uint16 (0..65535).
//
// Example: IN "100"  -> OUT value=100, ok=true
// Example: IN "abc"  -> OUT value=0,   ok=false
// Example: IN "70000"-> OUT value=0,   ok=false  (too big for uint16)
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

// Resolves one operand in ADD/SUBTRACT/PRINT: literal or variable name.
//
// Example: IN token "1", process any  -> OUT 1, ok=true
// Example: IN token "x", x=7 in process -> OUT 7, ok=true
// Example: IN token "z", z never set   -> OUT 0, ok=true  (missing var = 0)
// Example: IN token ""                 -> OUT 0, ok=false
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

// Turns PRINT's stored arg into the final message text.
//
// Example: IN arg="" , process "proc-01"
//          OUT "Hello world from proc-01!"
//
// Example: IN arg="\"Value from: \" + x", x=3
//          OUT "Value from: 3"
//
// Example: IN arg="\"hello\""  (plain quoted string, no + variable)
//          OUT "hello"
std::string InstructionEngine::resolvePrintMessage(Process& process,
                                                   const std::string& rawMessage) {
    // Step 1: Empty arg -> default "Hello world from <name>!"
    const std::string trimmed = trim(rawMessage);
    if (trimmed.empty()) {
        return process.defaultPrintMessage();
    }

    // Step 2: If whole message is wrapped in quotes, strip outer quotes.
    std::string message = trimmed;
    if (message.front() == '"' && message.back() == '"' && message.size() >= 2) {
        message = message.substr(1, message.size() - 2);
    }

    // Step 3: Look for " + " (or bare '+') splitting text and variable/literal.
    const std::string plusSpaced = " + ";
    auto plusPos = message.find(plusSpaced);
    std::size_t plusSkip = 0;
    if (plusPos == std::string::npos) {
        plusPos = message.find('+');
        plusSkip = 1;
    } else {
        plusSkip = plusSpaced.size();
    }

    // Step 4: If we found '+', concatenate left text with resolved right operand.
    if (plusPos != std::string::npos) {
        const std::string left = trim(message.substr(0, plusPos));
        const std::string right = trim(message.substr(plusPos + plusSkip));
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

    // Step 5: No '+' pattern — return message as-is.
    return message;
}

// ---------------------------------------------------------------------------
// Parsing: text -> Instruction
// ---------------------------------------------------------------------------

// Parses ONE instruction string into { type, arg }.
//
// Example: IN "ADD(x, x, 1)"
//          OUT { type=Add, arg="ADD(x, x, 1)" }
//
// Example: IN "PRINT(\"Value from: \" + x)"
//          OUT { type=Print, arg="\"Value from: \" + x" }
//
// Example: IN "PRINT"
//          OUT { type=Print, arg="" }  -> uses default message at run time
Instruction InstructionEngine::parseSingleInstruction(const std::string& text) {
    // Step 1: Trim and bail on empty input.
    std::string trimmed = trim(text);
    Instruction instruction;

    if (trimmed.empty()) {
        return instruction;
    }

    // Step 2: Normalize "ADD (x, ...)" -> "ADD(x, ...)" (remove space before '(').
    const auto parenPos = trimmed.find('(');
    if (parenPos != std::string::npos && parenPos > 0) {
        const std::string keyword = trim(trimmed.substr(0, parenPos));
        const std::string rest = trimmed.substr(parenPos);
        if (keyword == "ADD" || keyword == "SUBTRACT" || keyword == "DECLARE" ||
            keyword == "SLEEP" || keyword == "FOR" || keyword == "PRINT") {
            trimmed = keyword + rest;
        }
    }

    // Step 3: Detect instruction type by prefix and fill { type, arg }.
    if (trimmed == "PRINT" || trimmed.rfind("PRINT(", 0) == 0) {
        instruction.type = InstructionType::Print;
        if (trimmed == "PRINT") {
            instruction.arg.clear();
        } else {
            // Store only inside parentheses for PRINT.
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

    // Step 4: Unknown text — treat as PRINT payload.
    instruction.type = InstructionType::Print;
    instruction.arg = trimmed;
    return instruction;
}

// Parses several instructions separated by top-level commas (FOR body text).
//
// Example: IN "ADD(x, x, 1), PRINT(\"Value from: \" + x)"
//          OUT vector of 2 Instructions: [Add, Print]
//
// Used after stripping [ ] from FOR body: the inner text between brackets.
std::vector<Instruction> InstructionEngine::parseInstructionList(const std::string& text) {
    std::vector<Instruction> instructions;
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return instructions;
    }

    // Step 1: Same scanning setup as splitTopLevelArgs — split on top-level commas.
    std::string current;
    int parenDepth = 0;
    int bracketDepth = 0;
    bool inQuotes = false;

    // Step 2: Scan char-by-char; each completed segment -> parseSingleInstruction.
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

    // Step 3: Parse and append the last instruction in the list.
    if (!current.empty()) {
        instructions.push_back(parseSingleInstruction(current));
    }
    return instructions;
}

// Parses a full FOR(...) instruction into loop body + repeat count.
//
// Example: IN "FOR([ADD(x,x,1), PRINT(\"Value from: \" + x)], 100)"
//          OUT bodyOut = [ADD..., PRINT...], repeatsOut = 100, returns true
//
// Example: IN "FOR([], 0)"  -> returns false  (empty body or invalid repeats)
bool InstructionEngine::parseForInstruction(const std::string& text,
                                            std::vector<Instruction>& bodyOut,
                                            int& repeatsOut) {
    // Step 1: Must start with "FOR(".
    const std::string trimmed = trim(text);
    if (trimmed.rfind("FOR(", 0) != 0) {
        return false;
    }

    // Step 2: Find outer parentheses — content between ( and last ).
    const auto open = trimmed.find('(');
    const auto close = trimmed.rfind(')');
    if (open == std::string::npos || close <= open) {
        return false;
    }

    // Step 3: Split inner content into exactly two args: [body], repeats.
    const std::vector<std::string> args =
        splitTopLevelArgs(trimmed.substr(open + 1, close - open - 1));
    if (args.size() != 2) {
        return false;
    }

    // Step 4: Strip [ ] brackets from body text if present.
    std::string bodyText = trim(args[0]);
    if (bodyText.front() == '[' && bodyText.back() == ']') {
        bodyText = bodyText.substr(1, bodyText.size() - 2);
    }

    // Step 5: Parse repeat count (must be >= 1).
    bool ok = false;
    const uint16_t repeats = parseUint16Literal(trim(args[1]), ok);
    if (!ok || repeats < 1) {
        return false;
    }

    // Step 6: Parse body into instruction list; succeed only if non-empty.
    bodyOut = parseInstructionList(bodyText);
    repeatsOut = repeats;
    return !bodyOut.empty();
}

// ---------------------------------------------------------------------------
// Execution: Instruction -> changes on Process
// ---------------------------------------------------------------------------

// Runs a vector of instructions in order (one FOR iteration body, etc.).
//
// Example: body = [ADD(x,x,1), PRINT(...)], x starts at 0
//          -> x becomes 1, last PRINT log line returned in aggregate.logLine
//
// Example: body contains SLEEP(3) mid-way
//          -> stops immediately, returns relinquishCpu=true, sleepTicks=3
//
// Nested FOR: if body has FOR inside, parseForInstruction + recursive executeBlock.
// Max nesting depth = kMaxForDepth (3).
InstructionEngine::ExecuteResult InstructionEngine::executeBlock(
    Process& process, const std::vector<Instruction>& body, int coreId, int nestingDepth) {
    ExecuteResult aggregate;

    // Step 1: Run each instruction in the body, left to right.
    for (const Instruction& instruction : body) {
        if (instruction.type == InstructionType::For) {
            // Step 2a: Nested FOR — enforce max depth, parse, loop recursively.
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

        // Step 2b: Normal instruction — delegate to execute().
        ExecuteResult step = execute(process, instruction, coreId);
        if (step.producedLog) {
            aggregate.producedLog = true;
            aggregate.logLine = std::move(step.logLine);
        }
        if (step.relinquishCpu) {
            // Step 3: SLEEP (or similar) — stop the block immediately.
            aggregate.relinquishCpu = true;
            aggregate.sleepTicks = step.sleepTicks;
            return aggregate;
        }
    }

    // Step 4: All instructions finished without sleep preemption.
    return aggregate;
}

// Runs exactly ONE Instruction on a Process. Returns what happened to Scheduler.
//
// Example PRINT:  arg="\"Value from: \" + x", x=2, coreId=0
//   -> producedLog=true, logLine="(date) Core:0 \"Value from: 2\""
//
// Example ADD:    arg="ADD(x, x, 1)", x=2
//   -> x becomes 3 in process, producedLog=false
//
// Example DECLARE: arg="DECLARE(counter, 5)"
//   -> counter=5, producedLog=false
//
// Example SUBTRACT: arg="SUBTRACT(a, 10, 3)"
//   -> a=7, producedLog=false
//
// Example SLEEP:  arg="SLEEP(5)"
//   -> relinquishCpu=true, sleepTicks=5  (Scheduler handles the wait)
//
// Example FOR:    handled elsewhere (executeBlock / Scheduler::runInstructionTree)
InstructionEngine::ExecuteResult InstructionEngine::execute(Process& process,
                                                            const Instruction& instruction,
                                                            int coreId) {
    ExecuteResult result;

    switch (instruction.type) {
        case InstructionType::Print: {
            // Step 1: Build message. Step 2: Format log line. Step 3: Flag producedLog.
            const std::string message = resolvePrintMessage(process, instruction.arg);
            std::ostringstream logLine;
            logLine << "(" << TimeUtil::formatNow() << ") Core:" << coreId << " \"" << message
                    << "\"";
            result.producedLog = true;
            result.logLine = logLine.str();
            break;
        }
        case InstructionType::Declare: {
            // Step 1: Extract inside DECLARE(...). Step 2: Split name, value. Step 3: setVariable.
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
            // Step 1: Extract inside ADD/SUBTRACT(...).
            // Step 2: Split dest, src1, src2.
            // Step 3: Resolve src operands. Step 4: Compute. Step 5: Store in dest.
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
            // Step 1: Extract tick count. Step 2: Tell Scheduler to relinquish CPU.
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
            // No-op here — FOR runs via executeBlock or Scheduler::runInstructionTree.
            break;
    }

    return result;
}
