#!/usr/bin/env python3
"""Verify release packaging refuses dirty trees and preserves archive contents."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "package_release.py"


def fixture(base: Path, platform: str) -> Path:
    staged = base / f"RBLStudio-0.8.0-{platform}"
    files = {
        "VERSION": "0.8.0\n",
        "rbl.py": "print('cli')\n",
        "LICENSE": "license\n",
        "README.md": "readme\n",
        "RELEASE_MANIFEST.txt": "manifest\n",
        "THIRD_PARTY_NOTICES.txt": "notices\n",
        "examples/test.rbl": 'func main() ( print("ok") )\n',
        "tests/run_tests.py": "print('tests')\n",
        "benchmarks/run_benchmarks.py": "print('bench')\n",
        "licenses/License.txt": "license\n",
        "source/source.tar.zst": "source\n",
        "runtime/python/manifest.json": "{}\n",
        "toolchain/manifest.json": "{}\n",
    }
    if platform == "windows":
        files.update({
            "rbl-studio-wx.exe": "gui",
            "rbl-studio-worker.exe": "worker",
            "rbl.cmd": "@echo off\n",
            "RBLStudio.cmd": "@echo off\n",
            "RBLStudio.vbs": "' vbs\n",
            "runtime/python/python.exe": "python",
            "toolchain/windows-x86_64/msys64/ucrt64/bin/as.exe": "as",
            "toolchain/windows-x86_64/msys64/ucrt64/x86_64-w64-mingw32/bin/ld.exe": "ld",
            "toolchain/windows-x86_64/msys64/ucrt64/rbl-toolchain-manifest.json": "{}\n",
        })
    else:
        files.update({
            "rbl-studio-wx": "gui",
            "rbl-studio-worker": "worker",
            "rbl": "#!/bin/sh\n",
            "RBLStudio.sh": "#!/bin/sh\n",
            "runtime/python/bin/python3": "python",
            "toolchain/linux-x86_64/bin/clang": "clang",
            "toolchain/linux-x86_64/bin/ld.lld": "lld",
            "toolchain/linux-x86_64/manifest.json": "{}\n",
        })
    for relative, content in files.items():
        path = staged / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        if relative.endswith((".exe", "python3", "clang", "ld.lld", "rbl")):
            path.chmod(0o755)
    return staged


def run(staged: Path, output: Path, platform: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), "--platform", platform, "--staged", str(staged), "--output", str(output)],
        text=True,
        capture_output=True,
        timeout=180,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rbl-package-test-") as temp:
        base = Path(temp)
        staged = fixture(base, "windows")
        output = base / "out"
        result = run(staged, output, "windows")
        if result.returncode:
            raise AssertionError(f"windows packaging failed: {result.stdout}\n{result.stderr}")
        archive = output / "RBLStudio-0.8.0-windows-x86_64.zip"
        if not archive.is_file():
            raise AssertionError("windows archive was not created")
        with zipfile.ZipFile(archive) as zf:
            names = zf.namelist()
            root = "RBLStudio-0.8.0-windows-x86_64/"
            if not all(name.startswith(root) for name in names):
                raise AssertionError(f"archive entries escape the root directory: {names[:5]}")
            if zf.read(root + "VERSION").strip() != b"0.8.0":
                raise AssertionError("archive content mismatch")
        sums = (output / "SHA256SUMS").read_text(encoding="utf-8")
        if archive.name not in sums:
            raise AssertionError("SHA256SUMS does not cover the archive")
        metadata = json.loads((output / "RBLStudio-0.8.0-windows-x86_64.json").read_text(encoding="utf-8"))
        if metadata["status"] != "LOCAL PREBUILT CANDIDATE - NOT PUBLISHED":
            raise AssertionError("candidate metadata status is wrong")
        note_path = output / "RBLStudio-0.8.0-windows-x86_64-LOCAL_CANDIDATE.txt"
        if not note_path.is_file():
            raise AssertionError("human-readable candidate note is missing")
        note_text = note_path.read_text(encoding="utf-8")
        note_base = "RBLStudio-0.8.0-windows-x86_64"
        if not note_text.startswith(note_base + "\n" + "=" * len(note_base) + "\n\n"):
            raise AssertionError("candidate note header is malformed (expected name followed by a '=' rule)")
        if "=" + note_base in note_text:
            raise AssertionError("candidate note contains a duplicated '=name' header line")

        dirty = fixture(base / "dirty", "windows")
        (dirty / ".rblstudio-install.json").write_text("{}", encoding="utf-8")
        result = run(dirty, base / "dirty-out", "windows")
        if result.returncode == 0:
            raise AssertionError("packaging accepted a tree with install state")
        if "install state" not in result.stderr and "machine-specific" not in result.stderr:
            raise AssertionError(f"refusal reason is unclear: {result.stderr}")

        if os.name != "nt" and shutil.which("tar") and shutil.which("zstd"):
            linux = fixture(base / "linux", "linux")
            (linux / "runtime/python/bin/python3").unlink()
            (linux / "runtime/python/bin/python3").symlink_to("../python-real")
            (linux / "runtime/python/python-real").write_text("real", encoding="utf-8")
            result = run(linux, base / "linux-out", "linux")
            if result.returncode:
                raise AssertionError(f"linux packaging failed: {result.stdout}\n{result.stderr}")
            tar_archive = base / "linux-out" / "RBLStudio-0.8.0-linux-x86_64.tar.zst"
            extracted = base / "linux-extract"
            extracted.mkdir()
            subprocess.run(
                ["tar", "--zstd", "-xf", str(tar_archive), "-C", str(extracted)],
                check=True,
                capture_output=True,
            )
            link = extracted / "RBLStudio-0.8.0-linux-x86_64" / "runtime/python/bin/python3"
            if not link.is_symlink() or os.readlink(link) != "../python-real":
                raise AssertionError("linux archive did not preserve the symlink")
            if not os.access(extracted / "RBLStudio-0.8.0-linux-x86_64" / "rbl-studio-wx", os.X_OK):
                raise AssertionError("linux archive did not preserve the executable bit")
        else:
            print("SKIP: Linux archive checks require a POSIX host with tar/zstd")

    print("PASS: release packaging validates staging state and preserves archive contents")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
