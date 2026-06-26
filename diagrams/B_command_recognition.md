# B — Command Recognition

## B.1 Before Initialization Gate

Only two commands work before `initialize` is called.
All others are blocked with an error message.

```mermaid
flowchart TD
    INPUT[User types command] --> T1{== 'exit'?}
    T1 -- yes --> DO_EXIT[Stop scheduler, clear outputs, quit]
    T1 -- no --> T2{== 'initialize'?}
    T2 -- yes --> DO_INIT[Load config.txt]
    T2 -- no --> T3{== 'outputs-clear'?}
    T3 -- yes --> DO_OUT[Clear outputs/ folder]
    T3 -- no --> T4{initialized == true?}
    T4 -- no --> BLOCK[Print: Please initialize first]
    T4 -- yes --> POST_INIT_CMDS[Post-init command routing]
```

---

## B.2 Post-Initialization Command Routing

After `initialize` succeeds, every recognized command has a dedicated handler.

```mermaid
flowchart TD
    CMD[Recognized command string] --> C1{== 'clear'?}
    C1 -- yes --> H_CLEAR[ConsoleManager.clearScreen + printHeader]
    C1 -- no --> C2{== 'scheduler-start'?}
    C2 -- yes --> H_SS[Start scheduler threads + enable batch spawning]
    C2 -- no --> C3{== 'scheduler-stop'?}
    C3 -- yes --> H_ST[Graceful stop: drain, no new spawns]
    C3 -- no --> C4{== 'report-util'?}
    C4 -- yes --> H_REP[Generate report + save to csopesy-log.txt]
    C4 -- no --> C5{ScreenManager.isScreenCommand?}
    C5 -- yes --> H_SCR[ScreenManager.handleCommand]
    C5 -- no --> C6{== 'outputs-clear'?}
    C6 -- yes --> H_OUT[Delete all files in outputs/]
    C6 -- no --> H_UNK[Print: Unknown command. Please try again.]
```

---

## B.3 Screen Command Recognition Detail

`ScreenManager.isScreenCommand()` checks for four specific patterns
before `handleCommand()` dispatches to the right action.

```mermaid
flowchart TD
    S[Command string] --> P1{"== 'screen'\n(no args)"}
    P1 -- yes --> E1[Print: screen requires arguments]
    P1 -- no --> P2{"== 'screen -ls'"}
    P2 -- yes --> E2[Show CPU report + running/finished list]
    P2 -- no --> P3{"starts with\n'screen -s '"}
    P3 -- yes --> E3[Create new process with that name]
    P3 -- no --> P4{"starts with\n'screen -r '"}
    P4 -- yes --> E4[Re-attach to existing running process]
    P4 -- no --> P5[Not a screen command → returns false]
```
