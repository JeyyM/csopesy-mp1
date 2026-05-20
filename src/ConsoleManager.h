#pragma once

#include <string>

class ConsoleManager {
public:
    static void clearScreen();
    static void printHeader();
    static void printPrompt();
    static void printProcessPrompt(const std::string& processName);
    static void printLine(const std::string& text);
    static void printMockProcessReport();
    static void printProcessScreenHint(const std::string& processName);
    static void printLsAttachHint();
};
