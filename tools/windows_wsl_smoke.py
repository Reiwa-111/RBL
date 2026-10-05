#!/usr/bin/env python3
"""Smoke-test the Windows GUI -> WSL RBL backend path.

Run this from Windows with the same Python used by RBL Studio:
    py -3 tools/windows_wsl_smoke.py
"""
from __future__ import annotations
import os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if os.name != "nt":
    print("SKIP: this smoke test is Windows-only")
    raise SystemExit(0)

print("RBL Studio Windows -> WSL backend smoke test")
print("Root:", ROOT)
print("Python:", sys.version.split()[0])
print("WSL distro: Ubuntu-24.04")
print("Path normalization: Windows drive paths are converted to POSIX-style before wslpath")

def run(*args):
    p = subprocess.run(args, cwd=str(ROOT), text=True, capture_output=True, encoding="utf-8", errors="replace")
    print("$", " ".join(map(str, args)))
    if p.stdout: print(p.stdout, end="")
    if p.stderr: print(p.stderr, end="", file=sys.stderr)
    return p.returncode

rc = run(sys.executable, str(ROOT / "tools" / "rbltool.py"), "doctor")
if rc:
    raise SystemExit(rc)
rc = run(sys.executable, str(ROOT / "tools" / "rbltool.py"), "build", str(ROOT / "examples" / "test.rbl"))
if rc:
    raise SystemExit(rc)
rc = run(sys.executable, str(ROOT / "tools" / "rbltool.py"), "run", str(ROOT / "examples" / "test.rbl"))
raise SystemExit(rc)
