# E — Process Representation

## E.1 Process Lifecycle States

A process moves through three states from creation to completion.

```mermaid
flowchart TD
    CREATE([Process created]) -->|"createProcessLocked\nadded to readyQueue_"| READY

    READY["🟡 READY\nwaiting in queue"]
    RUNNING["🟢 RUNNING\non a CPU core"]
    SLEEPING["🔵 SLEEPING\nparked until wakeAtCycle"]
    FINISHED["⚫ FINISHED\nall instructions done"]

    READY       -->|"schedulerLoop\nassigns to free core"| RUNNING
    RUNNING     -->|"all instructions done"| FINISHED
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
        +vector~Instruction~ instructions_   program built at creation
        +atomic~int~ currentLine_            next line to execute
        +atomic~int~ assignedCore_           -1 if not on a core
        +atomic~ProcessStatus~ status_       Ready / Running / Finished
        +atomic~uint64~ sleepUntilCycle_     0 = not sleeping
        +mutex stateMutex_
        +map~string,uint16~ variables_       initialized at creation
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

    Process "1" --> "N" Instruction : instructions_
    Instruction --> InstructionType
    Process --> ProcessStatus
```

---

## E.3 Process Program Execution

Every process has a list of instructions built at creation.
The core thread executes them sequentially until all are done.
Note: in this implementation, `addStandardProgram()` loads the same
test-case program into every process (ADD and PRINT on x, y, z variables).

```mermaid
flowchart TD
    START([Process created]) --> VARS[Variables initialized from program definition]
    VARS --> LOOP["Execute instruction list sequentially"]
    LOOP --> INST["Run current instruction:\nADD / SUBTRACT / PRINT / DECLARE / SLEEP / FOR"]
    INST --> MORE{more instructions?}
    MORE -- yes --> LOOP
    MORE -- no --> DONE([Process Finished])
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
    E --> F[Print: Variables and their current values]
    F --> G[Print: Instruction text at current line]
    G --> SLPCHK{isSleeping?}
    SLPCHK -- yes --> H[Print: sleeping until CPU cycle N]
    SLPCHK -- no --> DONE([Return string])
    H --> DONE
    C --> DONE
```
