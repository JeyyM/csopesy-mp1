# G — Instruction Engine

## G.1 Parse → Execute Pipeline

Every instruction goes through two stages: parsing (text to struct) and
execution (struct applied to a Process).

```mermaid
flowchart LR
    TEXT["Raw string\ne.g. 'ADD(x, x, 1)'"] --> PARSE["parseSingleInstruction\ndetect keyword"]
    PARSE --> STRUCT["Instruction struct\n{ type=Add, arg='ADD(x,x,1)' }"]
    STRUCT --> EXEC["InstructionEngine::execute\napply to Process"]
    EXEC --> RESULT["ExecuteResult\n{ producedLog?, relinquishCpu? }"]
    RESULT --> SCHED["Scheduler handles:\nappend log / park process"]
```

---

## G.2 parseSingleInstruction Decision Tree

```mermaid
flowchart TD
    IN["Input string (trimmed)"] --> N1{Starts with PRINT?}
    N1 -- yes --> P1["type = Print\narg = inside parens only"]
    N1 -- no --> N2{Starts with ADD?}
    N2 -- yes --> P2["type = Add\narg = full text"]
    N2 -- no --> N3{Starts with SUBTRACT?}
    N3 -- yes --> P3["type = Subtract\narg = full text"]
    N3 -- no --> N4{Starts with DECLARE?}
    N4 -- yes --> P4["type = Declare\narg = full text"]
    N4 -- no --> N5{Starts with SLEEP?}
    N5 -- yes --> P5["type = Sleep\narg = full text"]
    N5 -- no --> N6{Starts with FOR?}
    N6 -- yes --> P6["type = For\narg = full text"]
    N6 -- no --> FALLBACK["type = Print\narg = raw text (fallback)"]
```

---

## G.3 How Instructions Affect a Process

Each instruction type modifies a different part of the process's data.
This shows what changes inside the Process object after each opcode runs.

```mermaid
flowchart LR
    subgraph PROCESS["Process State"]
        direction TB
        VARS["variables_\nkey-value map\nx, y, z, ..."]
        LOGS["logs_\nlist of output lines"]
        SLEEP_F["sleepUntilCycle_\nwake cycle number"]
        LINE["currentLine_\nadvances after each instruction"]
    end

    ADD["ADD / SUBTRACT\nADD(dest, src1, src2)"] -->|"updates dest variable"| VARS
    DECL["DECLARE\nDECLARE(name, value)"] -->|"creates / sets variable"| VARS
    PRINT["PRINT\nPRINT('msg' + var)"] -->|"appends formatted log line"| LOGS
    SLEEP["SLEEP\nSLEEP(N)"] -->|"sets wakeAtCycle = now + N\nrelinquishes core"| SLEEP_F
    FOR["FOR\nFOR(body, N)"] -->|"expands to N × body instructions\nno direct state change"| LINE
```

---

## G.4 execute() Dispatch — Which Handler Runs

```mermaid
flowchart LR
    EXEC["execute\nInstruction + Process + coreId"] --> SW{type?}

    subgraph OPCODES["Handler for each opcode"]
        direction TB
        PR["PRINT('message' + variable)\nresolve message text\nbuild log line with timestamp + core\nmark producedLog = true"]
        DE["DECLARE(name, value)\nset variable to literal value\nprocess.setVariable(name, val)"]
        AS["ADD(dest, src1, src2)\nSUBTRACT(dest, src1, src2)\nresolve src1 and src2\n(literal or variable lookup)\ncompute result, clamp 0–65535\nstore in dest variable"]
        SL["SLEEP(N)\nmark relinquishCpu = true\nset sleepTicks = N\nscheduler parks process\nuntil cpuCycles + N"]
        FO["FOR(body, N)\nno-op here\nhandled by Scheduler\nrunInstructionTree\n(expands loop + checks quantum)"]
    end

    SW -->|Print| PR
    SW -->|Declare| DE
    SW -->|Add/Sub| AS
    SW -->|Sleep| SL
    SW -->|For| FO
```
