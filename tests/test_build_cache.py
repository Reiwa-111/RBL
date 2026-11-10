#!/usr/bin/env python3
"""Verify that equal source basenames cannot share a cached executable."""
from __future__ import annotations

import os
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


def main() -> int:
    build_root = ROOT / "build"
    build_root.mkdir(parents=True, exist_ok=True)
    output_dirs: list[Path] = []

    try:
        with tempfile.TemporaryDirectory(prefix="rbl-build-cache-", dir=build_root) as temp:
            root = Path(temp)
            source_a = root / "project_a" / "same_name.rbl"
            source_b = root / "project_b" / "same_name.rbl"
            source_a.parent.mkdir(parents=True)
            source_b.parent.mkdir(parents=True)
            source_a.write_text(
                'func main() (\n    print("SOURCE A")\n)\n', encoding="utf-8"
            )
            source_b.write_text(
                'func main() (\n    print("SOURCE B")\n)\n', encoding="utf-8"
            )

            # Make B older than the executable that A will produce. A basename-only
            # cache would incorrectly treat A's executable as a valid B cache hit.
            old_ns = 1_600_000_000_000_000_000
            os.utime(source_b, ns=(old_ns, old_ns))

            settings = rbltool.load_settings()
            out_a = rbltool.build_output_dir(source_a, settings)
            out_b = rbltool.build_output_dir(source_b, settings)
            output_dirs.extend((out_a, out_b))
            if out_a == out_b:
                print("FAIL: different source paths map to the same build directory", file=sys.stderr)
                return 1

            if os.name == "nt" and rbltool.wsl_available(settings):
                original_mode = os.environ.get("RBL_TOOLCHAIN")
                try:
                    os.environ["RBL_TOOLCHAIN"] = "native"
                    native_dir = rbltool.build_output_dir(source_a, settings)
                    os.environ["RBL_TOOLCHAIN"] = "wsl"
                    wsl_dir = rbltool.build_output_dir(source_a, settings)
                finally:
                    if original_mode is None:
                        os.environ.pop("RBL_TOOLCHAIN", None)
                    else:
                        os.environ["RBL_TOOLCHAIN"] = original_mode
                if native_dir == wsl_dir:
                    print("FAIL: native Windows and WSL share the same source build directory", file=sys.stderr)
                    return 1

            rc_a, out, info_a = rbltool.build(source_a, settings)
            if rc_a:
                print(f"FAIL: source A did not build: {out}", file=sys.stderr)
                return rc_a or 1
            rc_b, out, info_b = rbltool.build(source_b, settings)
            if rc_b:
                print(f"FAIL: source B did not build: {out}", file=sys.stderr)
                return rc_b or 1

            if Path(info_a).resolve() == Path(info_b).resolve():
                print("FAIL: sources returned the same executable path", file=sys.stderr)
                return 1
            for source, exe, expected in (
                (source_a, info_a, "SOURCE A\n"),
                (source_b, info_b, "SOURCE B\n"),
            ):
                rc, stdout, stderr = rbltool.run_built(exe, settings=settings)
                if rc or stdout != expected:
                    print(
                        f"FAIL: {source.parent.name} ran the wrong cached program; "
                        f"exit={rc}, stdout={stdout!r}, stderr={stderr!r}",
                        file=sys.stderr,
                    )
                    return 1

            for out_dir in output_dirs:
                for suffix in (".s", ".o"):
                    artifact = out_dir / ("same_name" + suffix)
                    if not artifact.is_file():
                        print(f"FAIL: expected build artifact is missing: {artifact}", file=sys.stderr)
                        return 1

        print("PASS: same-basename sources build and run from isolated caches")
        return 0
    finally:
        for out_dir in output_dirs:
            shutil.rmtree(out_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
