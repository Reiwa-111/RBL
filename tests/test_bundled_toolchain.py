#!/usr/bin/env python3
"""Verify bundled MinGW-w64 discovery and external-toolchain fallback."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402
import package_windows_toolchain as toolchain_packager  # noqa: E402


def _make_toolchain(bin_dir: Path) -> None:
    bin_dir.mkdir(parents=True)
    for name in ("gcc.exe", "as.exe"):
        (bin_dir / name).write_bytes(b"test stub")


def _write_manifest(root: Path) -> None:
    files = {}
    total_bytes = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "rbl-toolchain-manifest.json":
            continue
        payload = path.read_bytes()
        files[path.relative_to(root).as_posix()] = {
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        total_bytes += len(payload)
    packages = [
        {
            "id": name,
            "version": version,
            "source_archive": f"https://mirror.msys2.org/mingw/sources/{archive}",
        }
        for name, version, archive in toolchain_packager.PACKAGE_SOURCES
    ]
    manifest = {
        "schema": 1,
        "gcc_version": "16.2.0",
        "packages": packages,
        "files": files,
        "file_count": len(files),
        "total_bytes": total_bytes,
    }
    (root / "rbl-toolchain-manifest.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )


def _expect_invalid_bundle(bundled_bin: Path, message: str) -> None:
    with patch.object(rbltool, "_mingw_candidates", return_value=[bundled_bin]):
        try:
            rbltool.find_native_toolchain()
        except RuntimeError as error:
            if "Bundled Windows toolchain failed manifest verification" not in str(error):
                raise
        else:
            raise AssertionError(message)


def _check_app_local_python_resolution() -> None:
    if os.name != "nt":
        return
    python = ROOT / "runtime" / "python" / "python.exe"
    if not python.is_file():
        raise AssertionError(f"tracked app-local CPython is missing: {python}")
    script = ROOT / "tools" / "rbltool.py"
    env = os.environ.copy()
    env.pop("RBL_MINGW", None)
    for isolation_flag in ("-I", "-B"):
        completed = subprocess.run(
            [str(python), isolation_flag, str(script), "doctor"],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if completed.returncode != 0 or "Win toolchain source bundled" not in completed.stdout:
            raise AssertionError(
                f"app-local CPython {isolation_flag} doctor failed "
                f"(exit={completed.returncode}):\n{completed.stdout}"
            )


def main() -> int:
    old_env = os.environ.get("RBL_MINGW")
    try:
        with tempfile.TemporaryDirectory(prefix="rbl-toolchain-") as temp:
            root = Path(temp) / "RBL"
            bundled_bin = root / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64" / "bin"
            external_bin = Path(temp) / "external" / "bin"
            _make_toolchain(bundled_bin)
            _make_toolchain(external_bin)
            for relative in (
                "bin/gcc.exe", "bin/as.exe", "x86_64-w64-mingw32/bin/ld.exe",
                "lib/crt2.o", "lib/default-manifest.o",
                "lib/gcc/x86_64-w64-mingw32/16.2.0/collect2.exe",
            ):
                path = bundled_bin.parents[0] / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    path.write_bytes(b"test stub")
            _write_manifest(bundled_bin.parent)

            os.environ.pop("RBL_MINGW", None)
            with patch.object(rbltool, "ROOT", root), \
                 patch.object(rbltool, "is_windows", return_value=True), \
                 patch.object(rbltool.shutil, "which", return_value=None):
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "bundled" or tc["bin"] != bundled_bin:
                    raise AssertionError(f"bundled toolchain did not take priority: {tc}")

                # Runtime discovery must reject tampering even when the payload
                # size is unchanged, malformed digests, and unmanifested files.
                original_gcc = (bundled_bin / "gcc.exe").read_bytes()
                tampered_gcc = b"evil stub"
                if len(tampered_gcc) != len(original_gcc):
                    raise AssertionError("same-size tamper fixture changed the file length")
                (bundled_bin / "gcc.exe").write_bytes(tampered_gcc)
                _expect_invalid_bundle(
                    bundled_bin, "bundled toolchain accepted a same-size modified gcc.exe"
                )
                os.environ["RBL_MINGW"] = str(external_bin)
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "external" or tc["bin"] != external_bin:
                    raise AssertionError(f"explicit external override did not bypass damaged bundle: {tc}")
                os.environ.pop("RBL_MINGW", None)
                (bundled_bin / "gcc.exe").write_bytes(original_gcc)
                _write_manifest(bundled_bin.parent)

                manifest_path = bundled_bin.parent / "rbl-toolchain-manifest.json"
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                manifest["files"]["bin/gcc.exe"]["sha256"] = "not-a-sha256"
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                _expect_invalid_bundle(
                    bundled_bin, "bundled toolchain accepted malformed SHA-256 metadata"
                )
                _write_manifest(bundled_bin.parent)

                extra = bundled_bin / "unmanifested.dll"
                extra.write_bytes(b"untracked payload")
                _expect_invalid_bundle(
                    bundled_bin, "bundled toolchain accepted an unmanifested payload file"
                )
                extra.unlink()
                manifest_path.unlink()
                _expect_invalid_bundle(
                    bundled_bin, "bundled toolchain silently fell back when its manifest was missing"
                )
                _write_manifest(bundled_bin.parent)

                os.environ["RBL_MINGW"] = str(external_bin)
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "external" or tc["bin"] != external_bin:
                    raise AssertionError(f"explicit external override failed: {tc}")
                os.environ.pop("RBL_MINGW", None)

                shutil.rmtree(bundled_bin.parent)
                os.environ.pop("RBL_MINGW", None)
                with patch.object(
                    rbltool, "_mingw_candidates", return_value=[bundled_bin, external_bin]
                ):
                    tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "external" or tc["bin"] != external_bin:
                    raise AssertionError(f"external fallback failed after bundle removal: {tc}")

                os.environ["RBL_MINGW"] = str(external_bin)
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "external" or tc["bin"] != external_bin:
                    raise AssertionError(f"external fallback failed: {tc}")

            # A portable runtime must not attempt a C bootstrap merely because
            # source timestamps are newer than the prebuilt release artifacts.
            artifacts = [Path(temp) / f"{name}.bin" for name in (
                "compiler", "runtime", "containers", "abi", "format"
            )]
            for artifact in artifacts:
                artifact.write_bytes(b"prebuilt")
            fake_tc = {"bin": bundled_bin, "gcc": bundled_bin / "gcc.exe",
                       "as": bundled_bin / "as.exe", "name": "test bundle"}
            old_rebuild = os.environ.pop("RBL_REBUILD_COMPILER", None)
            try:
                with patch.object(rbltool, "is_windows", return_value=True), \
                     patch.object(rbltool, "use_wsl", return_value=False), \
                     patch.object(rbltool, "find_native_toolchain", return_value=fake_tc), \
                     patch.object(rbltool, "WIN_COMPILER_BIN", artifacts[0]), \
                     patch.object(rbltool, "WIN_RUNTIME_OBJ", artifacts[1]), \
                     patch.object(rbltool, "WIN_CONTAINERS_OBJ", artifacts[2]), \
                     patch.object(rbltool, "WIN_RUNTIME_ABI_OBJ", artifacts[3]), \
                     patch.object(rbltool, "WIN_RUNTIME_FORMAT_OBJ", artifacts[4]), \
                     patch.object(rbltool, "run_native", side_effect=AssertionError("unexpected bootstrap")):
                    rbltool.ensure_toolchain({})
            finally:
                if old_rebuild is not None:
                    os.environ["RBL_REBUILD_COMPILER"] = old_rebuild

            # Linux release installs must also trust the shipped compiler/runtime
            old_rebuild_linux = os.environ.pop("RBL_REBUILD_COMPILER", None)
            # files rather than rebuilding solely because extraction changed mtimes.
            # This is the clean-install path: no C compiler or developer toolchain.
            linux_artifacts = [Path(temp) / f"linux-{name}.bin" for name in (
                "compiler", "runtime", "containers", "abi", "format"
            )]
            for artifact in linux_artifacts:
                artifact.write_bytes(b"prebuilt")
            linux_bin = Path(temp) / "linux-bin"
            with patch.object(rbltool, "is_windows", return_value=False), \
                 patch.object(rbltool, "use_wsl", return_value=False), \
                 patch.object(rbltool, "LINUX_BIN_DIR", linux_bin), \
                 patch.object(rbltool, "COMPILER_BIN", linux_artifacts[0]), \
                 patch.object(rbltool, "RUNTIME_OBJ", linux_artifacts[1]), \
                 patch.object(rbltool, "CONTAINERS_OBJ", linux_artifacts[2]), \
                 patch.object(rbltool, "RUNTIME_ABI_OBJ", linux_artifacts[3]), \
                 patch.object(rbltool, "RUNTIME_FORMAT_OBJ", linux_artifacts[4]), \
                 patch.object(rbltool, "_stale", side_effect=AssertionError("release mtimes must not trigger bootstrap")), \
                 patch.object(rbltool, "have", return_value=False), \
                 patch.object(rbltool, "run_native", side_effect=AssertionError("unexpected Linux bootstrap")):
                rbltool.ensure_toolchain({})
            if not linux_bin.is_dir():
                raise AssertionError("Linux install did not initialize its private output directory")
            if old_rebuild_linux is not None:
                os.environ["RBL_REBUILD_COMPILER"] = old_rebuild_linux
        _check_app_local_python_resolution()
        print("PASS: bundled MinGW-w64 integrity, fallback and app-local CPython resolution")
        return 0
    finally:
        if old_env is None:
            os.environ.pop("RBL_MINGW", None)
        else:
            os.environ["RBL_MINGW"] = old_env


if __name__ == "__main__":
    raise SystemExit(main())
