/*
cd "c:\Users\asus\Desktop\CSOPESY OS MP"
cmake --build build
.\build\csopesy_os_mp.exe
*/

// 1. Starts up the terminal UI (prints the header)
// 2. Sits in a loop waiting for the user to type a command
// 3. Reads each command, figures out what it means, and routes it to
//    the right piece of code
// 4. Keeps looping until the user types "exit"

// FILES THIS TOUCHES:
//   - config.txt          (read by "initialize")
//   - csopesy-log.txt     (written by "report-util")
//   - outputs/ folder     (cleared by "outputs-clear" and "exit")

// Most commands require the system to have been initialized first.
// "initialize" and "outputs-clear" are the only commands that work
// before "initialize" has been run. Everything else prints a reminder.


#include "Config.h"

#include "ConsoleManager.h"

#include "OutputManager.h"
#include "ReportManager.h"

#include "ScreenManager.h"

#include "Scheduler.h"



#include <filesystem>
#include <iostream>
#include <string>



namespace {


// Strips spaces, tabs, and newline characters from both ends of a string.
// This prevents "initialize  " (with trailing spaces) from being treated as
// an unknown command.
//
// Example: IN "  initialize  "  -> OUT "initialize"
// Example: IN "\tclear\r\n"     -> OUT "clear"
std::string trim(const std::string& value) {

    const auto start = value.find_first_not_of(" \t\r\n");

    if (start == std::string::npos) {

        return ""; // string was all whitespace

    }

    const auto end = value.find_last_not_of(" \t\r\n");

    return value.substr(start, end - start + 1);

}


// Prints the reminder shown when a command is used before "initialize".
// Kept as a function so the message is consistent everywhere.
void printNotInitialized() {

    ConsoleManager::printLine(

        "Please initialize the system first by typing \"initialize\".");

}


// Returns a human-readable name for the configured scheduler type.
// Used in the confirmation message after "initialize" succeeds.
//
// Example: config.scheduler == RR    -> "Round Robin"
// Example: config.scheduler == FCFS  -> "FCFS"
std::string schedulerLabel(const Config& config) {

    return config.scheduler == SchedulerType::RR ? "Round Robin" : "FCFS";

}



}  // namespace


