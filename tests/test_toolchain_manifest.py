#!/usr/bin/env python3
"""Check that the Windows toolchain manifest covers the complete payload."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_windows_toolchain", ROOT / "tools" / "package_windows_toolchain.py"
)
assert SPEC and SPEC.loader
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


def write_bundle(root: Path) -> None:
    payload = root / "bin" / "gcc.exe"
    payload.parent.mkdir(parents=True)
    payload.write_bytes(b"test-gcc")
    manifest = {
        "schema": 1,
        "packages": [
            {
                "id": name,
                "version": version,
                "source_archive": f"https://mirror.msys2.org/mingw/sources/{archive}",
            }
            for name, version, archive in PACKAGER.PACKAGE_SOURCES
        ],
        "files": {
            "bin/gcc.exe": {
                "size": payload.stat().st_size,
                "sha256": hashlib.sha256(payload.read_bytes()).hexdigest(),
            }
        },
        "file_count": 1,
        "total_bytes": payload.stat().st_size,
    }
    (root / "rbl-toolchain-manifest.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )


def expect_invalid(root: Path, label: str) -> None:
    try:
        PACKAGER.verify_manifest(root)
    except RuntimeError:
        return
    raise AssertionError(f"manifest verifier accepted {label}")


def main() -> int:
    bundled = ROOT / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64"
    manifest = PACKAGER.verify_manifest(bundled)
    if manifest["file_count"] != 43:
        raise AssertionError(f"unexpected bundled payload count: {manifest['file_count']}")

    with tempfile.TemporaryDirectory(prefix="rbl-manifest-test-") as temp:
        root = Path(temp)
        write_bundle(root)
        PACKAGER.verify_manifest(root)

        (root / "extra.dll").write_bytes(b"untracked")
        expect_invalid(root, "an unmanifested extra file")
        (root / "extra.dll").unlink()

        payload = root / "bin" / "gcc.exe"
        payload.write_bytes(b"tampered")
        expect_invalid(root, "a payload with a mismatched hash")
        payload.write_bytes(b"test-gcc")

        manifest_path = root / "rbl-toolchain-manifest.json"
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        data["files"]["../outside"] = {"size": 0, "sha256": "0" * 64}
        manifest_path.write_text(json.dumps(data), encoding="utf-8")
        expect_invalid(root, "a path-traversal manifest entry")

    print("PASS: toolchain manifest validates hashes, safe paths, counts and exact payload inventory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
