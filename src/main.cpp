#include "Config.h"
#include "ConsoleManager.h"
#include "ReportManager.h"
#include "ScreenManager.h"
#include "Scheduler.h"

#include <iostream>
#include <string>

namespace {

constexpr int kTestProcessCount = 10;
constexpr int kTestPrintsPerProcess = 100;

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

void printNotInitialized() {
    ConsoleManager::printLine(
        "Please initialize the system first by typing \"initialize\".");
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

        if (command == "clear") {
            ConsoleManager::clearScreen();
            ConsoleManager::printHeader();
            continue;
        }

        if (command == "scheduler-start") {
            if (scheduler.isEngineRunning()) {
                ConsoleManager::printLine("Scheduler is already running.");
            } else {
                scheduler.start(config);
                scheduler.generateBatch(kTestProcessCount, kTestPrintsPerProcess);
                ConsoleManager::printLine("Scheduler started. Generating processes.");
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

        if (ScreenManager::isScreenCommand(command)) {
            ScreenManager::handleCommand(command, scheduler, config);
            continue;
        }

        ConsoleManager::printLine("Unknown command. Please try again.");
    }

    scheduler.stop();
    return 0;
}
