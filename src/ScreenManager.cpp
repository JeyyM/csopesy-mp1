// Handles all commands that start with the word "screen".

// The main command loop in main.cpp recognizes "screen" commands and
// forwards them here. ScreenManager then decides which specific "screen"
// action the user wants:
//
// screen          -> show usage hint (no sub-command given)
// screen -ls      -> list every process (name, status, core, progress)
// screen -s NAME  -> create a brand-new process called NAME and open it
// screen -r NAME  -> re-attach to an existing process to inspect it


#include "ScreenManager.h"

#include "ConsoleManager.h"
#include "InstructionEngine.h"
#include "ProcessModel.h"
#include "ReportManager.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Strips spaces, tabs, and newline characters from both ends of a string.
// Same helper as in main.cpp — needed here to clean up process names
// after slicing them out of commands like "screen -s  myprocess ".
//
// Example: IN "  myprocess " -> OUT "myprocess"
std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// Returns true if `text` begins with `prefix`.
// Used to detect "screen -s " and "screen -r " sub-commands without
// needing to tokenize the whole string.
//
// Example: startsWith("screen -s myProc", "screen -s ") -> true
// Example: startsWith("screen -ls",       "screen -s ") -> false
bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// Splits a string into whitespace-delimited tokens.
// Example: "p1 256" -> ["p1", "256"]
std::vector<std::string> splitWhitespace(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Parses an unsigned decimal memory-size token. Returns false on bad input.
bool parseMemorySize(const std::string& token, uint32_t& out) {
    if (token.empty()) {
        return false;
    }
    for (char ch : token) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    try {
        const unsigned long long value = std::stoull(token);
        if (value > 0xFFFFFFFFULL) {
            return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

// Formats an address as "0x" + uppercase hex (e.g. 0x1F40).
std::string toHex(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << value;
    return oss.str();
}


// runProcessScreen — the inner command loop for an open process window
//
// Called after "screen -s <name>" creates a process OR "screen -r <name>"
// re-attaches to one. Shows the process's live info and accepts two commands:
//   process-smi — refresh / redisplay the process info panel
//   exit        — close this process window and return to the main menu
//
// The user is "inside" this process screen until they type "exit".
// While here, the scheduler is still running the process in the background
// on a CPU core — the user is just observing.
void runProcessScreen(const std::shared_ptr<Process>& process) {
    // Show process details immediately when the screen opens.
    // formatSmi() builds a snapshot of name, ID, creation time, logs,
    // current line / total lines, variable values.
    ConsoleManager::clearScreen();
    std::cout << process->formatSmi();
    ConsoleManager::printProcessScreenHint(process->name());

    // Inner command loop — stays here until "exit" is typed.
    while (true) {
        ConsoleManager::printPrompt();

        std::string command;
        // If input stream closes (Ctrl+D), treat it as "exit" from this screen.
        if (!std::getline(std::cin, command)) {
            return;
        }
        // Ignore blank Enter presses.
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        // process-smi command:
        // Reprints the live process info: logs, current instruction, variables.
        // The scheduler may have made progress since the last time we printed,
        // so this always fetches a fresh snapshot.
        if (command == "process-smi") {
            std::cout << process->formatSmi();
            continue;
        }

        // exit command:
        // Returns the user to the main menu.
        // Note: this "exit" only exits the process screen, not the whole
        // program. The main loop's own "exit" check handles quitting entirely.
        if (command == "exit") {
            ConsoleManager::clearScreen(); 
            ConsoleManager::printHeader();  // restore the main header
            return;                         // back to handleCommand, then main.cpp
        }

        // Any other input is not recognized inside a process screen.
        ConsoleManager::printLine("Unknown command inside process screen.");
    }
}

}  // namespace

// ScreenManager::isScreenCommand
//
// Called by main.cpp before handleCommand to quickly decide if a command
// belongs to this file at all. Returns true for any recognized "screen" form.
//
// Recognized patterns:
//   "screen"          -> bare command (no sub-command; usage hint will be shown)
//   "screen -ls"      -> list all processes
//   "screen -s ..."   -> create new process  (anything after "-s " is the name)
//   "screen -r ..."   -> attach to process   (anything after "-r " is the name)
//
// Example: IN "screen -s myProc" -> true
// Example: IN "report-util"      -> false
bool ScreenManager::isScreenCommand(const std::string& command) {
    return command == "screen" || command == "screen -ls" ||
           startsWith(command, "screen -s ") || startsWith(command, "screen -r ") ||
           startsWith(command, "screen -c ");
}

// ScreenManager::handleCommand
//
// The main dispatcher for all "screen" sub-commands. Called from main.cpp
// after isScreenCommand() returns true.
//
// Parameters:
//   command   — the full trimmed string the user typed
//   scheduler — used to create processes, find processes, and query state
//   config    — needed when auto-starting the engine via "screen -s"
//
// Returns true always (the bool return is a legacy signal to main.cpp).
//
// PSEUDOCODE:
//   match command against each "screen" variant:
//     "screen"         -> print usage
//     "screen -ls"     -> print process table
//     "screen -s NAME" -> create + open process
//     "screen -r NAME" -> find + open process
bool ScreenManager::handleCommand(const std::string& command, Scheduler& scheduler,
                                  const Config& config) {

    // "screen" with no sub-command — user probably forgot the flag.
    // Print a reminder of the three available forms.
    if (command == "screen") {
        ConsoleManager::printLine(
            "screen command requires arguments: screen -ls, "
            "screen -s <name> <mem>, screen -c <name> <mem> \"<instructions>\", "
            "or screen -r <name>.");
        return true;
    }

    // "screen -ls" — list all processes in a formatted table.
    //
    // Generates the same status report as "report-util" but prints it to the
    // terminal instead of saving it to a file. Shows:
    //   - Processes currently running (which core, how far along)
    //   - Processes waiting in the ready queue
    //   - Processes that have finished
    //   - CPU utilization percentage
    if (command == "screen -ls") {
        std::cout << ReportManager::generateSystemReport(scheduler);
        ConsoleManager::printLsAttachHint(); // After the table, prints a tip:
        return true;
    }

    // Prefixes used to extract arguments from sub-commands.
    const std::string createPrefix = "screen -s ";
    const std::string customPrefix = "screen -c ";
    const std::string attachPrefix = "screen -r ";

    // "screen -s <name> <memory-size>" — create a process with a standard
    // program and the given memory size, then open its screen.
    //
    //   1. Tokenize the arguments after "screen -s ": expect <name> <size>.
    //   2. Validate the memory size (power of two in [64, 65536]).
    //   3. Reject a duplicate name.
    //   4. Auto-start the engine if needed, create the process, open its screen.
    if (startsWith(command, createPrefix)) {
        const std::vector<std::string> args =
            splitWhitespace(command.substr(createPrefix.size()));

        if (args.size() != 2) {
            ConsoleManager::printLine(
                "Usage: screen -s <process name> <process memory size>.");
            return true;
        }

        const std::string name = args[0];
        uint32_t memoryBytes = 0;
        if (!parseMemorySize(args[1], memoryBytes) ||
            !isValidProcessMemorySize(memoryBytes)) {
            ConsoleManager::printLine(
                "Invalid memory allocation. Size must be a power of two in "
                "[64, 65536] bytes.");
            return true;
        }

        if (scheduler.processExists(name)) {
            ConsoleManager::printLine("Process " + name + " already exists.");
            return true;
        }

        scheduler.ensureEngineRunning(config);
        auto process = scheduler.createUserProcess(name, memoryBytes);
        runProcessScreen(process);
        return true;
    }

    // "screen -c <name> <memory-size> "<instructions>"" — create a process with
    // a caller-supplied, semicolon-separated instruction list.
    //
    //   1. Locate the quoted instruction block.
    //   2. Tokenize <name> <size> from the text before the quote.
    //   3. Validate memory size and the instruction count (1..50).
    //   4. Reject duplicates, then create and open the process.
    if (startsWith(command, customPrefix)) {
        const std::string rest = command.substr(customPrefix.size());

        const auto firstQuote = rest.find('"');
        const auto lastQuote = rest.rfind('"');
        if (firstQuote == std::string::npos || lastQuote == firstQuote) {
            ConsoleManager::printLine(
                "Usage: screen -c <process name> <process memory size> "
                "\"<instructions>\".");
            return true;
        }

        const std::string header = trim(rest.substr(0, firstQuote));
        const std::string body = rest.substr(firstQuote + 1, lastQuote - firstQuote - 1);
        const std::vector<std::string> args = splitWhitespace(header);

        if (args.size() != 2) {
            ConsoleManager::printLine(
                "Usage: screen -c <process name> <process memory size> "
                "\"<instructions>\".");
            return true;
        }

        const std::string name = args[0];
        uint32_t memoryBytes = 0;
        if (!parseMemorySize(args[1], memoryBytes) ||
            !isValidProcessMemorySize(memoryBytes)) {
            ConsoleManager::printLine(
                "Invalid memory allocation. Size must be a power of two in "
                "[64, 65536] bytes.");
            return true;
        }

        const std::vector<Instruction> program = InstructionEngine::parseUserProgram(body);
        if (program.empty() || program.size() > 50) {
            ConsoleManager::printLine(
                "Invalid command. A custom process must have 1 to 50 instructions.");
            return true;
        }

        if (scheduler.processExists(name)) {
            ConsoleManager::printLine("Process " + name + " already exists.");
            return true;
        }

        scheduler.ensureEngineRunning(config);
        auto process = scheduler.createCustomProcess(name, memoryBytes, program);
        runProcessScreen(process);
        return true;
    }

    // "screen -r <name>" — re-attach to an existing process.
    //
    //   1. Slice off "screen -r " prefix to get the bare process name.
    //   2. Reject an empty name.
    //   3. If the process was shut down by a memory access violation, print the
    //      violation message instead of attaching.
    //   4. Reject if not found or finished normally; otherwise open its screen.
    if (startsWith(command, attachPrefix)) {
        const std::string name = trim(command.substr(attachPrefix.size()));

        if (name.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }

        auto process = scheduler.findProcess(name);

        // Memory-violation report takes priority over the "not found" message.
        if (process &&
            process->terminationReason() == TerminationReason::MemoryViolation) {
            ConsoleManager::printLine(
                "Process " + name +
                " shut down due to memory access violation error that occurred at " +
                process->violationTime() + ". " + toHex(process->violationAddress()) +
                " invalid.");
            return true;
        }

        if (!process || process->status() == ProcessStatus::Finished) {
            ConsoleManager::printLine("Process " + name + " not found.");
            return true;
        }

        runProcessScreen(process);
        return true;
    }

    // Should never reach here because isScreenCommand() pre-filters commands,
    // but return false as a safety signal to the caller.
    return false;
}
