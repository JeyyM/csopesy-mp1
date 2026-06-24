#include "Config.h"
#include "ConsoleManager.h"
#include "ProcessModel.h"
#include "ReportManager.h"
#include "Scheduler.h"

#include <iostream>
#include <memory>
#include <random>
#include <string>

namespace {

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

void printNotInitialized() {
    ConsoleManager::printLine(
        "Please initialize the system first by typing \"initialize\".");
}

int randomInstructionCount(const Config& config) {
    static std::mt19937 rng(std::random_device{}());
    const int low = static_cast<int>(config.minIns);
    const int high = static_cast<int>(config.maxIns < config.minIns ? config.minIns
                                                                     : config.maxIns);
    std::uniform_int_distribution<int> dist(low, high);
    return dist(rng);
}

// Runs the process screen loop for a live process until the user types exit.
void runProcessScreen(const std::shared_ptr<Process>& process) {
    ConsoleManager::clearScreen();
    std::cout << process->formatSmi();
    ConsoleManager::printProcessScreenHint(process->name());

    while (true) {
        ConsoleManager::printPrompt();

        std::string command;
        if (!std::getline(std::cin, command)) {
            return;
        }
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        if (command == "process-smi") {
            std::cout << process->formatSmi();
            continue;
        }

        if (command == "exit") {
            ConsoleManager::clearScreen();
            ConsoleManager::printHeader();
            return;
        }

        ConsoleManager::printLine("Unknown command inside process screen.");
    }
}

bool handleScreenCommand(const std::string& command, Scheduler& scheduler,
                         const Config& config) {
    if (command == "screen -ls") {
        std::cout << scheduler.buildStatusReport();
        return true;
    }

    const std::string createPrefix = "screen -s ";
    const std::string attachPrefix = "screen -r ";

    if (startsWith(command, createPrefix)) {
        const std::string name = trim(command.substr(createPrefix.size()));
        if (name.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }
        if (scheduler.processExists(name)) {
            ConsoleManager::printLine("Process " + name + " already exists.");
            return true;
        }
        auto process = scheduler.createProcess(name, randomInstructionCount(config));
        runProcessScreen(process);
        return true;
    }

    if (startsWith(command, attachPrefix)) {
        const std::string name = trim(command.substr(attachPrefix.size()));
        if (name.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }
        auto process = scheduler.findProcess(name);
        if (!process || process->status() == ProcessStatus::Finished) {
            ConsoleManager::printLine("Process " + name + " not found.");
            return true;
        }
        runProcessScreen(process);
        return true;
    }

    return false;
}

}  // namespace

int main() {
    Config config;
    Scheduler scheduler;

    bool initialized = false;
    bool running = true;

    ConsoleManager::clearScreen();
    ConsoleManager::printHeader();

    while (running) {
        ConsoleManager::printPrompt();

        std::string command;
        if (!std::getline(std::cin, command)) {
            break;
        }
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        if (command == "exit") {
            ConsoleManager::printLine("Exiting CSOPESY Emulator.");
            scheduler.stop();
            running = false;
            continue;
        }

        if (command == "clear") {
            ConsoleManager::clearScreen();
            ConsoleManager::printHeader();
            continue;
        }

        if (command == "initialize") {
            if (initialized) {
                ConsoleManager::printLine("System is already initialized.");
                continue;
            }
            std::string error;
            if (!ConfigLoader::loadFromFile("config.txt", config, error)) {
                ConsoleManager::printLine(error);
                continue;
            }
            initialized = true;
            ConsoleManager::printLine("System initialized successfully using config.txt.");
            continue;
        }

        if (!initialized) {
            printNotInitialized();
            continue;
        }

        if (command == "scheduler-start") {
            if (scheduler.isEngineRunning()) {
                ConsoleManager::printLine("Scheduler is already running.");
            } else {
                scheduler.start(config);
                ConsoleManager::printLine("Scheduler started. Generating dummy processes.");
            }
            continue;
        }

        if (command == "scheduler-stop") {
            if (!scheduler.isEngineRunning()) {
                ConsoleManager::printLine("Scheduler is not running.");
            } else {
                scheduler.stop();
                ConsoleManager::printLine("Scheduler stopped.");
            }
            continue;
        }

        if (command == "report-util") {
            const std::string report = scheduler.buildStatusReport();
            std::string error;
            if (ReportManager::saveReport(ReportManager::defaultReportPath(), report,
                                          error)) {
                ConsoleManager::printLine("Report generated at C:/csopesy-log.txt!");
            } else {
                ConsoleManager::printLine(error);
            }
            continue;
        }

        if (command == "screen -ls" || startsWith(command, "screen -s ") ||
            startsWith(command, "screen -r ")) {
            if (handleScreenCommand(command, scheduler, config)) {
                continue;
            }
        }

        ConsoleManager::printLine("Unknown command. Please try again.");
    }

    scheduler.stop();
    return 0;
}
