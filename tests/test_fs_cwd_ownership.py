#!/usr/bin/env python3
from __future__ import annotations

import shlex
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def main() -> int:
    settings = rbltool.load_settings()
    if rbltool.use_native_windows(settings):
        print("SKIP: rbl.fs.cwd ownership probe exercises the POSIX assembly runtime; WSL/Linux runs it")
        return 0

    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rbl-cwd-ownership-", dir=build_root) as temp:
        executable = Path(temp) / "fs_cwd_ownership_probe"
        sources = [
            ROOT / "runtime" / "rbl_runtime.s",
            ROOT / "runtime" / "rbl_containers.c",
            ROOT / "runtime" / "rbl_abi.c",
            ROOT / "runtime" / "rbl_format.c",
            ROOT / "tests" / "fs_cwd_ownership_probe.c",
        ]
        if rbltool.use_wsl(settings):
            command = " ".join([
                "cc", "-std=c11", "-O2", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Wpedantic", "-DRBL_GC_TESTING",
                *(shlex.quote(rbltool.wsl_path(path, settings)) for path in sources),
                "-lm", "-o", shlex.quote(rbltool.wsl_path(executable, settings)),
            ])
            code, out, err = rbltool.run_wsl(command, settings)
        else:
            command = [
                "cc", "-std=c11", "-O2", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Wpedantic", "-DRBL_GC_TESTING",
                *(str(path) for path in sources), "-lm", "-o", str(executable),
            ]
            code, out, err = rbltool.run_native(command)

        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code
        if rbltool.use_wsl(settings):
            run_command = shlex.quote(rbltool.wsl_path(executable, settings))
            code, out, err = rbltool.run_wsl(run_command, settings)
        else:
            code, out, err = rbltool.run_native([str(executable)])
        print(out, end="")
        if code:
            print(err, end="", file=sys.stderr)
            return code
        if "16/16 registered" not in out:
            print("FAIL: cwd ownership probe omitted its success marker", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
