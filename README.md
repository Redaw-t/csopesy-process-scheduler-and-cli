# CSOPESY Emulator: Process Scheduler and CLI

A command-line OS emulator with a multi-core CPU scheduler (FCFS and Round Robin),
a `screen`-style process multiplexer, and CPU utilization reports.

## Group Members

- Jann Miro Quilantang
- VL Kirsten Camille Saguin
- Nathaniel Singh
- Nathan Trinidad

## Entry Point

**`src/main.cpp`** contains `main()`, which starts the console (`src/console/Console.cpp`).

---

## Quick Start

From a terminal in the project folder (the one containing `CMakeLists.txt` and `config.txt`):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\csopesy.exe
```

Then inside the emulator:

```text
root:\> initialize
root:\> scheduler-start
root:\> screen -ls
root:\> exit
```

> **Tip:** Run it from the project folder so `config.txt` is found and `csopesy-log.txt` is saved there.

---

## Requirements

| Tool  | Version | Notes |
|-------|---------|-------|
| g++   | C++17, POSIX threads | MSYS2 UCRT64 recommended. `g++ -v` should show `Thread model: posix`. |
| CMake | 3.16+   | Optional if you build with g++ directly (Option B below). |
| Ninja | any     | Optional, used by the CMake command line build. |

Install CMake and Ninja from the **MSYS2 UCRT64** terminal if you don't have them:

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

---

## Building and Running

### CLion

1. Open the project folder. CLion loads `CMakeLists.txt` automatically.
2. Pick the **`csopesy`** run configuration and press **Run**.

The executable is built into `cmake-build-debug\` (or `cmake-build-release\` for a Release profile).

### VS Code

1. Install the **C/C++** and **CMake Tools** extensions.
2. Open the project folder and select the **GCC (UCRT64)** kit when prompted.
3. Use **Build** and **Run** in the status bar.

### Command Line (Windows)

Works in Command Prompt, PowerShell, Windows Terminal, or the MSYS2 UCRT64 shell.

**1. Check that the tools are on your PATH**

```bat
g++ --version
cmake --version
```

If `g++` is not found, add `C:\msys64\ucrt64\bin` to your PATH
(Start → *Edit the system environment variables* → *Environment Variables* → *Path*), then open a new terminal.

**2. Go to the project folder**

```bat
cd "C:\path\to\m01"
```

**3. Build, using either option**

*Option A: CMake (also builds the tests)*

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Without Ninja, use `-G "MinGW Makefiles"` instead (requires `mingw32-make`).

*Option B: g++ only, no CMake*

```bat
g++ -std=c++17 -O2 -Isrc src/*.cpp src/config/*.cpp src/console/*.cpp src/process/*.cpp src/report/*.cpp src/scheduler/*.cpp src/util/*.cpp -static-libgcc -static-libstdc++ -pthread -o csopesy.exe
```

**4. Run**

| Built with | Command Prompt | PowerShell |
|------------|----------------|------------|
| Option A   | `build\csopesy.exe` | `.\build\csopesy.exe` |
| Option B   | `csopesy.exe`       | `.\csopesy.exe` |

To change parameters, exit the emulator, edit `config.txt`, and run the `.exe` again. **No rebuild is needed.**

### Command Line (Linux / macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/csopesy
```

> **Recording demos:** clearing the screen (`screen -s`, `exit`, `clear`) needs a real terminal.
> Use Windows Terminal / PowerShell / Command Prompt, or enable
> *Emulate terminal in the output console* in CLion's run configuration.

---

## Commands

### Main Menu

All commands except `initialize`, `help` and `exit` require `initialize` to be run first.

| Command | Description |
|---------|-------------|
| `initialize` | Load `config.txt` and start the CPU scheduler |
| `screen -s <name>` | Create a process and open its screen |
| `screen -r <name>` | Re-open the screen of a process that has not finished |
| `screen -ls` | Show CPU utilization, cores used/available, running and finished processes |
| `scheduler-start` | Start generating processes (`p01`, `p02`, …) |
| `scheduler-stop` | Stop generating processes |
| `report-util` | Save the `screen -ls` report to `csopesy-log.txt` (next to the loaded `config.txt`) |
| `clear` | Clear the screen |
| `help` | List commands |
| `exit` | Quit the emulator |

### Inside a Process Screen

| Command | Description |
|---------|-------------|
| `process-smi` | Show process name, ID, PRINT logs and progress, or `Finished!` |
| `exit` | Return to the main menu |

---

## Configuration (`config.txt`)

Space-separated, one parameter per line:

```text
num-cpu 4
scheduler "rr"
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delay-per-exec 0
```

| Parameter | Range | Description |
|-----------|-------|-------------|
| `num-cpu` | 1 – 128 | Number of CPU cores |
| `scheduler` | `"fcfs"` or `"rr"` | Scheduling algorithm |
| `quantum-cycles` | 1 – 2³² | Round Robin time slice, in CPU ticks |
| `batch-process-freq` | 1 – 2³² | A new process is generated every N ticks during `scheduler-start` |
| `min-ins` | 1 – 2³² | Minimum instructions per process |
| `max-ins` | 1 – 2³² | Maximum instructions per process |
| `delay-per-exec` | 0 – 2³² | Busy-wait ticks after each instruction (`delays-per-exec` also accepted) |
| `tick-ms` | 0 – 1000 | *Optional, default 10.* Real milliseconds per CPU tick (0 = as fast as possible) |

`initialize` looks for `config.txt` in this order:

1. the current working directory
2. the folder containing the executable
3. the parent of that folder (the project folder when building into `build/` or `cmake-build-*/`)

---

## How It Works

After `initialize`, a **clock thread** advances the CPU tick counter every `tick-ms` milliseconds,
and **each CPU core runs on its own thread**. On every tick:

1. Processes whose `SLEEP` has ended return to the ready queue.
2. Free cores take the next processes from the ready queue (first come, first served).
3. Each busy core executes one instruction, or busy-waits for `delay-per-exec` ticks after one.
4. **RR:** a process that has used `quantum-cycles` ticks goes to the back of the ready queue
   (if another process is waiting). **FCFS:** it keeps the core until it finishes or sleeps.
5. While `scheduler-start` is active, a new process is created every `batch-process-freq` ticks.
6. Any cores freed during the tick are refilled immediately.

### Process Instructions

| Instruction | Effect |
|-------------|--------|
| `PRINT(msg)` | Adds a log line (default: `Hello world from <name>!`) |
| `DECLARE(var, value)` | Creates a uint16 variable |
| `ADD(var1, var2/value, var3/value)` | `var1 = var2 + var3`, clamped to 0 – 65535 |
| `SUBTRACT(var1, var2/value, var3/value)` | `var1 = var2 - var3`, clamped to 0 – 65535 |
| `SLEEP(X)` | Gives up the CPU for X ticks |
| `FOR([instructions], repeats)` | Repeats a block; nested up to 3 levels |

Undeclared variables are automatically created with a value of 0.
"Lines of code" counts the instructions a process actually executes, with `FOR` loops unrolled.

### Memory Use

A process's instructions are generated from a random seed the first time it gets a core,
and released whenever it leaves the core (sleep, preemption, or finish). When it returns,
the same program is rebuilt from the seed and execution continues exactly where it stopped,
so thousands of waiting processes use very little memory.

---

## Project Structure

```text
m01/
├── CMakeLists.txt
├── config.txt
├── src/
│   ├── main.cpp                  Entry point
│   ├── config/                   config.txt parsing and validation
│   ├── console/                  Main menu, screen sessions, command parsing, header
│   ├── process/                  Process, instructions, random instruction generator
│   ├── scheduler/                CPU clock, cores, FCFS / RR scheduling
│   ├── report/                   screen -ls / report-util output
│   └── util/                     Time and path helpers
└── tests/
    └── test_main.cpp             Unit tests
```

---

## Tests

Build and run the `csopesy_tests` target, or from the command line (Option A):

```bat
build\csopesy_tests.exe
```

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `libwinpthread-1.dll was not found` | Add `C:\msys64\ucrt64\bin` to PATH, or copy `libwinpthread-1.dll` from there next to `csopesy.exe`. |
| `Could not find config.txt` | You're not in the project folder. `cd` there first. |
| `CMAKE_MAKE_PROGRAM is not set` | The `-G` generator isn't installed. Install Ninja (see Requirements) or use Option B. |
| Build errors after switching generators | Delete the `build` folder and configure again. |
| Screen doesn't clear in CLion | Enable *Emulate terminal in the output console*, or run from a real terminal. |
