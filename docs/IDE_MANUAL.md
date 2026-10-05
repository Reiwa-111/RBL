# RBL Studio manual

## Basic keys

| Key | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open `.rbl` |
| `Ctrl+S` | Save |
| `F5` | Build + run |
| `Ctrl+B` | Build direct ASM |
| `Ctrl+Shift+B` | Syntax check |
| `Ctrl+T` | Tests |
| `Ctrl+Shift+T` | Benchmarks |

## Editor

The editor deliberately feels closer to Python IDLE than to a heavy web IDE: monospaced source view, line numbers, syntax colouring, indentation helper, undo/redo, horizontal/vertical scroll and a bottom output pane.

## Output pane

- `Run`: program stdout/stderr and process exit code.
- `Console`: reserved for tool output / future interactive facilities.
- `Problems`: syntax diagnostics and build errors.

## Themes

Dark, Light, Nord and Monokai are included. Settings persist outside the project under the normal per-user application configuration directory.

## Windows toolchain

On Windows the IDE builds and runs natively: it looks for a MinGW-w64 GNU toolchain (`gcc.exe` + `as.exe`, normally from MSYS2) and produces a real `.exe`. WSL is only used when no native toolchain is found or when it is selected explicitly.

The `Windows toolchain` setting selects the mode:

| Value | Meaning |
|---|---|
| `auto` (default) | native Windows toolchain if found, otherwise WSL |
| `native` | Windows toolchain only |
| `wsl` | WSL only (`always` / `never` are still accepted as aliases) |

`Help -> Toolchain Doctor` shows the detected toolchain, the active backend and the WSL status. The `RBL_TOOLCHAIN` environment variable overrides the setting for one session. See `docs/WINDOWS.md`.

## Project CLI

The editor is optional for the normal toolchain. A project can be worked entirely from the `rbl` command:

```text
rbl new hello
rbl check
rbl build
rbl run
rbl test
rbl bench        # portable build/run/net benchmarks
rbl bench3way    # RBL vs CPython vs hand-written ASM (Linux host only)
rbl diff         # compare two backends byte for byte
rbl fmt
rbl doc
rbl clean
rbl doctor
```

## If the IDE does not open

From the RBL Studio directory:

```bash
python3 tools/ide_doctor.py
./RBLStudio.sh
```

The launcher prefers the project's `.venv/bin/python3` when present and falls back to `python3`. It checks Tkinter first. A startup traceback is stored in `ide_startup.log`.

On Windows, run `RBLStudio.cmd` from PowerShell first; it uses the project's `.venv\Scripts\python.exe` when available and writes startup diagnostics to `ide_startup.log`.

## Linux / WSLg startup diagnostics

The launcher keeps the GUI process in the foreground when run from a shell; this is normal because Tk is running its event loop. Do not use Ctrl+Z to test whether the IDE started. Use a second terminal if you want to keep the shell free.

For verbose startup diagnostics:

```bash
./RBLStudio.sh --debug
```

This records startup steps in `ide_startup.log` and raises the initial window above other windows briefly so WSLg window mapping is visible.

For a non-interactive GUI/Tk check:

```bash
python3 tools/ide_smoke.py
```

Expected output:

```text
RBL Studio Tk smoke test: PASS
```
