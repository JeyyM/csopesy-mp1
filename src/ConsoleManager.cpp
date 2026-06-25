#include "ConsoleManager.h"

#include <cstdlib>
#include <iostream>

// everything here is just draw and text on screen
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
        << "\n"
        << "\n"
        << "----------------------------------------------------\n"
        << "Welcome to CSOPESY Emulator!\n"
        << "\n"
        << "Developers:\n"
        << "    Miranda, Juan Miguel\n"
        << "    Alcantara, Van Asher\n"
        << "    Capote, Mary Grace\n"
        << "    Rojo, Von Matthew\n"
        << "\n"
        << "Last updated: 06-20-2026\n"
        << "----------------------------------------------------\n"
        << "\n";
}

void ConsoleManager::printPrompt() {
    std::cout << "root:\\> ";
    std::cout.flush();
}

void ConsoleManager::printLine(const std::string& text) {
    std::cout << text << "\n";
}

void ConsoleManager::printProcessScreenHint(const std::string& processName) {
    std::cout << "\n[Process screen: " << processName
              << " | Commands: process-smi, exit (return to main menu)]\n\n";
}
