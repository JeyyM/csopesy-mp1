// ReportManager.cpp
//
// Builds and saves the system report shown by screen -ls and report-util.
//
// The report contains:
//   - CPU utilization percentage (running cores / total cores)
//   - Number of cores in use and available
//   - A list of currently running processes with core, timestamp, and progress
//   - A list of finished processes with finish timestamp and final line count
//
// Data comes from a scheduler snapshot so this can run safely while
// background threads are still executing processes.

#include "ReportManager.h"

#include "ProcessModel.h"
#include "Scheduler.h"

#include <fstream>
#include <sstream>

namespace {

// Pads a string with spaces on the right so columns line up in the output.
// If the text is already wider than width, it is returned unchanged.
std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

}  // namespace

// Builds the full report string.
// Steps:
//   1. Take a snapshot of all processes from the scheduler (thread-safe copy).
//   2. Count how many processes are currently Running to get coresUsed.
//   3. Compute coresAvailable and utilization percentage.
//   4. Build the text block: header stats, running list, finished list.
std::string ReportManager::generateSystemReport(Scheduler& scheduler) {
    const SchedulerStatusSnapshot snapshot = scheduler.statusSnapshot();

    const std::vector<std::shared_ptr<Process>>& processes = snapshot.processes;

    // Count processes with Running status to determine how many cores are busy.
    int coresUsed = 0;
    for (const auto& process : snapshot.processes) {
        if (process->status() == ProcessStatus::Running) {
            ++coresUsed;
        }
    }
    // Cap at numCpu in case of any transient over-count during state transitions.
    if (coresUsed > snapshot.numCpu) {
        coresUsed = snapshot.numCpu;
    }

    const int coresAvailable = snapshot.numCpu - coresUsed;
    const int utilization =
        snapshot.numCpu > 0 ? (coresUsed * 100) / snapshot.numCpu : 0;

    // Build the report text line by line.
    std::ostringstream report;
    report << "CPU utilization: " << utilization << "%\n";
    report << "Cores used: " << coresUsed << "\n";
    report << "Cores available: " << coresAvailable << "\n";
    report << "\n";
    report << "---------------------------------------------\n";

    // Running section: name, creation time, assigned core, current/total lines.
    report << "Running processes:\n";
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            report << padRight(process->name(), 12) << " (" << process->creationTimestamp()
                   << ")    Core: " << process->assignedCore() << "    "
                   << process->currentLine() << " / " << process->totalLines() << "\n";
        }
    }

    // Finished section: name, finish time, final line count.
    report << "\nFinished processes:\n";
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Finished) {
            report << padRight(process->name(), 12) << " (" << process->finishTimestamp()
                   << ")    Finished    " << process->totalLines() << " / "
                   << process->totalLines() << "\n";
        }
    }
    report << "---------------------------------------------\n";
    return report.str();
}

// Writes the report to a file, overwriting any existing content.
// Returns true on success. On failure, sets errorMessage and returns false.
bool ReportManager::saveReport(const std::string& path, const std::string& content,
                               std::string& errorMessage) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        errorMessage = "Could not write report to: " + path;
        return false;
    }
    file << content;
    errorMessage.clear();
    return true;
}

// Default output path for report-util.
std::string ReportManager::defaultReportPath() {
    return "csopesy-log.txt";
}

// Message printed to the terminal after report-util successfully saves.
std::string ReportManager::reportConfirmationMessage() {
    return "Report generated at C:/csopesy-log.txt!";
}
