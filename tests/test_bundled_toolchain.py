#!/usr/bin/env python3
"""Verify bundled MinGW-w64 discovery and external-toolchain fallback."""
from __future__ import annotations

import os
import json
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def _make_toolchain(bin_dir: Path) -> None:
    bin_dir.mkdir(parents=True)
    for name in ("gcc.exe", "as.exe"):
        (bin_dir / name).write_bytes(b"test stub")


def main() -> int:
    old_env = os.environ.get("RBL_MINGW")
    try:
        with tempfile.TemporaryDirectory(prefix="rbl-toolchain-") as temp:
            root = Path(temp) / "RBL"
            bundled_bin = root / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64" / "bin"
            external_bin = Path(temp) / "external" / "bin"
            _make_toolchain(bundled_bin)
            _make_toolchain(external_bin)
            manifest_files = {}
            for relative in (
                "bin/gcc.exe", "bin/as.exe", "x86_64-w64-mingw32/bin/ld.exe",
                "lib/crt2.o", "lib/default-manifest.o",
                "lib/gcc/x86_64-w64-mingw32/16.2.0/collect2.exe",
            ):
                path = bundled_bin.parents[0] / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists():
                    path.write_bytes(b"test stub")
                manifest_files[relative] = {"size": path.stat().st_size, "sha256": "test"}
            (bundled_bin.parent / "rbl-toolchain-manifest.json").write_text(
                json.dumps({"schema": 1, "gcc_version": "16.2.0", "files": manifest_files}), encoding="utf-8"
            )

            os.environ.pop("RBL_MINGW", None)
            with patch.object(rbltool, "ROOT", root), \
                 patch.object(rbltool, "is_windows", return_value=True), \
                 patch.object(rbltool.shutil, "which", return_value=None):
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "bundled" or tc["bin"] != bundled_bin:
                    raise AssertionError(f"bundled toolchain did not take priority: {tc}")

                os.environ["RBL_MINGW"] = str(external_bin)
                tc = rbltool.find_native_toolchain()
                if tc is None or tc["kind"] != "external" or tc["bin"] != external_bin:
                    raise AssertionError(f"explicit external override failed: {tc}")
                os.environ.pop("RBL_MINGW", None)

                (bundled_bin / "gcc.exe").unlink()
                (bundled_bin / "as.exe").unlink()
                (bundled_bin.parent / "rbl-toolchain-manifest.json").unlink()
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

        print("PASS: bundled MinGW-w64 is preferred and external fallback remains available")
        return 0
    finally:
        if old_env is None:
            os.environ.pop("RBL_MINGW", None)
        else:
            os.environ["RBL_MINGW"] = old_env


if __name__ == "__main__":
    raise SystemExit(main())
