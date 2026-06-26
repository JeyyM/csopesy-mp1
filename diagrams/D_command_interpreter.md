# D — Command Interpreter Implementation

## D.1 Config Loading (initialize command)

When the user types `initialize`, `ConfigLoader::loadFromFile` reads
`config.txt` and validates every field.

```mermaid
flowchart TD
    START[initialize command] --> OPEN[Open config.txt]
    OPEN --> EXISTS{File found?}
    EXISTS -- no --> ERR1[Error: cannot open config.txt]
    EXISTS -- yes --> PARSE[Read key-value pairs line by line]
    PARSE --> VALIDATE{All required keys\npresent and valid?}
    VALIDATE -- no --> ERR2[Error: missing or bad value]
    VALIDATE -- yes --> STORE[Fill Config struct:\nnumCpu, scheduler, quantumCycles,\nbatchProcessFreq, minIns, maxIns,\ndelayPerExec, initialProcessCount]
    STORE --> DONE[Return Config to main.cpp\ninitialized = true]
```

---

## D.2 ScreenManager.handleCommand Internals

Full decision tree showing what happens inside each screen sub-command.

```mermaid
flowchart TD
    CMD[screen command string] --> LS{== 'screen -ls'?}
    LS -- yes --> LS_SNAP[scheduler.statusSnapshot]
    LS_SNAP --> LS_REPORT[ReportManager.generateSystemReport]
    LS_REPORT --> LS_PRINT[Print CPU %, cores, running + finished list]
    LS_PRINT --> HINT[Print attach hint]

    LS -- no --> CS{"starts with\n'screen -s '?"}
    CS -- yes --> CS_NAME[Extract process name]
    CS_NAME --> CS_EXISTS{processExists?}
    CS_EXISTS -- yes --> CS_ERR[Print: already exists]
    CS_EXISTS -- no --> CS_ENGINE[ensureEngineRunning\nauto-start if needed]
    CS_ENGINE --> CS_CREATE[scheduler.createUserProcess\ncreateProcessLocked:\nnew Process + addStandardProgram\ninitializeProcessLog + enqueue]
    CS_CREATE --> CS_SCREEN[runProcessScreen\ninner UI loop]

    CS -- no --> RS{"starts with\n'screen -r '?"}
    RS -- yes --> RS_NAME[Extract process name]
    RS_NAME --> RS_FIND[scheduler.findProcess]
    RS_FIND --> RS_FOUND{Found and\nnot Finished?}
    RS_FOUND -- no --> RS_ERR[Print: Process not found]
    RS_FOUND -- yes --> RS_SCREEN[runProcessScreen\ninner UI loop]
```

---

## D.3 ReportManager: Generate System Report

`screen -ls` and `report-util` both call `generateSystemReport`.
The data is a snapshot so running threads do not interfere.

```mermaid
flowchart TD
    CALL[generateSystemReport called] --> SNAP[scheduler.statusSnapshot\nlocks mutex, copies allProcesses_]
    SNAP --> COUNT[Count processes with status Running]
    COUNT --> CALC[Compute coresUsed, coresAvailable,\nutilization = coresUsed / numCpu * 100]
    CALC --> BUILD[Build text block:\nCPU %, cores used/available\nRunning section\nFinished section]
    BUILD --> RETURN[Return string]
    RETURN --> WHERE{Who called?}
    WHERE -- screen -ls --> PRINT[Print to terminal]
    WHERE -- report-util --> SAVE[saveReport to csopesy-log.txt]
```
