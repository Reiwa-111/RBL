# RBL Studio GUI troubleshooting

## Normal start

```bash
./RBLStudio.sh
```

## Debug start

```bash
./RBLStudio.sh --debug
```

Inspect `ide_startup.log` if the window does not appear. A healthy startup ends with:

```text
Tk event loop alive
RBL Studio UI initialized
window mapped: state=normal viewable=1 mapped=1 geometry=...
```

## Safe mode

Safe mode constructs only a tiny Tk window:

```bash
./RBLStudio.sh --safe
```

If safe mode is visible but normal mode is not, the WSLg/Tk stack is fine and the issue is inside IDE widget construction.

## Minimal probe

```bash
python3 tools/gui_probe.py
```

If this window is invisible too, the issue is outside RBL Studio and is specifically in the WSLg/Tk display path.
