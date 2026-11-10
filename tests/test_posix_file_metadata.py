from __future__ import annotations

import pathlib
import os
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    if not sys.platform.startswith("linux"):
        print("posix-file-metadata: SKIP (Linux-only test)")
        return 0
    compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        print("posix-file-metadata: FAIL (no C++ compiler found)")
        return 1

    with tempfile.TemporaryDirectory(prefix="rbl-posix-metadata-test-") as temp:
        executable = pathlib.Path(temp) / "posix-file-metadata-test"
        command = [
            compiler,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-I",
            str(ROOT / "native" / "wx-ui" / "include"),
            str(ROOT / "native" / "wx-ui" / "src" / "file_metadata.cpp"),
            str(ROOT / "tests" / "posix_file_metadata.cpp"),
            "-o",
            str(executable),
        ]
        build = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if build.stdout:
            print(build.stdout, end="")
        if build.stderr:
            print(build.stderr, end="", file=sys.stderr)
        print(f"posix-file-metadata compile exit: {build.returncode}")
        if build.returncode:
            return build.returncode
        # Use a native Linux filesystem by default. The repository may be on
        # DrvFS, which does not support mkfifo/default POSIX ACLs consistently.
        test_dir = pathlib.Path(os.environ.get("RBL_POSIX_METADATA_TEST_DIR", "/tmp"))
        test_dir.mkdir(parents=True, exist_ok=True)
        env = os.environ.copy()
        env["TMPDIR"] = str(test_dir)
        result = subprocess.run([str(executable)], cwd=ROOT, env=env, text=True, timeout=30)
        print(f"posix-file-metadata test exit: {result.returncode}")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
