#!/usr/bin/env python3
"""Execute Linux Runtime call sites with a SysV stack-alignment trap."""
from __future__ import annotations

import os
import re
import shlex
import runpy
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def instrument_calls(source: str) -> tuple[str, int]:
    """Insert a flags-preserving RSP%16 check immediately before each CALL."""
    pattern = re.compile(r"(?m)^(\s*)call(\s+.+)$")
    count = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal count
        count += 1
        indent, instruction = match.groups()
        return (
            f"{indent}push r10\n"
            f"{indent}push r11\n"
            f"{indent}pushfq\n"
            f"{indent}pop r10\n"
            f"{indent}mov r11, rsp\n"
            f"{indent}and r11, 15\n"
            f"{indent}test r11, r11\n"
            f"{indent}jz 9f\n"
            f"{indent}ud2\n"
            f"{indent}9:\n"
            f"{indent}push r10\n"
            f"{indent}popfq\n"
            f"{indent}pop r11\n"
            f"{indent}pop r10\n"
            f"{indent}call{instruction}"
        )

    return pattern.sub(replace, source), count


def assemble_instrumented(source: Path, obj: Path, settings: dict) -> tuple[int, str]:
    if rbltool.use_wsl(settings):
        source_wsl = shlex.quote(rbltool.wsl_path(source, settings))
        obj_wsl = shlex.quote(rbltool.wsl_path(obj, settings))
        code, out, err = rbltool.run_wsl(f"as --64 {source_wsl} -o {obj_wsl}", settings)
    else:
        code, out, err = rbltool.run_native(["as", "--64", str(source), "-o", str(obj)])
    return code, out + err


def main() -> int:
    original_mode = os.environ.get("RBL_TOOLCHAIN")
    original_runtime_source = rbltool.RUNTIME_SRC
    original_runtime_object = rbltool.RUNTIME_OBJ

    if os.name == "nt":
        if not rbltool.wsl_available({}):
            print("SKIP: Linux assembly alignment test requires WSL on Windows")
            return 0
        os.environ["RBL_TOOLCHAIN"] = "wsl"
    else:
        os.environ["RBL_TOOLCHAIN"] = "native"

    settings = rbltool.load_settings()
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    try:
        with tempfile.TemporaryDirectory(prefix="rbl-asm-alignment-", dir=build_root) as temp:
            temp_root = Path(temp)
            instrumented_source = temp_root / "rbl_runtime_aligned.s"
            instrumented_object = temp_root / "rbl_runtime_aligned.o"
            source_text = original_runtime_source.read_text(encoding="utf-8")
            instrumented_text, calls = instrument_calls(source_text)
            if calls < 30:
                print(f"FAIL: expected a complete Runtime call inventory, found {calls}", file=sys.stderr)
                return 1
            instrumented_source.write_text(instrumented_text, encoding="utf-8")

            code, output = assemble_instrumented(instrumented_source, instrumented_object, settings)
            if code:
                print(output, end="", file=sys.stderr)
                return code

            # The test runner imports the same rbltool module, so its builds link
            # this instrumented Linux Runtime instead of the normal object.
            rbltool.RUNTIME_SRC = instrumented_source
            rbltool.RUNTIME_OBJ = instrumented_object
            regression = runpy.run_path(str(ROOT / "tests" / "run_tests.py"))
            code = int(regression["main"]())
            if code:
                print("FAIL: an exercised Linux Runtime CALL violated SysV stack alignment", file=sys.stderr)
                return code

            extra = temp_root / "runtime_alignment_extra.rbl"
            extra.write_text(
                "func main() (\n"
                '    print("alpha" != "beta")\n'
                '    print(rbl.string.starts_with("abcdef", "abc"))\n'
                '    print(rbl.string.ends_with("abcdef", "def"))\n'
                '    print(rbl.string.contains("abcdef", "cd"))\n'
                '    print("left" + "right")\n'
                ')\n',
                encoding="utf-8",
            )
            code, stdout, stderr = rbltool.run_program(extra, settings)
            expected = "true\ntrue\ntrue\ntrue\nleftright\n"
            if code or stdout != expected:
                print(
                    f"FAIL: extra Runtime call paths: exit={code}, "
                    f"stdout={stdout!r}, stderr={stderr!r}",
                    file=sys.stderr,
                )
                return code or 1

            print(f"PASS: {calls} Linux Runtime CALL sites instrumented; exercised calls preserved SysV alignment")
            return 0
    finally:
        rbltool.RUNTIME_SRC = original_runtime_source
        rbltool.RUNTIME_OBJ = original_runtime_object
        if original_mode is None:
            os.environ.pop("RBL_TOOLCHAIN", None)
        else:
            os.environ["RBL_TOOLCHAIN"] = original_mode


if __name__ == "__main__":
    raise SystemExit(main())
