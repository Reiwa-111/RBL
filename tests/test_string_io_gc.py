#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import rbltool

ROOT = Path(__file__).resolve().parents[1]
FILE_BYTES = 64 * 1024
FILE_READS = 96
INPUT_LINES = 12000
INPUT_WIDTH = 256


def rbl_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    settings = rbltool.load_settings()
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="rbl-string-io-gc-", dir=build_root) as temp:
        temp_root = Path(temp)
        data_path = temp_root / "gc-input.bin"
        binary_path = temp_root / "invalid-utf8.bin"
        source_path = temp_root / "string_io_gc.rbl"
        width_source_path = temp_root / "string_width_invalid_utf8.rbl"
        data_path.write_bytes(b"A" * FILE_BYTES)
        binary_path.write_bytes(b"\xff\xfe")
        runtime_path = rbltool.wsl_path(data_path, settings) if rbltool.use_wsl(settings) else data_path.as_posix()
        binary_runtime_path = rbltool.wsl_path(binary_path, settings) if rbltool.use_wsl(settings) else binary_path.as_posix()

        source_path.write_text(
            "func main() (\n"
            '    set file_first = ""\n'
            '    set file_last = ""\n'
            '    set contents = ""\n'
            "    set file_roots = []\n"
            "    set i = 0\n"
            f"    for (i < {FILE_READS}) (\n"
            f"        let contents = read_file({rbl_string(runtime_path)})\n"
            "        if (i == 0) (\n"
            "            let file_first = contents\n"
            "            let file_roots = list_push(file_roots, contents)\n"
            "        )\n"
            "        let file_last = contents\n"
            "        let i += 1\n"
            "    )\n"
            "    print(len(file_first))\n"
            "    print(len(file_last))\n"
            "    print(len(file_roots[0]))\n"
            f"    set binary = read_file({rbl_string(binary_runtime_path)})\n"
            '    print(len(f"{binary:}"))\n'
            '    print(len(f"{binary:s}"))\n'
            '    set input_first = ""\n'
            '    set input_last = ""\n'
            "    set line = \"\"\n"
            "    set j = 0\n"
            f"    for (j < {INPUT_LINES}) (\n"
            "        if (j % 2 == 0) (\n"
            "            let line = input()\n"
            "        ) else (\n"
            '            let line = input("P:")\n'
            "        )\n"
            "        if (j == 0) (\n"
            "            let input_first = line\n"
            "        )\n"
            "        let input_last = line\n"
            "        let j += 1\n"
            "    )\n"
            "    print(len(input_first))\n"
            "    print(len(input_last))\n"
            "    print(input_first == input_last)\n"
            ")\n",
            encoding="utf-8",
        )

        code, output, info = rbltool.build(source_path, settings)
        if code:
            print("STRING I/O GC BUILD: FAIL")
            print(output)
            return code

        first = "FIRST" + "a" * (INPUT_WIDTH - 5)
        middle = "MIDDLE" + "m" * (INPUT_WIDTH - 6)
        last = "LAST" + "z" * (INPUT_WIDTH - 4)
        stdin_text = "\n".join([first, *([middle] * (INPUT_LINES - 2)), last]) + "\n"
        code, stdout, stderr = rbltool.run_built(info, stdin_text=stdin_text, settings=settings)
        expected_prefix = f"{FILE_BYTES}\n{FILE_BYTES}\n{FILE_BYTES}\n2\n2\n"
        expected_suffix = f"{INPUT_WIDTH}\n{INPUT_WIDTH}\nfalse\n"
        if (code or not stdout.startswith(expected_prefix) or
                not stdout.endswith(expected_suffix) or
                stdout.count("P:") != INPUT_LINES // 2):
            print("STRING I/O GC: FAIL")
            print(f"exit={code}; prompt_count={stdout.count('P:')}; expected={INPUT_LINES // 2}")
            print("stdout tail:", repr(stdout[-256:]))
            print("stderr:", stderr, file=sys.stderr)
            return code or 1

        width_source_path.write_text(
            "func main() (\n"
            f"    set binary = read_file({rbl_string(binary_runtime_path)})\n"
            '    print(f"{binary:>4s}")\n'
            ")\n",
            encoding="utf-8",
        )
        code, output, info = rbltool.build(width_source_path, settings)
        if code:
            print("STRING WIDTH INVALID UTF-8 BUILD: FAIL")
            print(output)
            return code
        code, stdout, stderr = rbltool.run_built(info, settings=settings)
        if code == 0 or "formatting requires valid UTF-8 text" not in stderr:
            print("STRING WIDTH INVALID UTF-8: FAIL")
            print(f"exit={code}; stdout={stdout!r}; stderr={stderr!r}")
            return code or 1

    print(
        f"PASS: {FILE_READS} x {FILE_BYTES}-byte read_file and "
        f"{INPUT_LINES} input calls survived GC pressure"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
