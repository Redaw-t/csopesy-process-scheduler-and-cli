# Command Interpreter Implementation

What each recognized command actually does inside the emulator.

---

## Slide 1: The Read-Eval Loop

```cpp
int Console::run() {
    printHeader();
    while (running_) {
        printPrompt();                          // root:\>
        if (!std::getline(in_, line)) break;    // end of input also exits
        ParsedCommand cmd = CommandParser::parse(line);
        if (cmd.empty()) continue;              // blank line
        dispatch(cmd);                          // find handler and run it
    }
    if (scheduler_) scheduler_->shutdown();     // stop and join all threads
    return 0;
}
```

- `Console` owns the `Scheduler`. Commands are interpreted by calling into it.
- The loop runs on the main thread; the CPU keeps running on its own threads in the background.

---

## Slide 2: Architecture

```text
            ┌────────────────────── Console (main thread) ──────────────────────┐
 user ────► │ parse ─► dispatch ─► cmdInitialize / cmdScreen / cmdScheduler... │
            └─────────┬─────────────────────┬───────────────────────┬───────────┘
                      │                     │                       │
               ConfigLoader            Scheduler                report::
             (reads config.txt)   (clock + core threads)   buildUtilizationReport
                                            │
                                        Process objects
```

| Command | Calls into |
|---------|-----------|
| `initialize` | `ConfigLoader`, then creates and starts the `Scheduler` |
| `screen -s <name>` | `Scheduler::createProcess()` → process screen |
| `screen -r <name>` | `Scheduler::findProcess()` → process screen |
| `screen -ls` | `Scheduler::snapshot()` → `report::buildUtilizationReport()` |
| `scheduler-start` / `-stop` | `Scheduler::startGenerating()` / `stopGenerating()` |
| `report-util` | same report text → `csopesy-log.txt` |
| `exit` | `Scheduler::shutdown()` |

---

## Slide 3: `initialize`

1. Finds `config.txt`, checking in order:
   1. the current working directory
   2. the folder containing the executable
   3. that folder's parent (the project folder for `build/` or `cmake-build-*/`)
2. Parses and validates every parameter (next slide).
3. Prints any warnings; on errors, prints them and stays uninitialized so the user can fix the
   file and try again.
4. On success: stores the config, creates the `Scheduler`, starts the clock and core threads,
   and unlocks the other commands.
5. Runs only once per session. Changing parameters means restarting the emulator, with no recompiling.

```text
root:\> initialize
Loaded configuration from C:\...\m01\config.txt
  num-cpu            4
  scheduler          rr
  quantum-cycles     5
  ...
Emulator initialized. CPU scheduler is running.
```

---

## Slide 4: Parsing `config.txt`

- File is read as whitespace-separated **key value** pairs; text after `#` is ignored.
- Quotes are stripped (`"rr"` → `rr`); keys are case-insensitive.
- Both `delay-per-exec` and `delays-per-exec` are accepted (the spec uses both spellings).

| Check | Example error |
|-------|---------------|
| Value must be a non-negative integer | `min-ins must be a non-negative integer (got "abc").` |
| Range check per parameter | `num-cpu must be in the range [1, 128] (got 0).` |
| Scheduler name | `scheduler must be "fcfs" or "rr" (got "sjf").` |
| `min-ins ≤ max-ins` | `min-ins (5) cannot be greater than max-ins (2).` |
| Key with no value | `Parameter "num-cpu" has no value.` |
| Missing key | *Warning*, default value is used |
| Unknown key | *Warning*, ignored |

- Values are stored as `uint64_t`, because several ranges go up to 2³² (one more than `uint32_t` can hold).

---

## Slide 5: `screen -s`, `screen -r`, `screen -ls`

**`screen -s <name>`**
- `Scheduler::createProcess(name)` creates a process with a random program and puts it in the
  ready queue. Returns `nullptr` if the name is taken → "already exists" message.
- The console then switches to that process's screen.

**`screen -r <name>`**
- `Scheduler::findProcess(name)` looks the name up in the process table.
- Not found, or already finished → `Process <name> not found.` Otherwise attach.

**`screen -ls`**
- Takes a `SchedulerSnapshot` (busy cores, running processes per core, finished list),
  formats it, and prints it.

---

## Slide 6: `scheduler-start`, `scheduler-stop`, `report-util`, `exit`

**`scheduler-start` / `scheduler-stop`**
- Turn batch process generation on or off. The scheduler itself keeps running either way,
  so already-created processes keep executing after `scheduler-stop`.
- Calling start twice, or stop when not running, gives a clear message instead of failing.

**`report-util`**
- Builds the same text as `screen -ls` and writes it to **`csopesy-log.txt`**, in the same folder
  as the loaded `config.txt` (overwritten each time).
- Prints the full path: `Report generated at C:\...\m01\csopesy-log.txt!`

**`exit`**
- Ends the loop, then `Scheduler::shutdown()` wakes the clock thread immediately, stops the
  core threads, and joins them all, so the program exits cleanly with no hanging threads.

---

## Slide 7: Process Screen Interpreter

```cpp
void Console::attachScreen(const std::shared_ptr<Process>& process) {
    clearScreen();
    printProcessSmi(*process);
    for (;;) {
        // read + parse a line
        if (cmd.name == "process-smi")  printProcessSmi(*process);
        else if (cmd.name == "exit")    { clearScreen(); printHeader(); return; }
        else if (cmd.name == "clear")   clearScreen();
        else                            /* "not recognized inside a process screen" */;
    }
}
```

- Holds a `shared_ptr` to the process, so the process stays valid even after it finishes
  while the screen is open.
- `process-smi` reads a **snapshot** of the process (state, counters) and a copy of its logs,
  never the live data directly.

---

## Slide 8: Summary

- A single read-eval loop on the main thread; all real work is delegated to `ConfigLoader`,
  `Scheduler`, `Process` and the report formatter.
- `initialize` validates the config thoroughly and only then starts the CPU.
- Commands talk to the scheduler through a small, thread-safe API
  (`createProcess`, `findProcess`, `snapshot`, `startGenerating`, `stopGenerating`, `shutdown`).
- Every command is covered by unit tests that run the console on scripted input.
