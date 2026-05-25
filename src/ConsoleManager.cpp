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
        << "  /$$$$$$   /$$$$$$   /$$$$$$  /$$$$$$$  /$$$$$$$$  /$$$$$$  /$$     /$$\n"
        << " /$$__  $$ /$$__  $$ /$$__  $$| $$__  $$| $$_____/ /$$__  $$|  $$   /$$/\n"
        << "| $$  \\__/| $$  \\__/| $$  \\ $$| $$  \\ $$| $$      | $$  \\__/ \\  $$ /$$/ \n"
        << "| $$      |  $$$$$$ | $$  | $$| $$$$$$$/| $$$$$   |  $$$$$$   \\  $$$$/  \n"
        << "| $$       \\____  $$| $$  | $$| $$____/ | $$__/    \\____  $$   \\  $$/   \n"
        << "| $$    $$ /$$  \\ $$| $$  | $$| $$      | $$       /$$  \\ $$    | $$    \n"
        << "|  $$$$$$/|  $$$$$$/|  $$$$$$/| $$      | $$$$$$$$|  $$$$$$/    | $$    \n"
        << " \\______/  \\______/  \\______/ |__/      |________/ \\______/     |__/    \n"
        << "\n"
        << "----------------------------------------------------\n"
        << "Welcome to CSOPESY Emulator!\n"
        << "\n"
        << "Developers:\n"
        << "Miranda, Juan Miguel\n"
        << "Rojo, Von Matthew\n"
        << "Alcantara, Van Asher\n"
        << "Capote, Mary Grace\n"        
        << "\n"
        << "Last updated: 05-24-2026\n"
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
