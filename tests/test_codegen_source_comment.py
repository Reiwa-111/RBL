#!/usr/bin/env python3
"""Ensure POSIX source filenames cannot inject directives into generated ASM."""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def main() -> int:
    if os.name == "nt":
        print("SKIP: POSIX permits CR/LF in filenames; Windows does not")
        return 0

    rbltool.ensure_toolchain({})
    with tempfile.TemporaryDirectory(prefix="rbl-source-comment-") as tmp:
        directory = Path(tmp)
        source = directory / "path\n.section .comment\r.section .rbl-injected\n.rbl"
        assembly = directory / "generated.s"
        source.write_text("func main() (\n    print(1)\n)\n", encoding="utf-8")
        result = subprocess.run(
            [str(rbltool.COMPILER_BIN), str(source), "-S", str(assembly)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"compiler rejected a valid newline-containing source path: "
                f"{result.stdout}\n{result.stderr}"
            )
        generated = assembly.read_text(encoding="utf-8")
        first_line = generated.splitlines()[0]
        if not first_line.startswith("# RBL direct x86-64 ASM generated from "):
            raise AssertionError(f"unexpected source comment: {first_line!r}")
        if any(line.strip() in {".section .comment", ".section .rbl-injected"}
               for line in generated.splitlines()[1:]):
            raise AssertionError("source path escaped its assembler comment")
        print("source-path CR/LF assembler-comment injection: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
