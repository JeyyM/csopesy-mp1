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

## G.3 execute() What Each Opcode Does

```mermaid
flowchart LR
    EXEC["execute\nInstruction + Process + coreId"] --> SW{type?}

    subgraph OPCODES["What each opcode does"]
        direction TB
        PR["PRINT\nresolve message text\nbuild log line with timestamp + core\nmark producedLog = true"]
        DE["DECLARE\nset variable to literal value\nprocess.setVariable(name, val)"]
        AS["ADD / SUBTRACT\nresolve src1 and src2\n(literal or variable lookup)\ncompute result, clamp 0–65535\nstore in dest variable"]
        SL["SLEEP\nmark relinquishCpu = true\nset sleepTicks = N\nscheduler parks process\nuntil cpuCycles + N"]
        FO["FOR\nno-op here\nhandled by Scheduler\nrunInstructionTree\n(expands loop + checks quantum)"]
    end

    SW -->|Print| PR
    SW -->|Declare| DE
    SW -->|Add/Sub| AS
    SW -->|Sleep| SL
    SW -->|For| FO
```
