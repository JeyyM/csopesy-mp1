#include "ReportManager.h"

#include "MockProcessData.h"

#include <fstream>

std::string ReportManager::generateMockReport() {
    return MockProcessData::generateLsReport();
}

bool ReportManager::saveReport(const std::string& path, const std::string& content,
                               std::string& errorMessage) {
    std::ofstream file(path);
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
