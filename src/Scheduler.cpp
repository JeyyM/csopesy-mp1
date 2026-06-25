#include "Scheduler.h"

#include "TimeUtil.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

std::string buildPrintLog(const std::string& timestamp, int core,
                          const std::string& processName) {
    std::ostringstream log;
    log << "(" << timestamp << ") Core:" << core << " \"Hello world from " << processName
        << "!\"";
    return log.str();
}

}  // namespace

Scheduler::~Scheduler() {
    stop();
}

// multi-threaded engine actually gets created:
//   1. One slot per CPU core is set up (coreCurrent_), all empty at
//      first.
//   2. One WORKER THREAD is launched per CPU core (coreThreads_) —
//      these are the threads that will eventually run the processes.
//   3. Exactly ONE SCHEDULER THREAD is launched (schedulerThread_) —
//      this is the dispatcher that decides who goes on which core.

void Scheduler::start(const Config& config) {
    if (engineRunning_.load()) {
        return; // already running, nothing to do
    }

    config_ = config;
    if (config_.numCpu < 1) {
        config_.numCpu = 1; // always have at least 1 core for safety net
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // STEP 1: create one "free desk" slot per CPU core (e.g. 4 cores
        // from the assignment's config -> 4 nullptr slots = 4 free cores).
        coreCurrent_.assign(config_.numCpu, nullptr);
    }

    engineRunning_.store(true);

    coreThreads_.clear();
    // STEP 2: one worker thread per CPU core. Each thread runs coreLoop()
    // and only ever looks after "its own" core (coreId).
    for (int core = 0; core < config_.numCpu; ++core) {
        coreThreads_.emplace_back(&Scheduler::coreLoop, this, core);
    }

    // STEP 3: exactly one dispatcher/scheduler thread, shared by all cores.
    schedulerThread_ = std::thread(&Scheduler::schedulerLoop, this);
}

// Signals every thread to stop and waits for them to actually finish
// before returning, so the program never exits while a thread is still mid-execution.
void Scheduler::stop() {
    if (!engineRunning_.exchange(false)) {
        return; // wasn't running, nothing to stop
    }

    cv_.notify_all(); // wake up any thread that might be sleeping/waiting

    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
    for (std::thread& worker : coreThreads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    coreThreads_.clear();
}

// Builds one process, gives it `printInstructions` PRINT commands, and
// adds it to the BACK of the ready queue. Adding to the back (and only
// ever removing from the front, in schedulerLoop) is what guarantees
// First-Come-First-Serve ordering.
std::shared_ptr<Process> Scheduler::createProcessLocked(const std::string& name,
                                                        int printInstructions,
                                                        bool includeMockPreview) {
    auto process = std::make_shared<Process>(nextId_++, name, TimeUtil::formatNow());
    if (includeMockPreview) {
        addMockInstructionPreview(process);
    }
    while (process->totalLines() < printInstructions) {
        process->addPrintInstruction();
    }
    allProcesses_.push_back(process);
    readyQueue_.push_back(process);
    return process;
}

void Scheduler::addMockInstructionPreview(const std::shared_ptr<Process>& process) {
    process->addPrintInstruction();
    process->addInstruction(InstructionType::Declare, "DECLARE(counter, 0)");
    process->addInstruction(InstructionType::Add, "ADD(counter, counter, 1)");
    process->addInstruction(InstructionType::Subtract, "SUBTRACT(counter, counter, 1)");
    process->addInstruction(InstructionType::Sleep, "SLEEP(1)");
    process->addInstruction(InstructionType::For, "FOR([PRINT], 2)");
}

std::shared_ptr<Process> Scheduler::createProcess(const std::string& name,
                                                  int printInstructions) {
    std::shared_ptr<Process> process;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process = createProcessLocked(name, printInstructions, true);
    }
    cv_.notify_all();
    return process;
}

// Creates a whole batch of processes at once.
// (see main.cpp, where this is called right after "initialize").
int Scheduler::generateBatch(int count, int printInstructions) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < count; ++i) {
            std::ostringstream name;
            name << "process" << std::setw(2) << std::setfill('0') << (i + 1);
            createProcessLocked(name.str(), printInstructions, false);
        }
    }
    cv_.notify_all();
    return count;
}

