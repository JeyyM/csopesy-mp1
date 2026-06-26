# E — Process Representation

## E.1 Process Lifecycle States

A process moves through three states from creation to completion.

```mermaid
flowchart TD
    CREATE([Process created]) -->|"createProcessLocked\nadded to readyQueue_"| READY

    READY["🟡 READY\nwaiting in queue"]
    RUNNING["🟢 RUNNING\non a CPU core"]
    SLEEPING["🔵 SLEEPING\nparked until wakeAtCycle"]
    FINISHED["⚫ FINISHED\nall 600 instructions done"]

    READY       -->|"schedulerLoop\nassigns to free core"| RUNNING
    RUNNING     -->|"all 600 lines done"| FINISHED
    RUNNING     -->|"SLEEP(N) instruction hit\nparked until cpuCycles + N"| SLEEPING
    RUNNING     -->|"RR quantum expired\nPreempted"| READY
    SLEEPING    -->|"tickLoop: cpuCycles\nreached wakeAtCycle"| READY
    FINISHED    --> DONE([stays in allProcesses_\nvisible in screen -ls])
```

---

## E.2 Process Data Structure

Everything the emulator tracks about one process.

```mermaid
classDiagram
    class Process {
        +int id_
        +string name_
        +string creationTimestamp_
        +vector~Instruction~ instructions_   600 steps added at creation
        +atomic~int~ currentLine_            next line to execute
        +atomic~int~ assignedCore_           -1 if not on a core
        +atomic~ProcessStatus~ status_       Ready / Running / Finished
        +atomic~uint64~ sleepUntilCycle_     0 = not sleeping
        +mutex stateMutex_
        +map~string,uint16~ variables_       x=0 y=0 z=0 at start
        +vector~string~ logs_               PRINT output lines
        +string finishTimestamp_

        +initializeStandardVariables()
        +addInstruction(type, text)
        +currentLine() int
        +appendLog(line)
        +getVariable(name) uint16
        +setVariable(name, value)
        +formatSmi() string
    }

    class Instruction {
        +InstructionType type
        +string arg
    }

    class InstructionType {
        <<enumeration>>
        Print
        Declare
        Add
        Subtract
        Sleep
        For
    }

    class ProcessStatus {
        <<enumeration>>
        Ready
        Running
        Finished
    }

    Process "1" --> "600" Instruction : instructions_
    Instruction --> InstructionType
    Process --> ProcessStatus
```

---

## E.3 Standard Program: What Every Process Runs

`addStandardProgram()` builds the same 600-instruction program for every process.
x, y, z all start at 0 (set by `initializeStandardVariables`).

```mermaid
flowchart TD
    START([Process created]) --> VARS[Variables: x=0, y=0, z=0]
    VARS --> LOOP["Repeat 100 times (lines 0..599)"]
    LOOP --> S1["ADD(x, x, 1)  →  x = x + 1"]
    S1 --> S2["PRINT('Value from: ' + x)"]
    S2 --> S3["ADD(y, y, 1)  →  y = y + 1"]
    S3 --> S4["PRINT('Value from: ' + y)"]
    S4 --> S5["ADD(z, z, 1)  →  z = z + 1"]
    S5 --> S6["PRINT('Value from: ' + z)"]
    S6 --> MORE{iteration < 100?}
    MORE -- yes --> S1
    MORE -- no --> DONE([Process Finished, x=y=z=100])
```

---

## E.4 process-smi Output Layout

`formatSmi()` assembles everything visible when the user is inside a process screen.

```mermaid
flowchart TD
    CALL[formatSmi called] --> A[Print: Process name + ID]
    A --> B[Print: Logs section\none line per PRINT that ran]
    B --> FCHK{status == Finished?}
    FCHK -- yes --> C[Print: Finished!]
    FCHK -- no --> D[Print: Current instruction line / total]
    D --> E[Print: Lines of code total]
    E --> F[Print: Variables x= y= z=]
    F --> G[Print: Instruction text at current line]
    G --> SLPCHK{isSleeping?}
    SLPCHK -- yes --> H[Print: sleeping until CPU cycle N]
    SLPCHK -- no --> DONE([Return string])
    H --> DONE
    C --> DONE
```
