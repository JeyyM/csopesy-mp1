#include "OutputManager.h"

#include <filesystem>

namespace {

constexpr const char* kOutputsDir = "outputs";

}  // namespace

const char* OutputManager::outputsDirectory() {
    return kOutputsDir;
}

std::string OutputManager::processLogPath(const std::string& processName) {
    return std::string(kOutputsDir) + "/" + processName + ".txt";
}

void OutputManager::ensureOutputsDirectory() {
    std::filesystem::create_directories(kOutputsDir);
}

bool OutputManager::clearAllProcessOutputs(std::size_t& removedCount,
                                           std::string& errorMessage) {
    removedCount = 0;
    errorMessage.clear();

    const std::filesystem::path dir(kOutputsDir);
    if (!std::filesystem::exists(dir)) {
        return true;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                ++removedCount;
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& ex) {
        errorMessage = std::string("Could not clear outputs: ") + ex.what();
        return false;
    }
}
