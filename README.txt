CSOPESY OS Emulator
===================
S09 - Group 9
Members: Miranda, Juan Miguel | Alcantara, Van Asher | Rojo, Von Matthew | Capote, Mary Grace

Entry Class File: src/main.cpp
GitHub: https://github.com/JeyyM/csopesy-mp1

----------------------------------------------------------------------
BUILDING AND RUNNING
----------------------------------------------------------------------

  cd "c:\<path to>\CSOPESY OS MP"
  cmake --build build
  .\build\csopesy_os_mp.exe

The program displays the CSOPESY header and a root:\> prompt on startup.

----------------------------------------------------------------------
CONFIG.TXT
----------------------------------------------------------------------

Place config.txt in the same folder as the executable before running
initialize. All fields are required.

  num-cpu             Number of CPU cores (1-128)
  scheduler           Scheduling algorithm: "fcfs" or "rr"
  quantum-cycles      Cycles per RR time slice (ignored for FCFS)
  batch-process-freq  Spawn one new process every N CPU cycles
  min-ins             Minimum instructions per process (parsed, reserved)
  max-ins             Maximum instructions per process (parsed, reserved)
  delay-per-exec      Extra milliseconds of delay per instruction (0 = fast)

Example config.txt:
  num-cpu 10
  scheduler "rr"
  quantum-cycles 20
  batch-process-freq 1
  min-ins 1000
  max-ins 1000
  delay-per-exec 0

----------------------------------------------------------------------
COMMANDS
----------------------------------------------------------------------

The following commands are always available (before and after initialize):

  exit
      Stops the scheduler, clears the outputs/ folder, and exits.

  outputs-clear
      Deletes all files inside the outputs/ folder.

----------------------------------------------------------------------
The following commands require initialize to be called first:

  initialize
      Reads and validates config.txt. Must be run before any other
      command. Can only be called once per session.

  clear
      Clears the terminal screen and reprints the CSOPESY header.

  scheduler-start
      Starts the scheduler background threads (tickLoop, schedulerLoop,
      one coreLoop per num-cpu). Enables automatic batch process
      spawning based on batch-process-freq.

  scheduler-stop
      Stops new process spawning and the CPU tick. Processes already
      in the queue continue running to completion in the background.
      The prompt is returned immediately (non-blocking).

  report-util
      Generates a system report showing CPU utilization, cores used,
      and the list of running and finished processes. Saves the output
      to csopesy-log.txt in the current directory.

  screen -ls
      Prints the same system report as report-util directly to the
      terminal. Does not save to file.

  screen -s <name>
      Creates a new process with the given name, assigns it to the
      scheduler, and enters the process screen for that process.
      Name must be unique. Starts the scheduler engine if not running.

  screen -r <name>
      Attaches to an existing process screen by name. Only works if
      the process exists and has not yet finished. Prints "not found"
      if the process is finished or does not exist.

----------------------------------------------------------------------
PROCESS SCREEN COMMANDS
----------------------------------------------------------------------

While inside a process screen (entered via screen -s or screen -r):

  process-smi
      Displays the current state of the process: name, ID, log output
      from PRINT instructions, current instruction line, total lines,
      variable values, and sleep status.

  exit
      Returns to the main root:\> prompt.

----------------------------------------------------------------------
OUTPUT FILES
----------------------------------------------------------------------

  outputs/<name>.txt
      Created immediately when a process is created. Appended with
      one line per PRINT instruction that executes on that process.
      Format: (<timestamp>) Core:<N> "<message>"

  csopesy-log.txt
      Written by report-util. Contains CPU utilization, core counts,
      and the running/finished process list at the time of the call.
