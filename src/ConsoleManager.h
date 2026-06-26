#pragma once

// Every message the user sees — the startup banner, the prompt they type
// into, command responses, hints, and error text — goes through this class.
// It also handles clearing the screen.

//  - printPrompt()  is how the interpreter listens (signals "your turn")
//  - printLine()    is how the interpreter speaks (one response line)
//  - clearScreen()  and printHeader() are used to reset the visual state

// who calls what:
//
//   main.cpp
//     clearScreen()     — on startup, and when the user types "clear"
//     printHeader()     — on startup, "clear", and returning from a process screen
//     printPrompt()     — every iteration of the main command loop
//     printLine()       — for every command response or error message
//
//   ScreenManager.cpp
//     clearScreen()     — when entering or exiting a process screen
//     printHeader()     — when returning from a process screen to the main menu
//     printPrompt()     — every iteration of the inner process-screen loop
//     printLine()       — for error messages inside screen sub-commands
//     printProcessScreenHint() — shown once when a process screen opens
//     printLsAttachHint()      — shown after "screen -ls" output

//   All methods are static — ConsoleManager has no state of its own.
//   It is a pure collection of output helpers, not an object with a lifetime.
//   This keeps the call sites simple: ConsoleManager::printLine("...").

#include <string>

// just a class that handles printing to the console
class ConsoleManager {
public:

    // Wipes the terminal window.
    // Uses "cls" on Windows and "clear" on Linux/macOS.
    // Called on startup, on "clear" command, and when entering/exiting process screens.
    static void clearScreen();

    // Prints the ASCII art "CSOPESY" banner, the welcome message, and developer names.
    // Called on startup and whenever the screen is cleared to restore the main UI.
    static void printHeader();

    // Prints the input prompt "root:\> " and flushes stdout so it appears immediately
    // before std::getline blocks waiting for the user to type.
    // Called at the top of every command loop iteration (both main and process screen).
    static void printPrompt();

    // Prints a single line of text followed by a newline.
    // This is the standard way every command response and error message is displayed.
    // Example output: "System initialized successfully using config.txt."
    static void printLine(const std::string& text);

    // Prints the hint bar shown at the bottom when a process screen opens.
    // Tells the user which commands are available while inside that screen.
    // Example output: "[Process screen: proc-01 | Commands: process-smi, exit (return to main menu)]"
    static void printProcessScreenHint(const std::string& processName);

    // Prints the tip shown after "screen -ls" output.
    // Reminds the user how to attach to one of the listed processes.
    // Output: "Tip: use screen -r <process name> to attach to a running process."
    static void printLsAttachHint();
};
