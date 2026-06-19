#pragma once

#include <string>

class ReportManager {
public:
    static bool saveReport(const std::string& path, const std::string& content,
                           std::string& errorMessage);
    static std::string defaultReportPath();
};
