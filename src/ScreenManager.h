#pragma once

// ScreenManager handles all commands that start with "screen".
// It checks whether a command string is a screen command, then routes
// it to the correct action: screen -ls, screen -s, or screen -r.
//
// All methods are static. The inner process screen loop (process-smi,
// exit) is defined in the .cpp file as a local helper.

#include "Config.h"
#include "Scheduler.h"

#include <string>

class ScreenManager {
public:
    // Returns true if the command string is any recognized screen variant.
    // Checked before handleCommand is called so main.cpp can route correctly.
    static bool isScreenCommand(const std::string& command);

    // Dispatches the screen command to the right handler.
    // Returns true if the command was handled (even if it printed an error).
    static bool handleCommand(const std::string& command, Scheduler& scheduler,
                              const Config& config);
};
