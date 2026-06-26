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
#include "ProcessModel.h"
#include "ReportManager.h"

#include <iostream>
#include <memory>
#include <string>

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
           startsWith(command, "screen -s ") || startsWith(command, "screen -r ");
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
            "screen command requires arguments: screen -ls, screen -s <process name>, "
            "or screen -r <process name>.");
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

    // Prefixes used to extract the process name from sub-commands.
    // Everything after the prefix (trimmed) is treated as the process name.
    const std::string createPrefix = "screen -s ";
    const std::string attachPrefix = "screen -r ";

    // "screen -s <name>" — create a new process and open its screen.
    //
    // Step-by-step:
    //   1. Slice off "screen -s " prefix to get the bare process name.
    //   2. Reject an empty name (user typed "screen -s " with nothing after).
    //   3. Reject a duplicate name (process already exists).
    //   4. Auto-start the engine if "scheduler-start" hasn't been run yet.
    //      (screen -s is allowed to implicitly start the scheduler.)
    //   5. Create the process and add it to the scheduler's ready queue.
    //   6. Open the process screen so the user can watch it run.
    if (startsWith(command, createPrefix)) {
        // Step 1: Extract process name (everything after "screen -s ").
        const std::string name = trim(command.substr(createPrefix.size()));

        // Step 2: Name must not be blank.
        if (name.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }

        // Step 3: Each process name must be unique across the whole session.
        if (scheduler.processExists(name)) {
            ConsoleManager::printLine("Process " + name + " already exists.");
            return true;
        }
        // Step 4: If the scheduler worker threads haven't been started yet
        // (because the user hasn't typed "scheduler-start"), start them now.
        // This lets "screen -s" work even without an explicit "scheduler-start".
        scheduler.ensureEngineRunning(config);

        // Step 5: Create the process (assigns it an ID, adds a standard program,
        // sets initial variables x=0, y=0, z=0, and puts it in the ready queue).
        auto process = scheduler.createUserProcess(name);

        // Step 6: Enter the interactive process screen for this new process.
        // This blocks (loops) until the user types "exit" from the process screen.
        runProcessScreen(process);
        return true;
    }

    // "screen -r <name>" — re-attach to an existing process.
    //
    // Step-by-step:
    //   1. Slice off "screen -r " prefix to get the bare process name.
    //   2. Reject an empty name.
    //   3. Look up the process by name in the scheduler's full process list.
    //   4. Reject if not found OR if the process has already finished
    //      (a finished process has no new updates to show; use -ls instead).
    //   5. Open the process screen for the found process.
    if (startsWith(command, attachPrefix)) {

        // Step 1: Extract process name (everything after "screen -r ").
        const std::string name = trim(command.substr(attachPrefix.size()));

        // Step 2: Name must not be blank.
        if (name.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }

        // Step 3: Find the process by name (searches allProcesses_ in Scheduler).
        auto process = scheduler.findProcess(name);

        // Step 4: If it doesn't exist yet, or it's already done, reject.
        // Finished processes don't accept new log entries; screen -ls shows them.
        if (!process || process->status() == ProcessStatus::Finished) {
            ConsoleManager::printLine("Process " + name + " not found.");
            return true;
        }

        // Step 5: Open the process screen (same view as after screen -s).
        runProcessScreen(process);
        return true;
    }

    // Should never reach here because isScreenCommand() pre-filters commands,
    // but return false as a safety signal to the caller.
    return false;
}
