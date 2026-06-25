#pragma once

#include <cstddef>
#include <string>

class OutputManager {
public:
    static const char* outputsDirectory();
    static std::string processLogPath(const std::string& processName);
    static void ensureOutputsDirectory();
    static bool clearAllProcessOutputs(std::size_t& removedCount, std::string& errorMessage);
};
