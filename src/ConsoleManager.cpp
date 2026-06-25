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
        << "Alcantara, Van Asher\n"
        << "Capote, Mary Grace\n"
        << "Miranda, Juan Miguel\n"
        << "Rojo, Von Matthew\n"
        << "\n"
        << "Last updated: 06-24-2026 by grass\n"
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
