# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL Studio manual

RBL Studio is a Python/Tkinter desktop editor integrated with the RBL project driver. The IDE is optional; the compiler and CLI can be used without opening the GUI.

## Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `F5` | Build and run |
| `Ctrl+B` | Build/preview assembly |
| `Ctrl+Shift+B` | Check source |
| `Ctrl+T` | Run tests |
| `Ctrl+Shift+T` | Run benchmarks |

## Editor and output

The editor provides tabs, line numbers, syntax highlighting, indentation assistance, undo/redo and scrolling. The lower panel separates program output, tool output and diagnostics. The exact set of visible panels may depend on the current Studio build.

## Build and run

`F5` saves the selected source, invokes the project driver, builds the native executable and runs it. `Ctrl+B` exposes the generated assembly for inspection. `Ctrl+Shift+B` runs the compiler check path; it does not link an executable.

## Themes and settings

The current theme set includes Dark, Light, Nord and Monokai. Settings are stored in the per-user RBL Studio configuration directory, not in the project source tree.

## Windows toolchain

On Windows, Studio prefers a detected native MinGW-w64 toolchain in `auto` mode and can use WSL as a fallback. The `RBL_TOOLCHAIN` environment variable overrides the saved toolchain setting for the current process. See `WINDOWS.md` for the selection rules.

## Project CLI

The GUI is not required for project work. The command names below show the CLI interface; this repository does not install an executable global launcher. From a checkout, run `python3 rbl.py <command>` on Linux or `python rbl.py <command>` on Windows, and use the path to that `rbl.py` when working in a separate project directory.

```text
rbl new hello
rbl check
rbl build
rbl run
rbl test
rbl bench
rbl bench3way --reps 10 --warmups 2
rbl diff --a wsl --b native
rbl fmt
rbl doc
rbl clean
rbl doctor
```

## Diagnostics

For startup problems, run the platform launcher from a terminal and inspect `ide_startup.log`. On Linux/WSL, use:

```bash
python3 tools/ide_doctor.py
python3 tools/ide_smoke.py
bash RBLStudio.sh --debug
```

On Windows, launch `RBLStudio.cmd` from PowerShell and inspect the startup log. See `GUI_TROUBLESHOOTING.md` for focused checks.
