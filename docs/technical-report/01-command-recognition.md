# Command Recognition

How the emulator turns a line of typed text into a command it knows.

---

## Slide 1: Overview

- Every line the user types goes through the same pipeline:

```text
keyboard ──► std::getline ──► CommandParser ──► command table lookup ──► handler
                               (tokenize)        (Console::dispatch)
```

- Two recognition contexts:
  - **Main menu**: `initialize`, `screen`, `scheduler-start`, `scheduler-stop`, `report-util`, `clear`, `help`, `exit`
  - **Process screen**: `process-smi`, `exit`, `clear`
- Source: `src/console/CommandParser.{h,cpp}`, `src/console/Console.cpp`

**Speaker notes:** Recognition is split into two steps. First the raw text is broken into
tokens, then the first token is looked up in a table of known commands. The same parser is
used in both the main menu and the process screen, but each context recognizes its own set of commands.

---

## Slide 2: Tokenizing the Input

`CommandParser::tokenize()` scans the line one character at a time:

| Rule | Example input | Tokens |
|------|---------------|--------|
| Whitespace separates tokens | `screen   -s   p01` | `screen`, `-s`, `p01` |
| Leading/trailing spaces ignored | `   exit   ` | `exit` |
| Double quotes group words | `screen -s "my proc"` | `screen`, `-s`, `my proc` |
| Blank line | `     ` | *(nothing, prompt shown again)* |

`CommandParser::parse()` then builds a `ParsedCommand`:

```cpp
struct ParsedCommand {
    std::string name;               // first token, lower-cased: "screen"
    std::vector<std::string> args;  // remaining tokens, case preserved: {"-s", "p01"}
};
```

**Speaker notes:** The command name is lower-cased so `SCREEN` and `screen` both work, but
arguments keep their case, because process names like `P01` and `p01` are different processes.

---

## Slide 3: The Command Table

Commands are registered once, in `Console::registerCommands()`, in a lookup table:

```cpp
struct CommandInfo {
    Handler handler;          // function that runs the command
    bool requiresInitialize;  // blocked until "initialize" has run?
    std::string usage;        // shown when arguments are wrong
};
std::map<std::string, CommandInfo> commands_;
```

| Command | Requires `initialize` |
|---------|:---------------------:|
| `initialize` | No |
| `exit` | No |
| `help` | No |
| `screen` | Yes |
| `scheduler-start` | Yes |
| `scheduler-stop` | Yes |
| `report-util` | Yes |
| `clear` / `cls` | Yes |

**Speaker notes:** Using a table instead of a long if/else chain means adding a command is one
line of registration plus a handler function. The `requiresInitialize` flag is how the spec rule
"no other command is recognized before initialize" is enforced in a single place.

---

## Slide 4: Dispatch and the `initialize` Gate

`Console::dispatch()` decides what happens to every parsed command:

```text
                 ┌─────────────────────────┐
 ParsedCommand ─►│ name in command table?  │── no ──► "'foo' is not recognized as a command.
                 └───────────┬─────────────┘           Type "help" to see available commands."
                             │ yes
                 ┌───────────▼─────────────┐
                 │ requiresInitialize AND  │── yes ─► "Please run "initialize" first."
                 │ not yet initialized?    │
                 └───────────┬─────────────┘
                             │ no
                             ▼
                     handler(args)
```

Example session:

```text
root:\> screen -ls
Please run "initialize" first.
root:\> foo
'foo' is not recognized as a command. Type "help" to see available commands.
```

---

## Slide 5: Sub-command and Argument Validation

`screen` has three forms, so its handler checks the flag and argument count:

| Input | Recognized as |
|-------|---------------|
| `screen -s <name>` | Create a process and attach |
| `screen -r <name>` | Re-attach to an existing process |
| `screen -ls` | List CPU utilization and processes |
| `screen`, `screen -x`, `screen -s` (no name), `screen -ls extra` | Invalid → usage message |

```text
root:\> screen -x
Usage: screen -s <process name> | screen -r <process name> | screen -ls
```

Other commands that take no arguments (`initialize`, `report-util`) also print their usage if
extra arguments are given.

---

## Slide 6: Recognition Inside a Process Screen

- After `screen -s` / `screen -r`, input goes to a separate loop in `Console::attachScreen()`.
- It uses the same parser, but only recognizes screen commands:

| Command | Action |
|---------|--------|
| `process-smi` | Print process details and logs |
| `exit` | Return to the main menu |
| `clear` / `cls` | Clear the screen |
| anything else | Error message listing the valid commands |

```text
root:\> foo
'foo' is not recognized inside a process screen. Available commands: process-smi, exit
```

- Main-menu commands such as `scheduler-start` are **not** recognized here, matching the
  spec ("these commands are only accessible in the main menu console").

---

## Slide 7: Summary

- **Tokenizer:** whitespace split, quote grouping, lower-cased command name
- **Command table:** `std::map` of name → handler, init requirement, usage text
- **Single gate:** the `requiresInitialize` flag blocks commands until `initialize` runs
- **Clear feedback:** unknown commands, missing initialization and bad arguments each get
  a specific message
- **Context-aware:** the main menu and process screens recognize different command sets
- **Testable:** the console reads from any `std::istream`, so unit tests feed it scripted input
