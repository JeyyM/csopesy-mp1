#pragma once

#include <string>

class ConsoleManager {
public:
    static void clearScreen();
    static void printHeader();
    static void printPrompt();
    static void printLine(const std::string& text);
    static void printProcessScreenHint(const std::string& processName);
};
