# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL Studio GUI troubleshooting

## Normal launch

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

## Check the environment

```bash
python3 tools/ide_doctor.py
python3 tools/ide_smoke.py
```

The first command checks the Python/Tk and toolchain environment. The smoke test checks that the Tk interface can be initialized; it does not validate every IDE workflow.

## Debug launch

On Linux/WSL:

```bash
bash RBLStudio.sh --debug
```

Inspect `ide_startup.log` in the project directory for the last completed startup step and any traceback.

## Isolate display problems

`tools/gui_probe.py` is a minimal display probe. If the probe cannot open a window either, investigate the host display/Tk/WSLg setup before debugging RBL Studio widgets.

The safe mode opens a reduced Tk window and helps distinguish display initialization failures from full IDE widget construction failures:

```bash
bash RBLStudio.sh --safe
```
