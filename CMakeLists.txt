cmake_minimum_required(VERSION 3.16)
project(csopesy_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# Core emulator sources, shared by the emulator and the test runner.
add_library(emulator_core STATIC
    src/config/Config.cpp
    src/console/CommandParser.cpp
    src/console/Console.cpp
    src/process/Instruction.cpp
    src/process/InstructionGenerator.cpp
    src/process/Process.cpp
    src/report/UtilizationReport.cpp
    src/scheduler/Scheduler.cpp
    src/util/TimeUtil.cpp
    src/util/PathUtil.cpp
)
target_include_directories(emulator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(emulator_core PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(emulator_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

# On MinGW, bundle the C++ runtime into the executable and copy the thread
# library DLL next to it, so the .exe also runs outside the IDE / MSYS2 shell.
if(MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(MINGW_WINPTHREAD_DLL libwinpthread-1.dll
              HINTS "${MINGW_BIN_DIR}" NO_DEFAULT_PATH)
endif()

function(configure_mingw_runtime target)
    if(MINGW)
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
        if(MINGW_WINPTHREAD_DLL)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_WINPTHREAD_DLL}" "$<TARGET_FILE_DIR:${target}>")
        endif()
    endif()
endfunction()

# Main executable. Entry point: src/main.cpp
add_executable(csopesy src/main.cpp)
target_link_libraries(csopesy PRIVATE emulator_core)
configure_mingw_runtime(csopesy)

# Unit tests for the configuration and process model.
add_executable(csopesy_tests tests/test_main.cpp)
target_link_libraries(csopesy_tests PRIVATE emulator_core)
configure_mingw_runtime(csopesy_tests)

enable_testing()
add_test(NAME csopesy_tests COMMAND csopesy_tests)
