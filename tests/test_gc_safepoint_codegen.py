#!/usr/bin/env python3
"""Verify that compiler-emitted GC safe points are limited to risky statements."""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


SOURCE = '''func scalar(x int) int (
    return x + 1
)

func allocating() int (
    set values = [1]
    return len(values)
)

func main() (
    set x = 1
    set y = x + 2
    print(scalar(y))
    print(allocating())
)
'''


def function_body(assembly: str, label: str, next_label: str) -> str:
    start = assembly.index(f"\n{label}:\n")
    end = assembly.index(f"\n{next_label}:\n", start)
    return assembly[start:end]


def main() -> int:
    settings = rbltool.load_settings()
    rbltool.ensure_toolchain(settings)
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="gc-safepoint-codegen-", dir=build_root) as temp:
        source = Path(temp) / "safepoints.rbl"
        assembly = Path(temp) / "safepoints.s"
        source.write_text(SOURCE, encoding="utf-8")
        command = rbltool.compiler_cmd(source, assembly, settings)
        if rbltool.use_wsl(settings):
            code, out, err = rbltool.run_wsl(command, settings)
        else:
            code, out, err = rbltool.run_native(command)
        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        generated = assembly.read_text(encoding="utf-8")
        scalar = function_body(generated, "fn_0", "fn_1")
        allocating = function_body(generated, "fn_1", "fn_2")
        if "call rbl_gc_safepoint" in scalar:
            print("FAIL: scalar-only function contains redundant GC safe points", file=sys.stderr)
            return 1
        if "call rbl_gc_safepoint" not in allocating:
            print("FAIL: allocating function lost its GC safe points", file=sys.stderr)
            return 1

        code, stdout, stderr = rbltool.run_program(source, settings)
        if code or stdout != "4\n1\n":
            print(f"FAIL: scalar/allocating fixture: exit={code}, stdout={stdout!r}, stderr={stderr!r}", file=sys.stderr)
            return 1

    print("PASS: scalar-only statements omit GC safe points; allocating statements retain them")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
