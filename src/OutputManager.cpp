// OutputManager.cpp
//
// Implements file I/O for per-process logs stored under outputs/.
//
// Typical lifecycle of one process log:
//   1. User runs screen -s proc-01 OR scheduler spawns process01
//   2. Scheduler::createProcessLocked -> initializeProcessLog("proc-01")
//        creates outputs/proc-01.txt with a header, no log lines yet
//   3. Scheduler::runProcessSlice runs PRINT instructions and appends lines
//        to the same file (open mode append — not handled in this file)
//   4. User runs outputs-clear or exit -> clearAllProcessOutputs deletes all files

#include "OutputManager.h"

#include <filesystem>  // create_directories, directory_iterator, remove
#include <fstream>     // ofstream for writing the initial log header

namespace {

// Single source of truth for the output folder name.
constexpr const char* kOutputsDir = "outputs";

}  // namespace

const char* OutputManager::outputsDirectory() {
    return kOutputsDir;
}

std::string OutputManager::processLogPath(const std::string& processName) {
    // Forward slashes work on Windows and Linux for std::filesystem / ofstream.
    return std::string(kOutputsDir) + "/" + processName + ".txt";
}

void OutputManager::ensureOutputsDirectory() {
    // create_directories creates outputs/ and any missing parents.
    // Does not throw if the folder already exists.
    std::filesystem::create_directories(kOutputsDir);
}

void OutputManager::initializeProcessLog(const std::string& processName) {
    ensureOutputsDirectory();

    // trunc = start a new file (or wipe an existing one with the same name).
    std::ofstream file(processLogPath(processName), std::ios::trunc);
    if (file.is_open()) {
        // Header layout mirrors what process-smi shows in memory so the
        // on-disk file and the CLI view stay consistent.
        file << "Process name: " << processName << "\n";
        file << "Logs:\n\n";
    }
    // If open fails (permissions, etc.), we silently skip — Scheduler will
    // still run the process; log lines just won't be written to disk.
}

bool OutputManager::clearAllProcessOutputs(std::size_t& removedCount,
                                           std::string& errorMessage) {
    removedCount = 0;
    errorMessage.clear();

    const std::filesystem::path dir(kOutputsDir);

    // Nothing to delete if the user never ran the scheduler or already cleared.
    if (!std::filesystem::exists(dir)) {
        return true;
    }

    try {
        // Walk every entry in outputs/ and delete regular files only.
        // Subdirectories (if any were added manually) are left untouched.
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
                ++removedCount;
            }
        }
        return true;
    } catch (const std::filesystem::filesystem_error& ex) {
        // e.g. file locked by another program, permission denied
        errorMessage = std::string("Could not clear outputs: ") + ex.what();
        return false;
    }
}
