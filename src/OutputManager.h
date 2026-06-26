#pragma once

// OutputManager.h
//
// Central place for all filesystem paths and file operations related to
// process output logs. Every running process gets one text file under
// outputs/<process-name>.txt.
//
// This class is stateless (all static methods) — it does not own any data.
// It only knows how to build paths, create folders, seed log headers, and
// delete old log files.
//
// Who calls OutputManager:
//   - Scheduler.cpp  : initializeProcessLog on create, append during runProcessSlice
//   - main.cpp       : clearAllProcessOutputs on "outputs-clear" and "exit"

#include <cstddef>  // std::size_t for removedCount in clearAllProcessOutputs
#include <string>   // process names and file paths

class OutputManager {
public:
    // Returns the name of the output folder ("outputs").
    // Used in CLI messages so the user sees where files were written or cleared.
    static const char* outputsDirectory();

    // Builds the full relative path for one process log file.
    // Example: processName "proc-01" -> "outputs/proc-01.txt"
    // Does not create the file — only returns the path string.
    static std::string processLogPath(const std::string& processName);

    // Makes sure the outputs/ directory exists on disk.
    // Safe to call multiple times; create_directories is a no-op if already present.
    static void ensureOutputsDirectory();

    // Creates a fresh log file for a newly registered process.
    // Opens outputs/<name>.txt in truncate mode (overwrites if file already exists).
    // Writes the standard header only — execution logs are appended later by Scheduler.
    // Called from Scheduler::createProcessLocked for both screen -s and batch spawns.
    static void initializeProcessLog(const std::string& processName);

    // Removes every regular file inside outputs/ (not subfolders).
    // On success: returns true, sets removedCount to number of files deleted.
    // On failure: returns false, sets errorMessage with the filesystem error text.
    // If outputs/ does not exist, returns true with removedCount = 0.
    static bool clearAllProcessOutputs(std::size_t& removedCount, std::string& errorMessage);
};
