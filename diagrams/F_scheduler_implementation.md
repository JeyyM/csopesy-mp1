# F — Scheduler Implementation

## F.1 Thread Architecture

The scheduler runs three types of background threads simultaneously
while the main thread stays at the `root:\>` prompt.

```mermaid
flowchart TD
    subgraph Main Thread
        PROMPT["root:\\> prompt (waiting for input)"]
    end

    subgraph Scheduler Background Threads
        TICK["tickLoop\none thread\nwakes every ~1ms\nincrements cpuCycles_\nspawns batch / wakes SLEEP"]
        DISP["schedulerLoop\none thread\nmoves readyQueue_ front\nto free coreCurrent_ slot"]
        C0["coreLoop(0)\nruns one process slice\non core 0"]
        C1["coreLoop(1)\nruns one process slice\non core 1"]
        CN["coreLoop(N-1)\n... up to numCpu cores"]
    end

    TICK -->|cpuCycles++| DISP
    TICK -->|wakes SLEEP| DISP
    DISP -->|assigns job| C0
    DISP -->|assigns job| C1
    DISP -->|assigns job| CN
    C0 -->|Preempted → back of queue| DISP
    C1 -->|Preempted → back of queue| DISP
    CN -->|Preempted → back of queue| DISP
```

---

## F.2 Data Flow Between Queues

How a process moves between the three queues during its lifetime.

```mermaid
flowchart LR
    CREATE([Process created]) --> RQ[readyQueue_\nFIFO waiting list]
    RQ -->|schedulerLoop picks first free core| CC[coreCurrent_ slot\nactually running]
    CC -->|RR quantum done = Preempted| RQ
    CC -->|SLEEP instruction| SQ[sleepingProcesses_\nparked until wakeAtCycle]
    SQ -->|tickLoop: cpuCycles reached wakeAt| RQ
    CC -->|all 600 lines done| FIN([Finished\nstays in allProcesses_\nfor screen -ls])
```

---

## F.3 FCFS vs Round Robin

The only difference between the two scheduling modes is `quantumBudgetCycles()`.

```mermaid
flowchart TD
    CONFIG{Scheduler type\nfrom config.txt}
    CONFIG -- FCFS --> QF["quantumBudgetCycles = max uint32\n(~4 billion)\nProcess runs until it finishes\nbefore any other process gets CPU"]
    CONFIG -- RR --> QR["quantumBudgetCycles = quantumCycles\n(e.g. 20)\nProcess runs 20 instruction-cycles\nthen goes to back of readyQueue_"]
    QF --> SLICE[runProcessSlice\nsame code for both]
    QR --> SLICE
    SLICE --> CHECK{usedCycles + cost\n> maxCycles?}
    CHECK -- yes --> PREEMPT[Return Preempted\n→ requeueProcess]
    CHECK -- no --> EXEC[Execute instruction\nadvance currentLine]
```

---

## F.4 One RR Time Slice (runProcessSlice + runInstructionTree)

Detailed walk through executing one quantum on one core.

```mermaid
flowchart TD
    SLICE_START([coreLoop calls runProcessSlice]) --> OPEN[Open outputs/name.txt in append mode]
    OPEN --> LINE_LOOP[For each line from currentLine to end]
    LINE_LOOP --> ENGINE_CHK{engineRunning?}
    ENGINE_CHK -- no --> ABORT[Return Aborted]
    ENGINE_CHK -- yes --> TREE[runInstructionTree for this instruction]

    TREE --> FOR_CHK{Is it a FOR\ninstruction?}
    FOR_CHK -- yes --> PARSE_FOR[parseForInstruction\nget body list + repeat count]
    PARSE_FOR --> FOR_LOOP[Nested loop:\nrepeat × body size recursive calls]
    FOR_LOOP --> TREE
    FOR_CHK -- no --> BUDGET{usedCycles + cost\n> maxCycles?}
    BUDGET -- yes --> PREEMPT[Return Preempted]
    BUDGET -- no --> RUN[InstructionEngine.execute\nrun ADD / PRINT / etc.]
    RUN --> LOG_CHK{Produced log?}
    LOG_CHK -- yes --> APPEND[process.appendLog\nwrite to .txt file]
    LOG_CHK -- no --> SLEEP_CHK{Relinquish CPU?}
    APPEND --> SLEEP_CHK
    SLEEP_CHK -- yes --> PARK[markProcessSleeping\nReturn Sleeping]
    SLEEP_CHK -- no --> INC[usedCycles += cost\ncurrentLine++]
    INC --> LINE_LOOP

    LINE_LOOP --> ALL_DONE[All lines done]
    ALL_DONE --> FINISH[setStatus Finished\nsetFinishTimestamp\nReturn Finished]
```

---

## F.5 scheduler-start vs scheduler-stop

```mermaid
sequenceDiagram
    participant User
    participant main
    participant Scheduler
    participant tickLoop
    participant coreLoops

    User->>main: scheduler-start
    main->>Scheduler: start(config) — spawn tickLoop, schedulerLoop, coreLoops
    main->>Scheduler: enableBatchGeneration()
    Note over tickLoop: every tick: maybeSpawnBatchProcess\nadds processN to readyQueue_
    Note over coreLoops: pick from readyQueue_, run slices

    User->>main: scheduler-stop
    main->>Scheduler: stopGracefully()
    Scheduler->>Scheduler: disableBatchGeneration() — no new spawns
    Scheduler->>Scheduler: stopTickThread = true
    Note over Scheduler: finishGracefulStop runs in background\nwaits for all processes to Finish\nthen joins all threads
    main-->>User: Scheduler stopped. Remaining processes finish in background.
```

---

## F.6 Batch Spawn Rate

`maybeSpawnBatchProcess()` fires on every `batchProcessFreq`-th CPU cycle.

```mermaid
flowchart TD
    TICK[cpuCycles++ in tickLoop] --> ACT{batchGenerationActive?}
    ACT -- no --> SKIP[Skip]
    ACT -- yes --> FREQ{cycles % batchProcessFreq == 0?}
    FREQ -- no --> SKIP
    FREQ -- yes --> DUP{Same as lastBatchSpawnCycle?}
    DUP -- yes --> SKIP
    DUP -- no --> SPAWN["spawnAutoBatchProcessLocked:\nname = 'process' + nextProcessNumber_++\ncreateProcesLocked → readyQueue_"]
    SPAWN --> NOTIFY[cv_.notify_all\nwake schedulerLoop]
```
