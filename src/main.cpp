/*
cd "c:\Users\asus\Desktop\CSOPESY OS MP"
cmake --build build
.\build\csopesy_os_mp.exe
*/

#include "Config.h"

#include "ConsoleManager.h"

#include "OutputManager.h"
#include "ReportManager.h"

#include "ScreenManager.h"

#include "Scheduler.h"



#include <iostream>

#include <string>



namespace {



std::string trim(const std::string& value) {

    const auto start = value.find_first_not_of(" \t\r\n");

    if (start == std::string::npos) {

        return "";

    }

    const auto end = value.find_last_not_of(" \t\r\n");

    return value.substr(start, end - start + 1);

}



void printNotInitialized() {

    ConsoleManager::printLine(

        "Please initialize the system first by typing \"initialize\".");

}



std::string schedulerLabel(const Config& config) {

    return config.scheduler == SchedulerType::RR ? "Round Robin" : "FCFS";

}



}  // namespace



int main() {

    Config config;

    Scheduler scheduler;



    bool initialized = false;

    bool running = true;



    ConsoleManager::clearScreen();

    ConsoleManager::printHeader();



    while (running) {

        ConsoleManager::printPrompt();



        std::string command;

        if (!std::getline(std::cin, command)) {

            break;

        }

        command = trim(command);

        if (command.empty()) {

            continue;

        }



        if (command == "exit") {
            scheduler.stop();

            std::size_t removedCount = 0;
            std::string clearError;
            OutputManager::clearAllProcessOutputs(removedCount, clearError);

            ConsoleManager::printLine("Exiting CSOPESY Emulator.");
            running = false;
            continue;
        }



        if (command == "initialize") {

            if (initialized) {

                ConsoleManager::printLine("System is already initialized.");

                continue;

            }

            std::string error;

            if (!ConfigLoader::loadFromFile("config.txt", config, error)) {

                ConsoleManager::printLine(error);

                continue;

            }



            initialized = true;

            ConsoleManager::printLine("System initialized successfully using config.txt.");

            ConsoleManager::printLine("Declared " + std::to_string(config.numCpu) +

                                      " CPU cores. Scheduler: " + schedulerLabel(config) +

                                      ".");

            if (config.scheduler == SchedulerType::RR) {

                ConsoleManager::printLine("Quantum cycles: " +

                                          std::to_string(config.quantumCycles) + ".");

            }

            ConsoleManager::printLine(

                "Type \"scheduler-start\" to start process generation and execution.");

            continue;

        }



        if (command == "outputs-clear") {
            std::size_t removedCount = 0;
            std::string error;
            if (OutputManager::clearAllProcessOutputs(removedCount, error)) {
                ConsoleManager::printLine("Removed " + std::to_string(removedCount) +
                                          " process output file(s) from " +
                                          OutputManager::outputsDirectory() + "/.");
            } else {
                ConsoleManager::printLine(error);
            }
            continue;
        }



        if (!initialized) {

            printNotInitialized();

            continue;

        }



        if (command == "clear") {

            ConsoleManager::clearScreen();

            ConsoleManager::printHeader();

            continue;

        }



        if (command == "scheduler-start") {
            if (scheduler.isEngineRunning()) {
                ConsoleManager::printLine("Scheduler is already running.");
                continue;
            }

            scheduler.start(config);
            scheduler.enableBatchGeneration();

            if (config.initialProcessCount > 0) {
                scheduler.generateInitialBatch(static_cast<int>(config.initialProcessCount));
            }

            ConsoleManager::printLine("Scheduler started. Generating processes.");
            continue;
        }



        if (command == "scheduler-stop") {
            if (!scheduler.isEngineRunning()) {
                ConsoleManager::printLine("Scheduler is not running.");
            } else {
                scheduler.stopGracefully();
                ConsoleManager::printLine(
                    "Scheduler stopped. Remaining processes will finish in the background.");
            }
            continue;
        }



        if (command == "report-util") {

            const std::string report = ReportManager::generateSystemReport(scheduler);

            std::string error;

            if (ReportManager::saveReport(ReportManager::defaultReportPath(), report, error)) {

                ConsoleManager::printLine(ReportManager::reportConfirmationMessage());

            } else {

                ConsoleManager::printLine(error);

            }

            continue;

        }



        if (ScreenManager::isScreenCommand(command)) {

            ScreenManager::handleCommand(command, scheduler, config);

            continue;

        }



        ConsoleManager::printLine("Unknown command. Please try again.");

    }



    scheduler.stop();

    return 0;

}

