#pragma once

// ReportManager builds the text output shown by screen -ls and report-util.
// All methods are static so no instance is needed.
// The report is built from a snapshot of the scheduler, so it is safe
// to call while the scheduler is running in the background.

#include <string>

class Scheduler;

class ReportManager {
public:
    // Builds the full CPU utilization + process list report as a string.
    // Reads a thread-safe snapshot from the scheduler so running threads
    // do not interfere with the output.
    static std::string generateSystemReport(Scheduler& scheduler);

    // Builds the process-smi view: CPU/memory utilization summary plus the
    // per-process resident-memory listing (MCO2).
    static std::string generateProcessSmi(Scheduler& scheduler);

    // Builds the vmstat view: detailed memory + CPU-tick + paging statistics.
    static std::string generateVmstat(Scheduler& scheduler);

    // Writes the report string to a file at the given path.
    // Overwrites any existing file. Returns false and sets errorMessage on failure.
    static bool saveReport(const std::string& path, const std::string& content,
                           std::string& errorMessage);

    // Returns the default output file path: "csopesy-log.txt".
    static std::string defaultReportPath();

    // Returns the message printed to the terminal after report-util succeeds.
    static std::string reportConfirmationMessage();
};
