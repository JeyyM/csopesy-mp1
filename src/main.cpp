#include "Config.h"
#include "ConsoleManager.h"
#include "MockProcessData.h"
#include "ProcessManager.h"
#include "ReportManager.h"
#include "Scheduler.h"

#include <iostream>
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

void runMockProcessScreen(const std::string& processName) {
    ConsoleManager::clearScreen();
    std::cout << MockProcessData::formatProcessSmi(processName);
    ConsoleManager::printProcessScreenHint(processName);

    while (true) {
        ConsoleManager::printPrompt();

        std::string command;
        if (!std::getline(std::cin, command)) {
            break;
        }
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        if (command == "process-smi") {
            std::cout << MockProcessData::formatProcessSmi(processName);
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

void runDynamicProcessScreen(ProcessManager& processManager) {
    Process* active = processManager.getActiveProcess();
    if (!active) {
        return;
    }

    const std::string processName = active->name;
    ConsoleManager::clearScreen();
    std::cout << active->formatProcessSmi();
    ConsoleManager::printProcessScreenHint(processName);

    while (true) {
        ConsoleManager::printPrompt();

        std::string command;
        if (!std::getline(std::cin, command)) {
            break;
        }
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        if (command == "process-smi") {
            processManager.advanceActiveProcess();
            if (Process* active = processManager.getActiveProcess()) {
                std::cout << active->formatProcessSmi();
            }
            continue;
        }

        if (command == "exit") {
            processManager.detachActiveProcess(true);
            ConsoleManager::clearScreen();
            ConsoleManager::printHeader();
            return;
        }

        ConsoleManager::printLine("Unknown command inside process screen.");
    }
}

bool handleScreenCommand(const std::string& command, ProcessManager& processManager) {
    if (command == "screen -ls") {
        ConsoleManager::printMockProcessReport();
        ConsoleManager::printLsAttachHint();
        return true;
    }

    const std::string createPrefix = "screen -s ";
    const std::string attachPrefix = "screen -r ";

    if (startsWith(command, createPrefix)) {
        const std::string processName = trim(command.substr(createPrefix.size()));
        if (processName.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }

        std::string error;
        if (!processManager.createProcess(processName, error)) {
            ConsoleManager::printLine(error);
            return true;
        }

        runDynamicProcessScreen(processManager);
        return true;
    }

    if (startsWith(command, attachPrefix)) {
        const std::string processName = trim(command.substr(attachPrefix.size()));
        if (processName.empty()) {
            ConsoleManager::printLine("Unknown command. Please try again.");
            return true;
        }

        if (MockProcessData::isAttachable(processName)) {
            runMockProcessScreen(processName);
            return true;
        }

        std::string error;
        if (processManager.attachProcess(processName, error)) {
            runDynamicProcessScreen(processManager);
            return true;
        }

        ConsoleManager::printLine(error);
        return true;
    }

    return false;
}

}  // namespace

int main() {
    Config config;
    ProcessManager processManager;
    Scheduler scheduler;

    bool initialized = false;
    bool running = true;

    ConsoleManager::clearScreen();
    ConsoleManager::printHeader();

    while (running) {
        scheduler.onCommandTick();

        ConsoleManager::printPrompt();

        std::string command;
        if (!std::getline(std::cin, command)) {
            break;
        }
        command = trim(command);
        if (command.empty()) {
            continue;
        }

        if (!initialized) {
            if (command == "exit") {
                running = false;
                continue;
            }
            if (command == "initialize") {
                std::string error;
                if (!ConfigLoader::loadFromFile("config.txt", config, error)) {
                    ConsoleManager::printLine(error);
                } else {
                    initialized = true;
                    processManager.setConfig(config);
                    MockProcessData::ensureInitialized();
                    ConsoleManager::printLine(
                        "System initialized successfully using config.txt.");
                }
                continue;
            }

            if (command == "screen -ls" || startsWith(command, "screen -s ") ||
                startsWith(command, "screen -r ") || command == "scheduler-start" ||
                command == "scheduler-stop" || command == "report-util") {
                printNotInitialized();
                continue;
            }

            printNotInitialized();
            continue;
        }

        if (command == "exit") {
            ConsoleManager::printLine("Exiting CSOPESY Emulator.");
            running = false;
            continue;
        }

        if (command == "process-smi") {
            ConsoleManager::printLine(
                "process-smi only works inside a process screen. "
                "Attach first with: screen -r <name>  (e.g. screen -r process05)");
            continue;
        }

        if (command == "initialize") {
            std::string error;
            if (!ConfigLoader::loadFromFile("config.txt", config, error)) {
                ConsoleManager::printLine(error);
            } else {
                processManager.setConfig(config);
                MockProcessData::ensureInitialized();
                ConsoleManager::printLine(
                    "System initialized successfully using config.txt.");
            }
            continue;
        }

        if (command == "scheduler-start") {
            ConsoleManager::printLine(scheduler.start());
            continue;
        }

        if (command == "scheduler-stop") {
            ConsoleManager::printLine(scheduler.stop());
            continue;
        }

        if (command == "report-util") {
            const std::string report = ReportManager::generateMockReport();
            std::string error;
            const std::string path = ReportManager::defaultReportPath();
            if (ReportManager::saveReport(path, report, error)) {
                ConsoleManager::printLine("Report generated at C:/csopesy-log.txt!");
            } else {
                ConsoleManager::printLine(error);
            }
            continue;
        }

        if (command == "screen -ls" || startsWith(command, "screen -s ") ||
            startsWith(command, "screen -r ")) {
            if (handleScreenCommand(command, processManager)) {
                continue;
            }
        }

        ConsoleManager::printLine("Unknown command. Please try again.");
    }

    return 0;
}
