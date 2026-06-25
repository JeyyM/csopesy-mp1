#include "ReportManager.h"

#include "ProcessModel.h"
#include "Scheduler.h"

#include <fstream>
#include <sstream>

namespace {

std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

}  // namespace

std::string ReportManager::generateSystemReport(Scheduler& scheduler) {
    const SchedulerStatusSnapshot snapshot = scheduler.statusSnapshot();

    const std::vector<std::shared_ptr<Process>>& processes = snapshot.processes;

    int coresUsed = 0;
    for (const auto& process : snapshot.processes) {
        if (process->status() == ProcessStatus::Running) {
            ++coresUsed;
        }
    }
    if (coresUsed > snapshot.numCpu) {
        coresUsed = snapshot.numCpu;
    }

    const int coresAvailable = snapshot.numCpu - coresUsed;
    const int utilization =
        snapshot.numCpu > 0 ? (coresUsed * 100) / snapshot.numCpu : 0;

    std::ostringstream report;
    report << "CPU utilization: " << utilization << "%\n";
    report << "Cores used: " << coresUsed << "\n";
    report << "Cores available: " << coresAvailable << "\n";
    report << "\n";
    report << "---------------------------------------------\n";
    report << "Running processes:\n";
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            report << padRight(process->name(), 12) << " (" << process->creationTimestamp()
                   << ")    Core: " << process->assignedCore() << "    "
                   << process->currentLine() << " / " << process->totalLines() << "\n";
        }
    }

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

std::string ReportManager::defaultReportPath() {
    return "csopesy-log.txt";
}

std::string ReportManager::reportConfirmationMessage() {
    return "Report generated at C:/csopesy-log.txt!";
}
