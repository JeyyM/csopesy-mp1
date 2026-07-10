cmake_minimum_required(VERSION 3.16)
project(CSOPESY_OS_MP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(csopesy_os_mp
    src/main.cpp
    src/Config.cpp
    src/ConsoleManager.cpp
    src/ProcessModel.cpp
    src/ReportManager.cpp
    src/OutputManager.cpp
    src/ScreenManager.cpp
    src/Scheduler.cpp
    src/InstructionEngine.cpp
    src/MemoryManager.cpp
    src/TimeUtil.cpp
)

target_include_directories(csopesy_os_mp PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(csopesy_os_mp PRIVATE Threads::Threads)

# Avoid MinGW DLL conflicts when two toolchains are on PATH (Windows)
if(MINGW)
  target_link_options(csopesy_os_mp PRIVATE -static)
endif()
