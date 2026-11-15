#!/usr/bin/env python3
"""Ensure `make clean`/`clean-build`/`clean-cache` touch only disposable paths."""
from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"


def recipe(source: str, target: str) -> str:
    lines = source.splitlines()
    starts = [i for i, line in enumerate(lines) if line.startswith(f"{target}:")]
    if not starts:
        raise AssertionError(f"Makefile target is missing: {target}")
    body = []
    for line in lines[starts[0] + 1:]:
        if line.startswith("\t"):
            body.append(line)
        elif line.strip():
            break
    return "\n".join(body)


def main() -> int:
    source = MAKEFILE.read_text(encoding="utf-8").replace("\r\n", "\n")
    clean = recipe(source, "clean")
    clean_build = recipe(source, "clean-build")
    clean_cache = recipe(source, "clean-cache")

    for target, body in (("clean", clean), ("clean-build", clean_build), ("clean-cache", clean_cache)):
        if "rm -rf build" in body or "build/source-material" in body:
            raise AssertionError(f"make {target} must not recursively remove the build root or source materials")

    if "\nclean: clean-build\n" not in source:
        raise AssertionError("make clean must delegate to clean-build so the cache survives")
    if "build/cache" in clean_build or "$(CACHE)" in clean_build:
        raise AssertionError("clean-build must not remove the compiler/source cache")
    for required in (
        "$(COMPILER_BUILD)",
        "$(OUTPUT)/wx-ui-linux",
        "$(OUTPUT)/cargo-target-wx-ui-linux",
    ):
        if required not in clean_build:
            raise AssertionError(f"clean-build no longer lists its intended disposable target: {required}")
    if "$(CACHE)/sources" not in clean_cache:
        raise AssertionError("clean-cache must remove $(CACHE)/sources")
    for forbidden in ("$(COMPILER_BUILD)", "$(OUTPUT)", "build/source-material"):
        if forbidden in clean_cache:
            raise AssertionError(f"clean-cache must not touch build outputs or evidence: {forbidden}")

    make = shutil.which("make")
    shell = shutil.which("sh")
    rm = shutil.which("rm")
    if not (make and shell and rm):
        print("PASS: make clean targets are statically scoped; SKIP sandbox execution (GNU make/sh/rm unavailable)")
        return 0

    with tempfile.TemporaryDirectory(prefix="rbl-make-clean-") as temp:
        sandbox = Path(temp)
        (sandbox / "VERSION").write_text("0.8.0\n", encoding="ascii")
        disposable = (
            "build/output/compiler",
            "build/output/wx-ui-linux",
            "build/output/cargo-target-wx-ui-linux",
        )
        for relative in disposable:
            directory = sandbox / relative
            directory.mkdir(parents=True)
            (directory / "marker").write_text("remove", encoding="ascii")
        retained = sandbox / "build" / "source-material"
        retained.mkdir(parents=True)
        marker = retained / "corresponding-source.tar.zst"
        marker.write_bytes(b"must survive make clean")
        cache = sandbox / "build" / "cache" / "sources" / "linux"
        cache.mkdir(parents=True)
        cached = cache / "program"
        cached.write_bytes(b"must survive make clean")
        download_cache = sandbox / "build" / "cache" / "wxwidgets-3.2.4"
        download_cache.mkdir(parents=True)
        downloaded = download_cache / "source.tar.bz2"
        downloaded.write_bytes(b"must survive clean-cache")
        binary_dir = sandbox / "bin" / "linux-x86_64"
        binary_dir.mkdir(parents=True)
        for name in ("rblc-asm", "rbl_runtime.o", "rbl_containers.o", "rbl_abi.o", "rbl_format.o"):
            (binary_dir / name).write_bytes(b"disposable")

        result = subprocess.run(
            [make, "-f", str(MAKEFILE), "clean"],
            cwd=sandbox,
            text=True,
            capture_output=True,
            timeout=30,
        )
        if result.returncode:
            raise AssertionError(f"sandboxed make clean failed: {result.stdout}\n{result.stderr}")
        if not marker.is_file() or marker.read_bytes() != b"must survive make clean":
            raise AssertionError("make clean removed or changed corresponding-source materials")
        if not cached.is_file() or cached.read_bytes() != b"must survive make clean":
            raise AssertionError("make clean removed the compiler cache")
        for relative in disposable:
            if (sandbox / relative).exists():
                raise AssertionError(f"make clean left disposable output behind: {relative}")
        if any((binary_dir / name).exists() for name in ("rblc-asm", "rbl_runtime.o", "rbl_containers.o", "rbl_abi.o", "rbl_format.o")):
            raise AssertionError("make clean left compiler/runtime binaries behind")

        result = subprocess.run(
            [make, "-f", str(MAKEFILE), "clean-cache"],
            cwd=sandbox,
            text=True,
            capture_output=True,
            timeout=30,
        )
        if result.returncode:
            raise AssertionError(f"sandboxed make clean-cache failed: {result.stdout}\n{result.stderr}")
        if (sandbox / "build" / "cache" / "sources").exists():
            raise AssertionError("make clean-cache left the compiler cache behind")
        if not downloaded.is_file():
            raise AssertionError("make clean-cache removed downloaded upstream source archives")
        if not marker.is_file():
            raise AssertionError("make clean-cache removed corresponding-source materials")

    print("PASS: sandboxed make clean removes outputs only and preserves the cache/evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
