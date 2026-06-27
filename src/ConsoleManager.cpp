//   Contains every function that writes to the terminal or clears it.
//   Nothing in here makes decisions — it only handles presentation.
//   The command interpreter (main.cpp, ScreenManager.cpp) decides *what*
//   to show; ConsoleManager decides *how* to show it.

#include "ConsoleManager.h"

#include <cstdlib>
#include <iostream>

// Clears the visible terminal window.
//
// Uses the OS shell command appropriate for the platform:
//   Windows -> "cls"
//   Linux / macOS -> "clear"
//
// The flush after system() ensures any buffered output that was printed
// just before the clear actually appears before the screen is wiped.
//
// Called by: main.cpp on startup and "clear" command;
//            ScreenManager.cpp when entering or exiting a process screen.
void ConsoleManager::clearScreen() {
#if defined(_WIN32)
    std::system("cls");
#else
    std::system("clear");
#endif
    std::cout.flush();
}

// Prints the full startup banner: ASCII art logo, welcome text, developer
// names, and last-updated date.
//
// The ASCII art spells "CSOPESY" using dollar-sign block characters.
// Each row is one string literal; the chain of << operators prints them
// in sequence with no extra logic needed.
//
// Called by: main.cpp on startup; main.cpp and ScreenManager.cpp whenever
//            the screen is cleared to restore the main menu appearance.
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
        << "Last updated: 06-27-2026\n"
        << "----------------------------------------------------\n"
        << "\n";
}

// Prints the command prompt and flushes stdout immediately.
//
// Output: "root:\> "  (no newline — the user types on the same line)
//
// The explicit flush is critical: without it, "root:\> " might stay stuck
// in the output buffer and never appear on screen before std::getline
// blocks waiting for the user's input. flush() forces it out immediately.
//
// Called at the top of every loop iteration in both:
//   - main.cpp (the main command loop)
//   - ScreenManager.cpp (the inner process-screen loop)
void ConsoleManager::printPrompt() {
    std::cout << "root:\\> ";
    std::cout.flush();
}

// Prints one line of text to the terminal followed by a newline character.
//
// This is the single standard output function for all command responses,
// confirmation messages, and error messages throughout the interpreter.
//
// Example calls and their output:
//   printLine("System initialized successfully using config.txt.")
//     -> "System initialized successfully using config.txt.\n"
//
//   printLine("Unknown command. Please try again.")
//     -> "Unknown command. Please try again.\n"
//
//   printLine("Process proc-01 already exists.")
//     -> "Process proc-01 already exists.\n"
void ConsoleManager::printLine(const std::string& text) {
    std::cout << text << "\n";
}

// Prints the hint bar displayed once when a process screen opens.
//
// This tells the user which commands are available while they are "inside"
// a process screen (as opposed to the main command loop).
//
// Example output for processName = "proc-01":
//   "\n[Process screen: proc-01 | Commands: process-smi, exit (return to main menu)]\n\n"
//
// The surrounding blank lines visually separate it from the process info
// panel above and the first prompt below.
//
// Called by: ScreenManager.cpp's runProcessScreen() immediately after
//            printing the initial process-smi panel.
void ConsoleManager::printProcessScreenHint(const std::string& processName) {
    std::cout << "\n[Process screen: " << processName
              << " | Commands: process-smi, exit (return to main menu)]\n\n";
}

// Prints the usage tip shown after "screen -ls" output.
//
// Output: "Tip: use screen -r <process name> to attach to a running process.\n"
//
// Placed after the process table so the user knows the natural next step
// after seeing a list of processes is to attach to one with "screen -r".
//
// Called by: ScreenManager.cpp's handleCommand() after printing the report.
void ConsoleManager::printLsAttachHint() {
    std::cout << "Tip: use screen -r <process name> to attach to a running process.\n";
}
