#!/usr/bin/env python3
"""Assert that calls with seven integer arguments use the generic ABI path."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def main() -> int:
    source = ROOT / "tests" / "cases" / "fast_call_seven_args.rbl"
    code, stdout, stderr, asm_path = rbltool.assemble_preview(source, {})
    if code:
        raise RuntimeError(f"could not generate seven-argument assembly: {stdout}\n{stderr}")
    assembly = Path(asm_path).read_text(encoding="utf-8")
    if "call fn_0" not in assembly:
        raise AssertionError("seven-argument call did not use the generic function entry")
    if "mov esi,7" not in assembly:
        raise AssertionError("seven-argument arity check is missing from generated assembly")
    if "fast_fn_0:" in assembly or "call fast_fn_0" in assembly:
        raise AssertionError("seven-argument function unexpectedly selected the six-register fast path")
    print("seven-argument generic ABI fallback: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
