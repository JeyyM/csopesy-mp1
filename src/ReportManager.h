#pragma once

#include <string>

class Scheduler;

class ReportManager {
public:
    static std::string generateSystemReport(Scheduler& scheduler);
    static bool saveReport(const std::string& path, const std::string& content,
                           std::string& errorMessage);
    static std::string defaultReportPath();
    static std::string reportConfirmationMessage();
};
