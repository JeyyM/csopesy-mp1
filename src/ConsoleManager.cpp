#include "ConsoleManager.h"

#include "ReportManager.h"

#include <cstdlib>
#include <iostream>

void ConsoleManager::clearScreen() {
#if defined(_WIN32)
    std::system("cls");
#else
    std::system("clear");
#endif
    std::cout.flush();
}

void ConsoleManager::printHeader() {
    std::cout
        << "  _____ _____  ____  _____  ______  _____ __     __\n"
        << " / ____/ ____|/ __ \\|  __ \\|  ____|/ ____|\\ \\   / /\n"
        << "| |   | (___ | |  | | |__) | |__  | (___   \\ \\_/ /\n"
        << "| |    \\___ \\| |  | |  ___/|  __|  \\___ \\   \\   /\n"
        << "| |____ ____) | |__| | |    | |____ ____) |   | |\n"
        << " \\_____|_____/ \\____/|_|    |______|_____/    |_|\n"
        << "\n"
        << "----------------------------------------------------\n"
        << "Welcome to CSOPESY Emulator!\n"
        << "\n"
        << "Developers:\n"
        << "Del Gallego, Neil Patrick\n"
        << "\n"
        << "Last updated: 01-18-2024\n"
        << "----------------------------------------------------\n"
        << "\n";
}

void ConsoleManager::printPrompt() {
    std::cout << "root:\\> ";
    std::cout.flush();
}

void ConsoleManager::printProcessPrompt(const std::string& processName) {
    std::cout << "root:" << processName << ":\\> ";
    std::cout.flush();
}

void ConsoleManager::printProcessScreenHint(const std::string& processName) {
    std::cout << "\n[Process screen: " << processName
              << " | Commands: process-smi, exit (return to main menu)]\n\n";
}

void ConsoleManager::printLsAttachHint() {
    std::cout << "\nTip: Use screen -r <name> to attach to a running process "
                 "(e.g. screen -r process05).\n";
}

void ConsoleManager::printLine(const std::string& text) {
    std::cout << text << "\n";
}

void ConsoleManager::printMockProcessReport() {
    std::cout << ReportManager::generateMockReport();
}