bool Scheduler::processExistsLocked(const std::string& name) const {
    for (const auto& process : allProcesses_) {
        if (process->name() == name) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Process> Scheduler::addSchedulerDummyProcessLocked(const std::string& name,
                                                                   int id,
                                                                   const std::string& timestamp,
                                                                   int totalLines) {
    if (processExistsLocked(name)) {
        return nullptr;
    }

    auto process = std::make_shared<Process>(id, name, timestamp);
    addMockInstructionPreview(process);
    while (process->totalLines() < totalLines) {
        process->addPrintInstruction();
    }

    const int core = config_.numCpu > 0 ? (id % config_.numCpu) : 0;
    process->appendLog(buildPrintLog(timestamp, core, name));
    allProcesses_.push_back(process);
    readyQueue_.push_back(process);

    if (nextId_ <= id) {
        nextId_ = id + 1;
    }
    return process;
}

void Scheduler::addSchedulerDummyProcessesLocked() {
    if (schedulerDummyProcessesGenerated_) {
        return;
    }

    addSchedulerDummyProcessLocked("p01", 101, "01/18/2024 09:20:00AM", 1000);
    addSchedulerDummyProcessLocked("p02", 102, "01/18/2024 09:20:01AM", 1200);
    addSchedulerDummyProcessLocked("p03", 103, "01/18/2024 09:20:02AM", 1400);
    addSchedulerDummyProcessLocked("p04", 104, "01/18/2024 09:20:03AM", 1600);
    addSchedulerDummyProcessLocked("p05", 105, "01/18/2024 09:20:04AM", 1800);

    schedulerDummyProcessesGenerated_ = true;
}

int Scheduler::generateSchedulerDummyProcesses() {
    int added = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t before = allProcesses_.size();
        addSchedulerDummyProcessesLocked();
        added = static_cast<int>(allProcesses_.size() - before);
    }
    if (added > 0) {
        cv_.notify_all();
    }
    return added;
}

std::shared_ptr<Process> Scheduler::findProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& process : allProcesses_) {
        if (process->name() == name) {
            return process;
        }
    }
    return nullptr;
}

bool Scheduler::processExists(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return processExistsLocked(name);
}

// This is the ONE thread (from start()) whose only job is matchmaking:
// "is there a process waiting AND a free core?" If yes, send the
// process at the FRONT of the ready queue to that free core.
//
// Step by step, every loop iteration:
//   1. Sleep/wait briefly until there's actual work to do — either a
//      waiting process AND a free core, or the engine is shutting
//      down.
//   2. If the engine was stopped while waiting, exit the loop.
//   3. Otherwise, go through each CPU core. For every core that is
//      currently free (nullptr), take the process at the FRONT of the
//      ready queue (FCFS — first in line gets the next free core) and
//      assign it to that core.
//   4. Wake up the core threads so the one that just got a new job can
//      start running it right away.
void Scheduler::schedulerLoop() {
    while (engineRunning_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // STEP 1: wait until there's something to dispatch, or we're
            // told to shut down. The lambda below is the "wake-up
            // condition" — cv_.wait_for keeps sleeping until it returns
            // true (or 5ms passes, just so we periodically re-check).
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                if (!engineRunning_.load()) {
                    return true; // shutting down — stop waiting
                }
                if (readyQueue_.empty()) {
                    return false; // nobody waiting, nothing to dispatch
                }
                for (const auto& slot : coreCurrent_) {
                    if (slot == nullptr) {
                        return true; // found a free core — there's work to do
                    }
                }
                return false; // all cores busy, nothing to dispatch yet
            });

            // STEP 2: if we woke up because of shutdown, leave the loop.
            if (!engineRunning_.load()) {
                break;
            }

            // STEP 3: hand out the next process in line (FCFS) to every
            // free core we can find right now.
            for (std::size_t core = 0; core < coreCurrent_.size() && !readyQueue_.empty();
                 ++core) {
                if (coreCurrent_[core] == nullptr) {
                    std::shared_ptr<Process> next = readyQueue_.front(); // first in line
                    readyQueue_.pop_front();  // remove from line
                    next->setAssignedCore(static_cast<int>(core));
                    next->setStatus(ProcessStatus::Running);
                    coreCurrent_[core] = next; // "seat" the process at this core's desk
                }
            }
        }
        // STEP 4: wake up the core worker threads so the one that just
        // received a job can start executing it immediately.
        cv_.notify_all();
    }
}

//Each CPU core gets its OWN thread running this function (started in
// start()). Think of each one as a dedicated worker who only ever
// looks at their own "desk" (coreCurrent_[coreId]):
//   1. Wait until the scheduler thread has placed a process on this
//      core's desk (or until we're told to shut down).
//   2. If we're shutting down and there's no job waiting, this worker
//      can stop entirely.
//   3. Otherwise, grab the assigned process and actually run it
//      (executeProcess does the real work, including writing the
//      process's print output to its text file).
//   4. Once finished, clear this core's desk (mark it free again) so
//      the scheduler thread knows it can assign someone new here.
void Scheduler::coreLoop(int coreId) {
    while (true) {
        std::shared_ptr<Process> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // STEP 1: sleep until this specific core has been given a
            // process to run, or the engine is shutting down.
            cv_.wait(lock, [this, coreId] {
                return !engineRunning_.load() || coreCurrent_[coreId] != nullptr;
            });

            // STEP 2: shutting down and nothing left to run on this core
            // -> this worker thread is done for good.
            if (!engineRunning_.load() && coreCurrent_[coreId] == nullptr) {
                return;
            }
            job = coreCurrent_[coreId];
        }

        if (job) {
            // STEP 3: actually run the process from start to finish.
            executeProcess(job, coreId);
            {
                // STEP 4: this core is free again — let the scheduler
                // thread know by clearing this core's desk.
                std::lock_guard<std::mutex> lock(mutex_);
                coreCurrent_[coreId] = nullptr;
            }
            cv_.notify_all(); // wake the scheduler thread to assign someone new here
        }
    }
}

