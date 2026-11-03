# Console UI Implementation

What the user sees, and how the console draws it.

---

## Slide 1: Overview

- Text-based UI modeled on a Linux / Windows shell and the Linux `screen` command
- Two views:
  - **Main menu**: header, `root:\>` prompt, system commands
  - **Process screen**: one process's details, opened with `screen -s` / `screen -r`
- Only the console thread writes to the screen. Scheduler and CPU core threads never
  print, so output is never interleaved.
- Source: `src/console/Console.{h,cpp}`, `src/console/AppInfo.h`, `src/report/UtilizationReport.cpp`

---

## Slide 2: The Header

Shown at startup, after `clear`, and when leaving a process screen:

```text
  ____ ____   ___  ____  _____ ______   __
 / ___/ ___| / _ \|  _ \| ____/ ___\ \ / /
| |   \___ \| | | | |_) |  _| \___ \\ V /
| |___ ___) | |_| |  __/| |___ ___) || |
 \____|____/ \___/|_|   |_____|____/ |_|
------------------------------------------
Welcome to CSOPESY Emulator!

Developers:
  Jann Miro Quilantang
  VL Kirsten Camille Saguin
  Nathaniel Singh
  Nathan Trinidad

Last updated: 10-29-2026
------------------------------------------
Type "initialize" to load config.txt, or "exit" to quit.

root:\>
```

- Banner, developer names and date are kept in one file, `AppInfo.h`, so they can be
  changed without touching console logic.
- The `initialize` hint only appears until the emulator has been initialized.

---

## Slide 3: Prompt and Feedback Messages

- Prompt `root:\>` is printed and flushed before every read.
- Every command gives visible feedback, including errors:

| Situation | Message |
|-----------|---------|
| Before `initialize` | `Please run "initialize" first.` |
| Unknown command | `'foo' is not recognized as a command. Type "help" to see available commands.` |
| Bad `screen` usage | `Usage: screen -s <process name> \| screen -r <process name> \| screen -ls` |
| Unknown / finished process | `Process p999 not found.` |
| Duplicate name | `Process demo already exists. Use "screen -r demo" to attach to it.` |
| `scheduler-start` | `Scheduler started: generating a new process every 10 CPU tick(s).` |
| `report-util` | `Report generated at C:\...\csopesy-log.txt!` |
| Bad config | `Error: num-cpu must be in the range [1, 128] (got 0).` |

---

## Slide 4: Switching Screens (the `screen` Multiplexer)

```text
   MAIN MENU                                      PROCESS SCREEN
 ┌──────────────┐   screen -s name / -r name   ┌──────────────────┐
 │ header       │ ───────────────────────────► │ clear screen     │
 │ root:\>      │                              │ process details  │
 │              │ ◄─────────────────────────── │ root:\>          │
 └──────────────┘            exit              └──────────────────┘
          clear screen + reprint header
```

- **Entering:** the console clears the screen and prints the process details right away.
- **Inside:** a separate input loop (`attachScreen`) handles `process-smi` and `exit`.
- **Leaving:** `exit` clears the screen and redraws the main-menu header.
- The process keeps running in the background the whole time; the screen is only a view of it.

**Speaker notes:** This mirrors Linux `screen`: detaching doesn't stop the program, and you
can re-attach later with `screen -r` as long as the process hasn't finished.

---

## Slide 5: `process-smi` View

While running:

```text
Process name: screen_01
ID: 1
Logs:
(11/03/2026 07:37:03PM) Core:0 "Hello world from screen_01!"
(11/03/2026 07:37:03PM) Core:0 "Hello world from screen_01!"
(11/03/2026 07:37:03PM) Core:0 "Hello world from screen_01!"

Current instruction line: 11
Lines of code: 32
```

After finishing:

```text
Process name: screen_01
ID: 1
Logs:
(11/03/2026 07:37:03PM) Core:0 "Hello world from screen_01!"
...

Finished!
```

- Each log line shows the timestamp, the core that ran the PRINT, and the message.
- Details are read from a thread-safe snapshot, so the view is always consistent
  even while a core is executing the process.

---

## Slide 6: `screen -ls` / `report-util` View

```text
CPU utilization: 100%
Cores used: 4
Cores available: 0

--------------------------------------
Running processes:
p36       (11/03/2026 07:36:56PM)    Core: 0     5 / 74
p26       (11/03/2026 07:36:55PM)    Core: 1     27 / 48
p23       (11/03/2026 07:36:55PM)    Core: 2     40 / 79
p16       (11/03/2026 07:36:54PM)    Core: 3     55 / 64

Finished processes:
p05       (11/03/2026 07:36:54PM)    Finished    46 / 46
p01       (11/03/2026 07:36:54PM)    Finished    68 / 68
p03       (11/03/2026 07:36:55PM)    Finished    54 / 54
--------------------------------------
```

- One function, `report::buildUtilizationReport()`, produces this text.
  `screen -ls` prints it; `report-util` writes the exact same text to `csopesy-log.txt`.
- Column widths are computed from the longest process name, so long names stay aligned.
- Running rows show the creation time; finished rows show the completion time.

---

## Slide 7: Clearing the Screen

```cpp
void Console::clearScreen() {
#ifdef _WIN32
    std::system("cls");                      // Windows console
#else
    out_ << "\033[2J\033[H" << std::flush;   // ANSI: clear + cursor home
#endif
}
```

- Used when entering/leaving process screens and for the `clear` command.
- Works in Command Prompt, PowerShell and Windows Terminal. (IDE output panes like
  CLion's need terminal emulation enabled.)

---

## Slide 8: Design Choices

- **Input/output streams are injected:** `Console(std::istream&, std::ostream&)`.
  The real program passes `std::cin` / `std::cout`; tests pass string streams and check the text.
- **One writer:** only the console prints, so background threads can't corrupt the display.
- **Shared formatting:** `screen -ls` and `report-util` can never disagree, because they use
  the same report function.
- **Configurable identity:** header content lives in `AppInfo.h`.