// main() — the command interpreter loop
int main() {
    // create the two core objects used by all commands
    //
    // config  : holds settings loaded from config.txt (CPU count, scheduler type, etc.)
    // scheduler: manages worker threads, the ready queue, and process execution
    Config config;
    Scheduler scheduler;


    // initialized: becomes true after "initialize" succeeds.
    //              Acts as a gate — most commands require this to be true.
    // running:     becomes false when "exit" is typed, ending the loop.
    bool initialized = false;

    bool running = true;


    // Clear the terminal and show the ASCII art header + welcome text.
    ConsoleManager::clearScreen();

    ConsoleManager::printHeader();

    // MAIN COMMAND LOOP
    // Runs until the user types "exit" (sets running = false).
    // Each iteration: print prompt -> read line -> strip spaces -> dispatch.
    while (running) {
        // Print "root:\> " and wait for the user to press Enter.
        ConsoleManager::printPrompt();



        std::string command;
        // getline reads the full line (including spaces).
        // Returns false at end-of-file (Ctrl+D on Linux, Ctrl+Z on Windows).
        if (!std::getline(std::cin, command)) {

            break;

        }

        // Remove any surrounding whitespace so " exit " works the same as "exit".
        command = trim(command);

        // Ignore blank lines (user just pressed Enter with no text).
        if (command.empty()) {

            continue;

        }


        // exit command:
        // Stops the scheduler and quits. Output/stamp files are preserved
        // so the grader can inspect them after the program closes.
        if (command == "exit") {
            scheduler.stop();
            ConsoleManager::printLine("Exiting CSOPESY Emulator.");
            running = false;
            continue;
        }


        // initialize command:
        // Reads config.txt and loads all scheduler settings.
        // Must be run once before most other commands will work.
        //
        // What it does step-by-step:
        //   1. Make sure it hasn't already been called (can't initialize twice).
        //   2. Ask ConfigLoader to open and parse config.txt.
        //   3. If parsing fails (bad file, missing key), print the error and stop.
        //   4. Mark the system as initialized and print a summary of the settings.
        //   5. Remind the user they need to type "scheduler-start" to begin.
        if (command == "initialize") {
            
            if (initialized) {
                // Guard: calling "initialize" a second time does nothing.
                ConsoleManager::printLine("System is already initialized.");

                continue;

            }

            std::string error;
            // ConfigLoader::loadFromFile opens config.txt, parses every key-value
            // pair, validates ranges, and fills in the `config` struct.
            // If anything is wrong it puts an error message in `error` and returns false.
            if (!ConfigLoader::loadFromFile("config.txt", config, error)) {

                ConsoleManager::printLine(error); // e.g. "config.txt is missing num-cpu"

                continue;

            }


            // Config loaded successfully — flip the gate so other commands unlock.
            initialized = true;

            // Print a confirmation showing what was loaded.
            ConsoleManager::printLine("System initialized successfully using config.txt.");

            ConsoleManager::printLine("Declared " + std::to_string(config.numCpu) +

                                      " CPU cores. Scheduler: " + schedulerLabel(config) +

                                      ".");

            // Round Robin needs an extra piece of info: the quantum (time slice) size.
            if (config.scheduler == SchedulerType::RR) {

                ConsoleManager::printLine("Quantum cycles: " +

                                          std::to_string(config.quantumCycles) + ".");

            }

            ConsoleManager::printLine(

                "Type \"scheduler-start\" to start process generation and execution.");

            continue;

        }


        // outputs-clear command:
        // Deletes all per-process output files from the outputs/ folder.
        // Available even before "initialize" — useful for manual cleanup.
        //
        // Output files are created by each process as it runs (one file per
        // process, named after the process). This command removes them all.
        if (command == "outputs-clear") {
            std::size_t removedCount = 0;
            std::string error;
            if (OutputManager::clearAllProcessOutputs(removedCount, error)) {
                ConsoleManager::printLine("Removed " + std::to_string(removedCount) +
                                          " process output file(s) from " +
                                          OutputManager::outputsDirectory() + "/.");
            } else {
                ConsoleManager::printLine(error);
            }
            continue;
        }

        // initialization gate:
        // Every command below this point requires the system to be initialized.
        // If "initialize" hasn't been run yet, print a reminder and skip.
        if (!initialized) {

            printNotInitialized();

            continue;

        }


        // clear command:
        // Wipes the terminal screen and reprints the ASCII art header.
        // Useful when the screen is cluttered with previous output.
        if (command == "clear") {

            ConsoleManager::clearScreen();

            ConsoleManager::printHeader();

            continue;

        }


        // scheduler-start command:
        // Starts the background CPU worker threads and begins generating
        // processes automatically at the rate set by batch-process-freq.
        //
        // What it does step-by-step:
        //   1. Check if the scheduler is already running (can't start twice).
        //   2. Launch all background threads (tick, scheduler, core workers).
        //   3. Enable automatic process generation (batch mode).
        //   4. If config says to pre-load processes, create them immediately.
        if (command == "scheduler-start") {
            if (scheduler.isBatchGenerationActive()) {
                // Batch generation is already on — truly already started.
                ConsoleManager::printLine("Scheduler is already running.");
                continue;
            }

            // Engine may already be running (auto-started by "screen -s").
            // Only call start() if the threads aren't up yet.
            if (!scheduler.isEngineRunning()) {
                scheduler.start(config);
            }

            // Turn on automatic process spawning (every batch-process-freq cycles).
            scheduler.enableBatchGeneration();

            // If "initial-process-count" was set in config.txt, create that many
            // processes right now so the CPUs are busy immediately.
            if (config.initialProcessCount > 0) {
                scheduler.generateInitialBatch(static_cast<int>(config.initialProcessCount));
            }

            ConsoleManager::printLine("Scheduler started. Generating processes.");
            continue;
        }


        // scheduler-stop command:
        // Stops automatic process generation but lets already-queued and
        // already-running processes finish naturally in the background.
        if (command == "scheduler-stop") {
            if (!scheduler.isEngineRunning()) {

                // Can't stop something that isn't running.
                ConsoleManager::printLine("Scheduler is not running.");
            } else {
                // Graceful stop: no new batch processes will spawn; existing ones finish.
                scheduler.stopGracefully();
                ConsoleManager::printLine(
                    "Scheduler stopped. Remaining processes will finish in the background.");
            }
            continue;
        }


        // process-smi command:
        // Prints the demand-paging summary: CPU utilization, physical-memory
        // usage/utilization, and the resident memory of each active process.
        if (command == "process-smi") {
            std::cout << ReportManager::generateProcessSmi(scheduler);
            continue;
        }

        // vmstat command:
        // Prints detailed memory + CPU-tick + paging statistics.
        if (command == "vmstat") {
            std::cout << ReportManager::generateVmstat(scheduler);
            continue;
        }

        // report-util command:
        // Takes a snapshot of all processes and CPU usage right now and
        // writes it to csopesy-log.txt (same format as "screen -ls").
        //
        // Useful for saving a record of what was running at a given moment.
        if (command == "report-util") {
            // generateSystemReport reads the current state of the scheduler
            // (running processes, finished processes, CPU utilization).
            const std::string report = ReportManager::generateSystemReport(scheduler);

            std::string error;
            // saveReport writes the text to disk at the default path (csopesy-log.txt).
            if (ReportManager::saveReport(ReportManager::defaultReportPath(), report, error)) {
                // Print something like "Report saved to csopesy-log.txt."
                ConsoleManager::printLine(ReportManager::reportConfirmationMessage());

            } else {

                ConsoleManager::printLine(error);

            }

            continue;

        }


        // screen (and its sub-commands):
        // All "screen" commands are handled by ScreenManager because they
        // involve process-specific interactions. The sub-commands are:
        //
        //   screen -ls            -> list all processes (like "ps" on Linux)
        //   screen -s <name>      -> create a new process called <name>
        //   screen -r <name>      -> attach to an existing process and inspect it
        //
        // isScreenCommand() returns true for any command beginning with "screen".
        // handleCommand() then figures out the specific sub-command and acts on it.
        if (ScreenManager::isScreenCommand(command)) {

            ScreenManager::handleCommand(command, scheduler, config);

            continue;

        }


        // Fallthrough: unknown command
        // If none of the checks above matched, the user typed something we
        // don't recognize. Print a friendly error and go back to the prompt.
        ConsoleManager::printLine("Unknown command. Please try again.");

    }   // end of main command loop


    // Ensure all background threads are stopped when the program exits.
    // (The "exit" command already called this, but it's safe to call twice.)
    scheduler.stop();

    return 0;

}

