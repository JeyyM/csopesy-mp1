#pragma once

#include <string>
#include <vector>

struct MockLogEntry {
    std::string timestamp;
    int core = 0;
    std::string message;
};

struct MockProcessEntry {
    std::string name;
    int id = 0;
    std::string timestamp;
    int core = 0;
    int currentInstruction = 0;
    int totalInstructions = 0;
    bool finished = false;
    std::vector<MockLogEntry> logs;
};

class MockProcessData {
public:
    static void ensureInitialized();
    static bool addSchedulerMockProcesses();
    static bool hasSchedulerMockProcesses();

    static const MockProcessEntry* findByName(const std::string& name);
    static const MockProcessEntry* findRunningProcess(const std::string& name);
    static bool isAttachable(const std::string& name);
    static std::string formatProcessSmi(const std::string& name);
    static std::string generateLsReport();
};
