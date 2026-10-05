#!/usr/bin/env python3
"""Non-interactive Tk smoke test: creates the real IDE, pumps Tk once, then exits."""
from __future__ import annotations
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import tkinter as tk
from ide.rbl_studio import RBLStudio

try:
    root = tk.Tk(className="RBLStudioSmoke")
except tk.TclError as exc:
    if "couldn't connect to display" in str(exc).lower():
        print(f"RBL Studio Tk smoke test: SKIPPED (no GUI display: {exc})")
        raise SystemExit(0)
    raise
root.withdraw()
RBLStudio(root)
root.update_idletasks()
root.update()
root.destroy()
print("RBL Studio Tk smoke test: PASS")
