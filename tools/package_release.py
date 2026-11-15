#!/usr/bin/env python3
"""Assemble a portable, self-contained RBL Studio release archive.

The input must be an installer *portable staging* directory: a staged
application tree produced by ``install_windows.ps1 -Portable`` or
``install_linux.sh --portable``. This tool refuses to package a build tree,
an installed copy with machine-specific state, or a tree that still contains
compiler/runtime caches.

Outputs (in ``--output``, default ``build/output``):

* ``RBLStudio-<version>-<platform>.zip`` (Windows) or ``.tar.zst`` (Linux)
* ``SHA256SUMS`` covering the archive(s) produced in this run
* ``RBLStudio-<version>-<platform>.json`` local candidate metadata
* ``RBLStudio-<version>-<platform>-LOCAL_CANDIDATE.txt`` human note

Nothing is published, uploaded or registered.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = REPO_ROOT / "build" / "output"
PLATFORMS = {"windows": "windows-x86_64", "linux": "linux-x86_64"}

REQUIRED = (
    "VERSION",
    "rbl.py",
    "LICENSE",
    "README.md",
    "RELEASE_MANIFEST.txt",
    "THIRD_PARTY_NOTICES.txt",
    "runtime/python",
    "toolchain",
    "source",
    "licenses",
    "examples",
    "tests",
    "benchmarks",
)

PLATFORM_REQUIRED = {
    "windows": (
        "rbl-studio-wx.exe",
        "rbl-studio-worker.exe",
        "rbl.cmd",
        "RBLStudio.cmd",
        "RBLStudio.vbs",
        "runtime/python/python.exe",
        "toolchain/windows-x86_64/msys64/ucrt64/bin/as.exe",
        "toolchain/windows-x86_64/msys64/ucrt64/x86_64-w64-mingw32/bin/ld.exe",
        "toolchain/windows-x86_64/msys64/ucrt64/rbl-toolchain-manifest.json",
    ),
    "linux": (
        "rbl-studio-wx",
        "rbl-studio-worker",
        "rbl",
        "RBLStudio.sh",
        "runtime/python/bin/python3",
        "toolchain/linux-x86_64/bin/clang",
        "toolchain/linux-x86_64/bin/ld.lld",
        "toolchain/linux-x86_64/manifest.json",
    ),
}

# Machine-specific install state and build-tree leftovers must never ship.
FORBIDDEN_NAMES = {
    ".rblstudio-install.json",
    ".rblstudio-install-state",
    ".rblstudio-install-files",
    "Uninstall-RBLStudio.ps1",
    "Uninstall-RBLStudio.sh",
    "CMakeCache.txt",
    "CTestTestfile.cmake",
    "DartConfiguration.tcl",
    "Makefile",
    "cmake_install.cmake",
}
FORBIDDEN_DIRS = {"CMakeFiles", "Testing", "wxwidgets-build", "wxwidgets-install", "build"}


class PackagingError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate(staged: Path, platform: str) -> None:
    if not staged.is_dir():
        raise PackagingError(f"staged directory does not exist: {staged}")
    for relative in REQUIRED + PLATFORM_REQUIRED[platform]:
        if not (staged / relative).exists():
            raise PackagingError(f"staged tree is missing required entry: {relative}")

    problems: list[str] = []
    root_entries = {path.name for path in staged.iterdir()}
    for name in sorted(FORBIDDEN_NAMES & root_entries):
        problems.append(f"machine-specific or build-only entry present at the package root: {name}")
    for name in sorted(FORBIDDEN_DIRS & root_entries):
        problems.append(f"build/install state directory present at the package root: {name}")
    for path in staged.rglob("*"):
        if path.is_file() and path.name.lower().endswith((".pdb", ".dmp", ".ilk", ".idb", ".stackdump")):
            problems.append(f"debug/crash artifact present: {path.relative_to(staged).as_posix()}")
    if problems:
        shown = "\n  ".join(problems[:20])
        raise PackagingError("refusing to package an unclean staged tree:\n  " + shown)


def count_files(root: Path) -> int:
    return sum(1 for path in root.rglob("*") if path.is_file() or path.is_symlink())


def zip_timestamp(path: Path) -> tuple[int, int, int, int, int, int]:
    # Reproducible source archives may carry epoch/1970 timestamps; ZIP cannot
    # represent anything before 1980.
    return time.localtime(max(path.stat().st_mtime, 315532800))[:6]


def make_zip(staged: Path, archive: Path, root_name: str) -> None:
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for dirpath, dirnames, filenames in os.walk(staged):
            dirnames.sort()
            rel_dir = Path(dirpath).relative_to(staged)
            if rel_dir != Path("."):
                info = zipfile.ZipInfo(
                    (Path(root_name) / rel_dir).as_posix().rstrip("/") + "/",
                    zip_timestamp(staged / rel_dir),
                )
                info.external_attr = (0o40755 << 16) | 0x10
                zf.writestr(info, b"")
            for name in sorted(filenames):
                source = Path(dirpath) / name
                if source.is_symlink():
                    raise PackagingError(f"Windows staging must not contain symlinks: {source}")
                info = zipfile.ZipInfo(
                    (Path(root_name) / rel_dir / name).as_posix(),
                    zip_timestamp(source),
                )
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (source.stat().st_mode & 0xFFFF) << 16
                with source.open("rb") as handle, zf.open(info, "w") as target:
                    shutil.copyfileobj(handle, target, 1024 * 1024)


def make_tar_zst(staged: Path, archive: Path, root_name: str) -> None:
    zstd = shutil.which("zstd")
    tar = shutil.which("tar")
    if not zstd or not tar:
        raise PackagingError("Linux packaging requires GNU tar and zstd on PATH")
    # GNU tar preserves modes and symlinks; zstd is streamed so no temporary
    # uncompressed tar is left behind.
    command = [
        tar,
        "--sort=name",
        "--owner=0",
        "--group=0",
        "--numeric-owner",
        "-cf",
        "-",
        "-C",
        str(staged.parent),
        root_name,
    ]
    with archive.open("wb") as out:
        tar_process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert tar_process.stdout is not None
        zstd_process = subprocess.Popen(
            [zstd, "-q", "-19", "-T0", "-c"],
            stdin=tar_process.stdout,
            stdout=out,
            stderr=subprocess.PIPE,
        )
        tar_process.stdout.close()
        _, zstd_error = zstd_process.communicate()
        tar_error = tar_process.stderr.read() if tar_process.stderr else b""
        tar_process.wait()
        if tar_process.returncode:
            raise PackagingError(f"tar failed: {tar_error.decode('utf-8', 'replace')}")
        if zstd_process.returncode:
            raise PackagingError(f"zstd failed: {zstd_error.decode('utf-8', 'replace')}")


def read_version(staged: Path) -> str:
    version = (staged / "VERSION").read_text(encoding="utf-8").strip()
    if not version or any(part == "" for part in version.split(".")):
        raise PackagingError(f"invalid VERSION in staged tree: {version!r}")
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    parser.add_argument("--staged", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--name", help="archive base name (default: RBLStudio-<version>-<platform>)")
    args = parser.parse_args()

    staged = args.staged.resolve(strict=True)
    platform = PLATFORMS[args.platform]
    validate(staged, args.platform)
    version = read_version(staged)

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base = args.name or f"RBLStudio-{version}-{platform}"
    extension = ".zip" if args.platform == "windows" else ".tar.zst"
    archive = output / (base + extension)
    if archive.exists():
        raise PackagingError(f"refusing to overwrite an existing archive: {archive}")

    files = count_files(staged)
    print(f"Packaging {staged} -> {archive} ({files} files)")
    if args.platform == "windows":
        make_zip(staged, archive, base)
    else:
        make_tar_zst(staged, archive, base)

    digest = sha256_file(archive)
    size = archive.stat().st_size
    sums = output / "SHA256SUMS"
    previous = sums.read_text(encoding="utf-8").splitlines() if sums.is_file() else []
    previous = [line for line in previous if line.split("  ", 1)[-1] != archive.name]
    previous.append(f"{digest}  {archive.name}")
    sums.write_text("\n".join(sorted(previous)) + "\n", encoding="utf-8", newline="\n")

    metadata = {
        "schema": "rbl-release-candidate/1",
        "status": "LOCAL PREBUILT CANDIDATE - NOT PUBLISHED",
        "version": version,
        "platform": platform,
        "archive": archive.name,
        "archive_size": size,
        "archive_sha256": digest,
        "file_count": files,
        "staged_from": str(staged),
        "repository": str(REPO_ROOT),
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "notes": [
            "Assembled from an installer portable staging directory, not from a build tree.",
            "Install manifest, uninstaller and desktop/registry integration are intentionally absent.",
            "Not published, uploaded or tagged; release-page disclosure and legal sign-off remain open.",
            "RELEASE_MANIFEST.txt still records the repository's historical release status.",
        ],
    }
    (output / (base + ".json")).write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    note = (
        f"{base}\n"
        + "=" * len(base)
        + "\n\n"
        "LOCAL PREBUILT CANDIDATE - NOT A PUBLISHED RELEASE\n\n"
        f"Version: {version}\nPlatform: {platform}\nFiles: {files}\n"
        f"SHA-256: {digest}\nSize: {size} bytes\n\n"
        "This archive contains a ready-to-run RBL Studio application: GUI, Rust\n"
        "worker, app-local Python and the assembler/linker toolchain it needs.\n"
        "Extract it and start RBLStudio.cmd (Windows) or RBLStudio.sh (Linux).\n"
        "No compiler, MSYS2, Python or RBL environment variable is required.\n\n"
        "It was not published, uploaded or attached to a release. Corresponding\n"
        "source archives are included under source/; release-page disclosure and\n"
        "formal legal sign-off remain open.\n"
    )
    (output / (base + "-LOCAL_CANDIDATE.txt")).write_text(note, encoding="utf-8")

    print(f"Archive: {archive}")
    print(f"SHA-256: {digest}")
    print(f"Size:    {size} bytes")
    print(f"Files:   {files}")
    print(f"Sums:    {sums}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PackagingError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