// executeProcess()  —  THE PRINT COMMAND IMPLEMENTATION
// every process gets its OWN text file, and every PRINT
// instruction it runs gets written into that file with the timestamp
// it ran AND which CPU core ran it — matching the sample format given
// in the assignment
//
// Step by step:
//   1. Open (or create) a text file named "<process name>.txt" and
//      write a small header into it. std::ios::trunc means "start the
//      file empty" so re-running doesn't just keep appending forever.
//   2. Go through this process's instructions ONE AT A TIME, starting
//      from wherever it left off (process->currentLine() — useful if a
//      process was ever paused and resumed, though in this FCFS-only
//      assignment it normally runs straight through).
//   3. For each instruction: figure out the message to print, then
//      build one log line containing the current timestamp, which
//      core executed it, and the message — exactly the format the
//      assignment asks for.
//   4. Save that line in TWO places:
//        a) the process's in-memory log (so "process-smi" can show it
//           live on screen), and
//        b) the process's own text file on disk (the actual graded
//           deliverable for this homework).
//   5. Advance the process's progress counter by one line.
//   6. Sleep briefly to simulate the instruction "taking time" to
//      execute (this is what lets "screen -ls" visibly show progress
//      over a second or two, instead of everything finishing
//      instantly). The sleep length grows with the config's
//      delay-per-exec setting.
//   7. Once every instruction has run, mark the process Finished and
//      record the finish timestamp (used by "screen -ls"'s "Finished
//      processes" list).
void Scheduler::executeProcess(const std::shared_ptr<Process>& process, int coreId) {
    // STEP 1: create/open this process's dedicated output file.
    const std::string fileName = process->name() + ".txt";
    std::ofstream logFile(fileName, std::ios::trunc);
    if (logFile.is_open()) {
        logFile << "Process name: " << process->name() << "\n";
        logFile << "Logs:\n\n";
    }

    const int total = process->totalLines();
    const auto& instructions = process->instructions();

    // STEP 2: run each instruction in order, starting from wherever this
    // process left off.
    for (int line = process->currentLine(); line < total; ++line) {
        if (!engineRunning_.load()) {
            return;  // Abort promptly on shutdown; leaves the process unfinished.
        }

        // STEP 3: figure out what text to print for this instruction.
        const Instruction& instruction = instructions[line];
        if (instruction.type != InstructionType::Print) {
            process->setCurrentLine(line + 1);
            continue;
        }

        std::string message = instruction.arg.empty() ? process->defaultPrintMessage()
                                                       : instruction.arg;

        // Build the log line in the required format
        std::ostringstream logLine;
        logLine << "(" << TimeUtil::formatNow() << ") Core:" << coreId << " \"" << message
                << "\"";

        // STEP 4a: remember this line in memory (for "process-smi").
        process->appendLog(logLine.str());

        // STEP 4b: write this same line into the process's own text
        // file — this is the actual graded output for the "print"
        // command requirement.
        if (logFile.is_open()) {
            logFile << logLine.str() << "\n";
            logFile.flush();
        }

        // STEP 5: move the progress counter forward by one instruction.
        process->setCurrentLine(line + 1);

        // STEP 6: Pause for a short time to simulate the instruction being
        // executed. This makes process execution visible in real time instead
        // of finishing instantly.
        const int cycles = static_cast<int>(config_.delayPerExec) + 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(kCycleMs * cycles));
    }

    // STEP 7: every instruction has run — this process is officially done.
    process->setStatus(ProcessStatus::Finished);
    process->setFinishTimestamp(TimeUtil::formatNow());
}

std::string Scheduler::buildStatusReport() {
    std::vector<std::shared_ptr<Process>> processes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processes = allProcesses_;
    }

    // Count how many cores are currently busy running something, so we
    // can show "CPU utilization" and "Cores used/available".
    int coresUsed = 0;
    for (const auto& process : processes) {
        if (process->status() == ProcessStatus::Running) {
            ++coresUsed;
        }
    }
    if (coresUsed > config_.numCpu) {
        coresUsed = config_.numCpu;
    }
    const int coresAvailable = config_.numCpu - coresUsed;
    const int utilization =
        config_.numCpu > 0 ? (coresUsed * 100) / config_.numCpu : 0;

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
