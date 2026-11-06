# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: RBL Studio IDE

RBL Studio is an optional Python/Tkinter graphical editor that uses the same project/toolchain driver as the CLI. The IDE is not required to compile the language.

## Launch

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

## Main keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `F5` | Build and run the selected file |
| `Ctrl+B` | Build / assembly preview |
| `Ctrl+Shift+B` | Check source (`check`) |
| `Ctrl+T` | Run tests |
| `Ctrl+Shift+T` | Run benchmarks |

Panels and visible UI elements may change between Studio builds. `F5` uses the configured toolchain backend; `Ctrl+Shift+B` does not link an executable.

## Diagnose startup

```bash
python3 tools/ide_doctor.py
python3 tools/ide_smoke.py
```

For a debug launch on Linux/WSL:

```bash
bash RBLStudio.sh --debug
```

To isolate display problems, use `tools/gui_probe.py` and the reduced safe mode:

```bash
bash RBLStudio.sh --safe
```

Inspect `ide_startup.log` in the checkout root. It records the last startup stage and a traceback if startup failed.
