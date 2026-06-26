# C — Console UI Implementation

## C.1 ConsoleManager Responsibilities

ConsoleManager is a thin utility layer. It owns no state — it only
prints things to the terminal. Everything visual goes through here.

```mermaid
flowchart LR
    subgraph ConsoleManager
        A[clearScreen\nruns 'cls' on Windows]
        B[printHeader\nASCII art + developer names]
        C[printPrompt\nprints 'root:\\> ']
        D[printLine\nprints any text + newline]
        E[printProcessScreenHint\nshows process screen help bar]
        F[printLsAttachHint\nsuggests screen -r after screen -ls]
    end

    MAIN["main.cpp"] -->|calls on start| B
    MAIN -->|each loop iteration| C
    MAIN -->|all messages| D
    SCREEN["ScreenManager"] -->|entering a process| A
    SCREEN -->|process screen bar| E
    SCREEN -->|exiting a process| A
    SCREEN -->|after screen -ls| F
```

---

## C.2 Main Shell Loop (how UI stays alive)

The shell is a simple infinite loop. It never blocks in a busy spin —
`std::getline` parks the thread until the user presses Enter.

```mermaid
sequenceDiagram
    participant User
    participant main
    participant ConsoleManager

    main->>ConsoleManager: clearScreen()
    main->>ConsoleManager: printHeader()

    loop While running == true
        main->>ConsoleManager: printPrompt()  prints root:\\>
        User-->>main: types command + Enter
        main->>main: trim whitespace
        alt empty input
            main->>main: skip (loop back)
        else valid command
            main->>main: handle command
        end
    end
```

---

## C.3 Process Screen UI Loop

When the user enters a process screen (via screen -s or screen -r),
the outer shell loop is paused. A separate inner loop runs instead.
The user returns to the main menu by typing 'exit' inside the process screen.

```mermaid
flowchart TD
    ENTER[User typed 'screen -s name'\nor 'screen -r name'] --> CLEAR[ConsoleManager.clearScreen]
    CLEAR --> SHOW_SMI[Print process-smi info\nname, ID, logs, variables, progress]
    SHOW_SMI --> HINT[Print process screen help bar]
    HINT --> INNER_PROMPT[Print 'root:\\>' prompt]
    INNER_PROMPT --> READ[Read command]
    READ --> IS_SMI{command ==\n'process-smi'?}
    IS_SMI -- yes --> SHOW_SMI
    IS_SMI -- no --> IS_EXIT{command ==\n'exit'?}
    IS_EXIT -- no --> UNKNOWN[Print: Unknown command inside process screen]
    UNKNOWN --> INNER_PROMPT
    IS_EXIT -- yes --> RESTORE[ConsoleManager.clearScreen + printHeader]
    RESTORE --> BACK([Return to main shell loop])
```
