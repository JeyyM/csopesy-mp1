cmake_minimum_required(VERSION 3.16)
project(CSOPESY_OS_MP CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(csopesy_os_mp
    src/main.cpp
    src/Config.cpp
    src/ConsoleManager.cpp
    src/Process.cpp
    src/ProcessManager.cpp
    src/ReportManager.cpp
    src/MockProcessData.cpp
    src/Scheduler.cpp
    src/TimeUtil.cpp
)

target_include_directories(csopesy_os_mp PRIVATE src)

# Avoid MinGW DLL conflicts when two toolchains are on PATH (Windows)
if(MINGW)
  target_link_options(csopesy_os_mp PRIVATE -static)
endif()
