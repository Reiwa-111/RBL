#!/usr/bin/env python3
"""Minimal WSLg/X11 GUI probe for RBL Studio."""
from __future__ import annotations
import os
import sys
import tkinter as tk

r = tk.Tk()
r.title("RBL Studio GUI Probe")
r.geometry("560x240+80+80")
r.configure(bg="#17151f")
msg = tk.Label(
    r,
    text=(
        "RBL Studio GUI probe\n\n"
        "If you can see this window, Tk + WSLg are working.\n"
        f"DISPLAY={os.environ.get('DISPLAY', '<unset>')}"
    ),
    bg="#17151f",
    fg="#e9e4f2",
    font=("DejaVu Sans", 13),
    justify="center",
)
msg.pack(fill="both", expand=True)
r.after(200, lambda: (r.lift(), r.focus_force(), r.attributes("-topmost", True)))
r.after(1200, lambda: r.attributes("-topmost", False))
r.mainloop()
