# Contributing

Guidelines for working on the CSOPESY emulator together. See [README.md](README.md) for
building, running and configuration.

## Getting Set Up

1. Install **MSYS2 UCRT64 g++** (or use CLion's bundled MinGW) and confirm `g++ -v` shows
   `Thread model: posix`.
2. Clone or copy the project and open the folder in **CLion** or **VS Code** (with CMake Tools).
3. Build the `csopesy` and `csopesy_tests` targets.
4. Run `csopesy_tests` and make sure every check passes before you change anything.

---

## Workflow

### Branches

- `main` should always build and pass the tests.
- Do your work on a separate branch named after the change, e.g. `fix/rr-preemption` or
  `feature/process-smi-format`.
- Keep each branch focused on one change, so it's easy to review.

### Commits

- Write the message as an instruction, saying what the change does:
  `Fix utilization report when cores are idle`, not `fixed stuff`.
- Keep commits small, and only commit code that builds.
- Don't commit build output: `build/`, `cmake-build-*/`, `.idea/`, `.vscode/`, `*.exe` and
  `csopesy-log.txt` are already in `.gitignore`.

### Before Merging

- [ ] Project builds with **no warnings** (`-Wall -Wextra -Wpedantic` is on)
- [ ] `csopesy_tests` passes
- [ ] You ran the emulator and tried the commands you touched
- [ ] Another member has looked over the change
- [ ] README.md is updated if commands, config parameters or behavior changed

---

## Project Layout

| Folder | What lives there |
|--------|------------------|
| `src/console/` | Main menu, command dispatch, `screen` sessions, header/banner (`AppInfo.h`) |
| `src/config/` | Reading and validating `config.txt` |
| `src/process/` | `Process`, the instruction set, the random instruction generator |
| `src/scheduler/` | CPU clock, core threads, ready/sleeping queues, FCFS and RR |
| `src/report/` | Text for `screen -ls` and `report-util` |
| `src/util/` | Time formatting and executable-path helpers |
| `tests/` | Unit tests (`test_main.cpp`) |

When you add a `.cpp` file, also add it to the `emulator_core` list in `CMakeLists.txt`,
and to the g++ command in README.md if it's in a new folder. Then **reload CMake** in your IDE.

---

## Code Style

Follow the style of the surrounding code. In short:

| Item | Convention | Example |
|------|-----------|---------|
| Classes, structs, enums | `PascalCase` | `Scheduler`, `ProcessSnapshot` |
| Functions and methods | `camelCase` | `executeNext()`, `findProcess()` |
| Member variables | `camelCase` with trailing `_` | `ready_`, `config_` |
| Constants | `k` prefix | `kMaxLoopNesting`, `kPrompt` |
| Namespaces | lowercase | `timeutil`, `report` |
| Files | named after the main class | `Scheduler.h` / `Scheduler.cpp` |

- C++17 only, standard library only (no external dependencies).
- 4-space indentation, braces on the same line, keep lines under ~120 characters.
- Headers use `#pragma once` and include what they use.
- Comments explain **why** or describe behavior, not what the next line obviously does.
- Fixed-width integers (`uint16_t`, `uint64_t`) for anything tied to the spec's ranges.
  Config values can be as large as 2³², so use `uint64_t` for them.

---

## Threading Rules

The emulator runs a clock thread, one thread per CPU core, and the console thread at the
same time. To keep it free of data races:

- **`Scheduler` state** (ready queue, sleeping list, cores, process table) is guarded by
  `Scheduler::mutex_`. Functions ending in **`Locked`** expect the caller to already hold it.
- **`Process`** guards its own state with its own mutex. Every public method is safe to call
  from any thread.
- The console should only read scheduler data through **`snapshot()`**, `findProcess()` and
  `Process` accessors. Never reach into scheduler internals from the console.
- Don't call a `Scheduler` method while holding a `Process` lock, so a lock-order deadlock
  can't happen.
- Don't print from scheduler or core threads. PRINT instructions go into the process log,
  and the console decides when to show it.

---

## Common Changes

### Adding a main-menu command

1. Add a handler `void cmdYourCommand(const std::vector<std::string>& args);` in `Console.h`.
2. Register it in `Console::registerCommands()`. The second field says whether it requires
   `initialize` first.
3. Add it to `cmdHelp()` and to the command table in README.md.

### Adding a config parameter

1. Add the field and its default to `Config` in `src/config/Config.h`.
2. In `Config.cpp`: add the key to `keyAliases()`, its range check, and where it's stored.
   Add it to `canonicalKeys()` only if it's **required**; a missing required key produces a warning.
3. Add a parsing test in `testConfigParsing()` and a row in README.md.

### Adding an instruction type

1. Add the opcode to `OpCode` and a factory function in `src/process/Instruction.h`.
2. Handle it in `Instruction::toString()` and in `Process::execute()`.
3. Decide whether the random generator should emit it (`ProgramBuilder` in
   `InstructionGenerator.cpp`). Keep generation deterministic for a given seed, because
   programs are rebuilt from their seed after preemption.

---

## Testing

Tests live in `tests/test_main.cpp` and use a tiny built-in `check(condition, "description")` helper.

- Add a `testSomething()` function and call it from `main()`.
- For scheduler behavior, build a `Scheduler` **without** calling `start()` and drive it with
  `tickOnce()`. That makes every tick deterministic, so the test can check exact states.
- Use `tick-ms 0` in test configs so threaded tests run quickly.
- For threaded changes, also try a stress run, e.g. `num-cpu 128`, `batch-process-freq 1`,
  `quantum-cycles 1`, and watch that `screen -ls` stays consistent.

---

## Before Submitting or Recording

- [ ] Clean Release build with no warnings
- [ ] All tests pass
- [ ] `config.txt` holds the default parameters from the spec
- [ ] Banner names and "Last updated" date are correct (`src/console/AppInfo.h`)
- [ ] A `README.txt` is included (the spec asks for that exact file name)
- [ ] Demo recorded from a real terminal so the screen clears properly
