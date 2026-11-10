#!/usr/bin/env python3
from __future__ import annotations

import shlex
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import rbltool

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    settings = rbltool.load_settings()
    if rbltool.use_native_windows(settings):
        toolchain = rbltool.find_native_toolchain()
        if toolchain and toolchain.get("kind") == "bundled":
            print("SKIP: native C ownership probe requires GCC cc1/headers; use full MinGW-w64 or WSL for this developer-only test")
            return 0
    build_root = ROOT / "build"
    build_root.mkdir(exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="rbl-string-ownership-", dir=build_root) as temp:
        executable = Path(temp) / (
            "string_ownership_probe.exe"
            if rbltool.use_native_windows(settings)
            else "string_ownership_probe"
        )

        if rbltool.use_wsl(settings):
            command = " ".join([
                "cc", "-std=c11", "-O2", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Wpedantic", "-DRBL_GC_TESTING",
                shlex.quote(rbltool.wsl_path(ROOT / "runtime" / "rbl_containers.c", settings)),
                shlex.quote(rbltool.wsl_path(ROOT / "tests" / "string_ownership_probe.c", settings)),
                "-o", shlex.quote(rbltool.wsl_path(executable, settings)),
            ])
            code, out, err = rbltool.run_wsl(command, settings)
        elif rbltool.use_native_windows(settings):
            toolchain = rbltool.find_native_toolchain()
            if not toolchain:
                print("FAIL: native Windows C toolchain is unavailable", file=sys.stderr)
                return 1
            command = [
                str(toolchain["gcc"]), "-std=c11", "-O2", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Wpedantic", "-DRBL_GC_TESTING",
                str(ROOT / "runtime" / "rbl_containers.c"),
                str(ROOT / "tests" / "string_ownership_probe.c"),
                "-o", str(executable), "-static",
            ]
            code, out, err = rbltool.run_native(command, env=rbltool.toolchain_env(toolchain))
        else:
            command = [
                "cc", "-std=c11", "-O2", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Wpedantic", "-DRBL_GC_TESTING",
                str(ROOT / "runtime" / "rbl_containers.c"),
                str(ROOT / "tests" / "string_ownership_probe.c"),
                "-o", str(executable),
            ]
            code, out, err = rbltool.run_native(command)

        if code:
            print(out, end="")
            print(err, end="", file=sys.stderr)
            return code

        code, out, err = rbltool.run_built(executable, settings=settings)
        print(out, end="")
        if code:
            print(err, end="", file=sys.stderr)
            return code
        if "PASS: string roots, container roots, reclamation, OOM rollback, registry retries and native release" not in out:
            print("FAIL: ownership probe did not report its success marker", file=sys.stderr)
            return 1

        for mode, expected in (
            ("--string-alloc-oom", "out of memory while allocating a string"),
            ("--registry-init-oom", "out of memory while tracking a string"),
            ("--container-registry-init-oom", "out of memory while tracking a container"),
        ):
            if rbltool.use_wsl(settings):
                command = f"{shlex.quote(rbltool.wsl_path(executable, settings))} {mode}"
                fail_code, fail_out, fail_err = rbltool.run_wsl(command, settings)
            else:
                fail_code, fail_out, fail_err = rbltool.run_native([str(executable), mode])
            if fail_code != 2 or expected not in (fail_out + fail_err):
                print(f"FAIL: {mode} did not fail with the expected Runtime diagnostic", file=sys.stderr)
                print(f"exit={fail_code}; stdout={fail_out!r}; stderr={fail_err!r}", file=sys.stderr)
                return 1

    print("ALL STRING OWNERSHIP TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
