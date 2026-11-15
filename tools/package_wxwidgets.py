#!/usr/bin/env python3
"""Fetch and safely stage the pinned wxWidgets source used by Linux builds."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath


VERSION = "3.2.4"
ARCHIVE_NAME = f"wxWidgets-{VERSION}.tar.bz2"
URL = f"https://github.com/wxWidgets/wxWidgets/releases/download/v{VERSION}/{ARCHIVE_NAME}"
SHA256 = "0640e1ab716db5af2ecb7389dbef6138d7679261fbff730d23845ba838ca133e"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_member_name(name: str) -> PurePosixPath:
    if "\\" in name or name.startswith("/") or (len(name) >= 2 and name[1] == ":"):
        raise ValueError(f"unsafe archive path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"unsafe archive path: {name!r}")
    return path


def _safe_symlink_target(member_path: PurePosixPath, target: str) -> PurePosixPath:
    if not target or "\\" in target or target.startswith("/") or (len(target) >= 2 and target[1] == ":"):
        raise ValueError(f"unsafe symlink target for {member_path}: {target!r}")
    parts = list(member_path.parent.parts)
    for part in PurePosixPath(target).parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                raise ValueError(f"symlink escapes archive root: {member_path} -> {target}")
            parts.pop()
        else:
            parts.append(part)
    if not parts:
        raise ValueError(f"symlink resolves to archive root: {member_path} -> {target}")
    return PurePosixPath(*parts)


def safe_extract_tar_bz2(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    seen: set[str] = set()
    symlinks: list[tuple[PurePosixPath, str]] = []
    with tarfile.open(archive, mode="r:bz2") as tar:
        members = tar.getmembers()
        for member in members:
            rel = _safe_member_name(member.name.rstrip("/"))
            key = rel.as_posix()
            if key in seen:
                raise ValueError(f"duplicate archive member: {key}")
            seen.add(key)
            if member.isdir():
                continue
            if member.isfile():
                continue
            if member.issym():
                _safe_symlink_target(rel, member.linkname)
                symlinks.append((rel, member.linkname))
                continue
            raise ValueError(f"unsupported archive member type: {key}")

        for member in members:
            rel = _safe_member_name(member.name.rstrip("/"))
            target = root.joinpath(*rel.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = tar.extractfile(member)
            if source is None:
                raise ValueError(f"unable to read archive member: {member.name}")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(0o755 if member.mode & 0o111 else 0o644)

        for rel, link_target in symlinks:
            target = root.joinpath(*rel.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            resolved = root.joinpath(*_safe_symlink_target(rel, link_target).parts)
            if not resolved.exists():
                raise ValueError(f"dangling source archive symlink: {rel} -> {link_target}")
            target.symlink_to(link_target)


def download_archive(cache_dir: Path) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    archive = cache_dir / ARCHIVE_NAME
    if archive.exists():
        actual = sha256_file(archive)
        if actual != SHA256:
            raise RuntimeError(f"cached wxWidgets archive hash mismatch; preserved file: {archive} ({actual})")
        return archive

    fd, temporary_name = tempfile.mkstemp(prefix=f".{ARCHIVE_NAME}.", suffix=".part", dir=cache_dir)
    os.close(fd)
    temporary = Path(temporary_name)
    try:
        request = urllib.request.Request(URL, headers={"User-Agent": "RBLStudio-source-packager/1.0"})
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
            output.flush()
            os.fsync(output.fileno())
        actual = sha256_file(temporary)
        if actual != SHA256:
            raise RuntimeError(f"downloaded wxWidgets archive hash mismatch: {actual}")
        os.replace(temporary, archive)
    finally:
        if temporary.exists():
            temporary.unlink()
    return archive


def package(cache_dir: Path, output_dir: Path) -> Path:
    archive = download_archive(cache_dir)
    if output_dir.exists():
        manifest = output_dir / "wxwidgets-source.json"
        if manifest.is_file():
            data = json.loads(manifest.read_text(encoding="utf-8"))
            if data.get("archive_sha256") == SHA256 and (output_dir / f"wxWidgets-{VERSION}").is_dir():
                return output_dir / f"wxWidgets-{VERSION}"
        raise RuntimeError(f"refusing to overwrite existing wxWidgets source staging directory: {output_dir}")

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.", dir=output_dir.parent))
    try:
        safe_extract_tar_bz2(archive, temporary)
        source_root = temporary / f"wxWidgets-{VERSION}"
        if not source_root.is_dir() or not (source_root / "configure").is_file() or not (source_root / "docs" / "licence.txt").is_file():
            raise RuntimeError("wxWidgets source archive layout or license file is unexpected")
        (temporary / "wxwidgets-source.json").write_text(
            json.dumps({
                "component": "wxWidgets",
                "version": VERSION,
                "url": URL,
                "archive": ARCHIVE_NAME,
                "archive_sha256": SHA256,
                "license": "wxWindows Library Licence 3.1 (LGPL-based linking exception)",
                "source_root": f"wxWidgets-{VERSION}",
            }, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, output_dir)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return output_dir / f"wxWidgets-{VERSION}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("build/cache/wxwidgets-3.2.4"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = package(args.cache.resolve(), args.output.resolve())
    print(source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
