#include "Config.h"
#include "ConsoleManager.h"

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

}  // namespace

int main() {
    Config config;

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
            running = false;
            continue;
        }

        if (command == "clear") {
            ConsoleManager::clearScreen();
            ConsoleManager::printHeader();
            continue;
        }

        if (command == "initialize") {
            std::string error;
            if (!ConfigLoader::loadFromFile("config.txt", config, error)) {
                ConsoleManager::printLine(error);
            } else {
                initialized = true;
                ConsoleManager::printLine(
                    "System initialized successfully using config.txt.");
            }
            continue;
        }

        if (command == "scheduler-start" || command == "scheduler-stop" ||
            command == "report-util" || command == "screen -ls" ||
            startsWith(command, "screen -s ") || startsWith(command, "screen -r ")) {
            if (!initialized) {
                printNotInitialized();
                continue;
            }
            ConsoleManager::printLine(command + " command recognized. Doing something.");
            continue;
        }

        ConsoleManager::printLine("Unknown command. Please try again.");
    }

    return 0;
}
