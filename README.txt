CSOPESY Command Line Emulator
=============================

Student Name: S09 - Group 9
Group Members:
- Miranda, Juan Miguel
- Alcantara, Van Asher
- Rojo, Von Matthew
- Capote, Mary Grace
Entry Class File: src/main.cpp

INSTRUCTIONS ON HOW TO RUN THE PROGRAM:
----------------------------------------
1. Building and Running the Project:
     cd "c:\filepath\CSOPESY OS MP"
     cmake --build build
     .\build\csopesy_os_mp.exe

2. Using the Program:
   - The program will display a CSOPESY ASCII header and welcome message
   - Before initialization, only initialize and exit are accepted
   - You can enter the following commands:
   
   AVAILABLE COMMANDS:
   -------------------
   - initialize      : Load config.txt, validate configuration, and initialize the shell.
   - clear           : Clear the screen and reprint the header
   - exit            : Exit the program
   - scheduler-start : Routed to the scheduler module after initialization
   - scheduler-stop  : Routed to the scheduler module after initialization
   - report-util     : Routed to the report module after initialization
   - screen -ls      : Routed to the screen/process module after initialization
   - screen -s <name>: Routed to the screen/process module after initialization
   - screen -r <name>: Routed to the screen/process module after initialization

PERSON 1 OWNERSHIP:
-------------------
Person 1 owns the main shell loop, trimmed command input, initialization gate,
config.txt loading/validation, top-level command routing, and shared root:\>
prompt behavior.

Person 1 does not own process list formatting, the process screen loop,
scheduler process list output, or process-smi content.

SOURCE CODE LOCATION:
---------------------
GitHub Repository: https://github.com/JeyyM/csopesy-mp1
