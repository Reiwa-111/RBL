#!/usr/bin/env python3
from __future__ import annotations
import importlib.util
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
print('RBL Studio IDE doctor')
print(f'Root: {ROOT}')
print(f'Python: {sys.version.split()[0]}')
print(f'tkinter: {"OK" if importlib.util.find_spec("tkinter") else "MISSING"}')
print(f'DISPLAY: {os.environ.get("DISPLAY", "<unset>")}')
print(f'WAYLAND_DISPLAY: {os.environ.get("WAYLAND_DISPLAY", "<unset>")}')
print(f'WSLg: {"yes" if os.environ.get("WAYLAND_DISPLAY") else "unknown"}')
for cmd in ('as','ld'):
    print(f'{cmd}: {shutil.which(cmd) or "MISSING"}')
print(f'RBL compiler: {ROOT / "bin" / "linux-x86_64" / "rblc-asm"}')
print('Import test:', end=' ')
try:
    from ide.rbl_studio import RBLStudio  # noqa: F401
    print('OK')
except Exception as exc:
    print(f'FAIL: {exc}')
    raise

print('Tk smoke test:', end=' ')
try:
    import subprocess
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'ide_smoke.py')], check=True, text=True, capture_output=True)
    print('OK')
except Exception as exc:
    print(f'FAIL: {exc}')
