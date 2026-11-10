#!/usr/bin/env python3
from __future__ import annotations

import contextlib
import io
import tempfile
import sys
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rbl


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rbl-clean-test-") as temp:
        root = Path(temp)
        build = root / "build"
        cache = build / "sources"
        cache.mkdir(parents=True)
        (cache / "compiler-output.bin").write_bytes(b"generated")
        preserved = {
            "verification": build / "verification" / "evidence.log",
            "source-material": build / "source-material" / "sources.tar.zst",
            "release-candidate": build / "wx-ui-win-rc" / "app.exe",
            "visualizations": build / "visualizations" / "plot.png",
        }
        for path in preserved.values():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"preserve")

        with mock.patch.object(rbl.rbltool, "BUILD_DIR", build):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                rc = rbl.clean(root)
        if rc != 0 or cache.exists():
            raise SystemExit("FAIL: compiler cache was not removed")
        for label, path in preserved.items():
            if path.read_bytes() != b"preserve":
                raise SystemExit(f"FAIL: {label} data was modified")
        print("PASS: clean removes only build/sources and preserves evidence/source/RC/user data")

    with tempfile.TemporaryDirectory(prefix="rbl-clean-link-test-") as temp:
        root = Path(temp)
        build = root / "build"
        outside = root / "outside"
        outside.mkdir()
        sentinel = outside / "keep.txt"
        sentinel.write_text("keep", encoding="utf-8")
        build.mkdir()
        link = build / "sources"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError):
            print("SKIP: symlink refusal test unavailable")
            return 0
        with mock.patch.object(rbl.rbltool, "BUILD_DIR", build):
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                rc = rbl.clean(root)
        if rc == 0 or not sentinel.exists() or sentinel.read_text(encoding="utf-8") != "keep":
            raise SystemExit("FAIL: clean followed a symlinked cache")
        print("PASS: clean refuses a symlinked cache and preserves its target")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
