#pragma once

#include "Config.h"
#include "Scheduler.h"

#include <string>

class ScreenManager {
public:
    static bool isScreenCommand(const std::string& command);
    static bool handleCommand(const std::string& command, Scheduler& scheduler,
                              const Config& config);
};
