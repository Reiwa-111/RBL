#!/usr/bin/env python3
"""Create the exact Rust-worker corresponding-source bundle from Cargo cache."""
from __future__ import annotations

import argparse
import hashlib
import io
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from verify_rust_source_bundle import (LIVE_SOURCE_FILES, cargo_closure,
                                       cargo_lock_registry_checksums,
                                       normalized_notice)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--zstd", required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    active = set()
    for target in ("x86_64-pc-windows-gnu", "x86_64-unknown-linux-gnu"):
        active |= cargo_closure(root, target)
    checksums = cargo_lock_registry_checksums(root)
    cache = Path.home() / ".cargo" / "registry" / "cache"
    crates: dict[str, bytes] = {}
    for name, version in sorted(active):
        matches = list(cache.glob(f"*/*{name}-{version}.crate"))
        if len(matches) != 1:
            raise RuntimeError(f"expected one cached crate for {name} {version}, found {len(matches)}")
        data = matches[0].read_bytes()
        if hashlib.sha256(data).hexdigest() != checksums[(name, version)]:
            raise RuntimeError(f"Cargo cache checksum mismatch: {name} {version}")
        crates[f"crates/{name}-{version}.crate"] = data
    files = {path: (root / path).read_bytes() for path in LIVE_SOURCE_FILES}
    files["native/wx-ui/RUST_DEPENDENCY_NOTICES.md"] = normalized_notice(
        (root / "native/wx-ui/RUST_DEPENDENCY_NOTICES.md").read_bytes())
    files.update(crates)
    manifest = "".join(f"{hashlib.sha256(data).hexdigest()}  {path}\n" for path, data in sorted(files.items()))
    files["SHA256SUMS.txt"] = manifest.encode("ascii")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rbl-rust-source-") as temp:
        tar_path = Path(temp) / "source.tar"
        with tarfile.open(tar_path, "w") as archive:
            for path, data in sorted(files.items()):
                info = tarfile.TarInfo(f"RBL-0.8.0-Rust-Worker-Source-Materials/{path}")
                info.size = len(data)
                info.mtime = 0
                archive.addfile(info, io.BytesIO(data))
        subprocess.run([args.zstd, "-q", "-f", str(tar_path), "-o", str(args.output)], check=True)
    print(f"Created {args.output} ({len(active)} crates).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
