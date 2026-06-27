# CSOPESY OS Emulator — Technical Report Diagrams

Read these files in order. Each letter matches one of the five report topics.

| File | Contents | Report Topic |
|------|----------|--------------|
| `A_system_overview.md`        | A.1 Module map · A.2 Full command flow                         | Whole system at a glance |
| `B_command_recognition.md`    | B.1 Before-init gate · B.2 Post-init routing · B.3 Screen patterns | Command recognition |
| `C_console_ui.md`             | C.1 ConsoleManager · C.2 Shell loop · C.3 Process screen loop  | Console UI implementation |
| `D_command_interpreter.md`    | D.1 Config loading · D.2 ScreenManager detail · D.3 Report build | Command interpreter |
| `E_process_representation.md` | E.1 States · E.2 Data structure · E.3 Standard program · E.4 process-smi | Process representation |
| `F_scheduler_implementation.md` | F.1 Threads · F.2 Queues · F.3 FCFS/RR · F.4 Time slice · F.5 Start/stop · F.6 Batch | Scheduler implementation |
| `G_instruction_engine.md`     | G.1 Pipeline · G.2 Parse tree · G.3 Process state changes · G.4 Opcode dispatch | (Instruction engine, used by scheduler) |
| `WRITEUP_pseudocode.txt`      | Plain-text pseudocode for every section above (A through G)    | All topics — written explanation |

---

## How to view the Mermaid diagrams

The `.md` files contain fenced Mermaid code blocks.
You can paste them into any of these free tools to render them visually:

- **Mermaid Live Editor** — https://mermaid.live  (paste the mermaid block)
- **VS Code / Cursor** — install the "Markdown Preview Mermaid Support" extension,
  then open the `.md` file and press `Ctrl+Shift+V` to preview.
- **GitHub** — push the files; GitHub renders Mermaid in Markdown automatically.

---

## Diagram code key

| Code | Meaning |
|------|---------|
| A    | System Overview |
| B    | Command Recognition |
| C    | Console UI |
| D    | Command Interpreter |
| E    | Process Representation |
| F    | Scheduler |
| G    | Instruction Engine |
| .1 .2 … | Sub-diagram number within that section |
