# A — System Overview

## A.1 High-Level Module Map

Shows every source file and how they depend on each other.
Arrow means "uses / calls".

```mermaid
graph TD
    subgraph ENTRY["Entry Point"]
        MAIN["main.cpp"]
    end

    subgraph CLI["CLI Layer"]
        CON["ConsoleManager\nprint / clear / header"]
        SCR["ScreenManager\nscreen commands"]
        REP["ReportManager\nscreen -ls / report-util"]
    end

    subgraph CORE["Core Engine"]
        CFG["Config\nconfig.txt settings"]
        SCHED["Scheduler\nCPU cores · queues · threads"]
        PROC["ProcessModel\nper-process data"]
        ENG["InstructionEngine\nparse + run instructions"]
    end

    subgraph SUPPORT["Support"]
        OUT["OutputManager\noutputs/ folder"]
        TIME["TimeUtil\ntimestamps"]
    end

    MAIN --> CFG
    MAIN --> CON
    MAIN --> SCR
    MAIN --> REP
    MAIN --> SCHED
    MAIN --> OUT

    SCR --> SCHED
    SCR --> REP
    SCR --> CON
    SCR --> PROC

    REP --> SCHED
    REP --> PROC

    SCHED --> PROC
    SCHED --> ENG
    SCHED --> OUT
    SCHED --> TIME

    ENG --> PROC
    ENG --> TIME
```

---

## A.2 Full Command Flow (top-level)

What happens from the moment the user types a command to when the result appears.

```mermaid
flowchart TD
    START([Program starts]) --> HEADER[Print header]
    HEADER --> PROMPT[/"root:\> prompt — waiting for input"/]
    PROMPT --> READ[Read line from keyboard]
    READ --> EMPTY{Empty input?}
    EMPTY -- yes --> PROMPT

    EMPTY -- no --> EXIT_CHK{"exit?"}
    EXIT_CHK -- yes --> STOP_SCHED[scheduler.stop]
    STOP_SCHED --> CLEAR_OUT[Clear outputs/ folder]
    CLEAR_OUT --> END([Program ends])

    EXIT_CHK -- no --> INIT_CHK{"initialize?"}
    INIT_CHK -- yes --> LOAD_CFG[Load config.txt]
    LOAD_CFG -- error --> PRINT_ERR[Print error] --> PROMPT
    LOAD_CFG -- ok --> MARK_INIT[initialized = true\nprint settings] --> PROMPT

    INIT_CHK -- no --> GATE{"initialized?"}
    GATE -- no --> BLOCKED[Print: Please initialize first] --> PROMPT

    GATE -- yes --> subCMD

    subgraph subCMD["Command Handlers"]
        direction TB
        H1["clear\nclearScreen + printHeader"]
        H2["scheduler-start\nstart threads + batch spawning"]
        H3["scheduler-stop\ngraceful drain, no new spawns"]
        H4["report-util\nbuild report + save log file"]
        H5["screen *\nScreenManager.handleCommand"]
        H6["outputs-clear\ndelete outputs/ files"]
        H7["unknown\nprint error message"]
    end

    subCMD --> DONE[ ] --> PROMPT

    style DONE fill:none,stroke:none
```
