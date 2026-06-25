#include "ScreenManager.h"

#include "ConsoleManager.h"
#include "ProcessModel.h"
#include "ReportManager.h"

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

int randomInstructionCount(const Config& config) {
    static std::mt19937 rng(std::random_device{}());
    const int low = static_cast<int>(config.minIns);
    const int high = static_cast<int>(config.maxIns < config.minIns ? config.minIns
                                                                     : config.maxIns);
    std::uniform_int_distribution<int> dist(low, high);
    return dist(rng);
}

int printsPerProcessFromConfig(const Config& config) {
    if (config.minIns == config.maxIns) {
        return static_cast<int>(config.minIns);
    }
    return randomInstructionCount(config);
}

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

}  // namespace

bool ScreenManager::isScreenCommand(const std::string& command) {
    return command == "screen" || command == "screen -ls" ||
           startsWith(command, "screen -s ") || startsWith(command, "screen -r ");
}

bool ScreenManager::handleCommand(const std::string& command, Scheduler& scheduler,
                                  const Config& config) {
    if (command == "screen") {
        ConsoleManager::printLine(
            "screen command requires arguments: screen -ls, screen -s <process name>, "
            "or screen -r <process name>.");
        return true;
    }

    if (command == "screen -ls") {
        std::cout << ReportManager::generateSystemReport(scheduler);
        ConsoleManager::printLsAttachHint();
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
        scheduler.ensureEngineRunning(config);
        const int instructionCount = printsPerProcessFromConfig(config);
        auto process = scheduler.createUserProcess(name, instructionCount);
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
