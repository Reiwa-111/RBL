#!/usr/bin/env python3
"""Fetch, verify and safely stage the pinned app-local CPython runtime."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath
from typing import Dict, List, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_VERSION = "3.14.10"

PACKAGES = {
    "windows": {
        "platform": "windows-x86_64",
        "archive": "python-3.14.10-embed-amd64.zip",
        "url": "https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip",
        "sha256": "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159",
        "size": 12619664,
        "interpreter": "python.exe",
        "license_files": ["LICENSE.txt", "licenses/LICENSE.cpython.txt"],
    },
    "linux": {
        "platform": "linux-x86_64-gnu",
        "archive": "cpython-3.14.10+20261110-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz",
        "url": "https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.14.10%2B20261110-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz",
        "sha256": "c2ff2e97422f5d3896a16e0317fd212ba977bbf2688d93a6f7a03d817506bd30",
        "size": 36875876,
        "interpreter": "bin/python3",
        "glibc_min": "2.17",
        "license_files": [
            "lib/python3.14/LICENSE.txt",
            "licenses/LICENSE.cpython.txt",
            "licenses/LICENSE.python-build-standalone-MPL-2.0.txt",
        ],
    },
}

PBS_LICENSES = {
    "LICENSE.cpython.txt": {
        "url": "https://raw.githubusercontent.com/astral-sh/python-build-standalone/20261110/LICENSE.cpython.txt",
        "sha256": "86e61415828a8b5b06ec8d024e6f086ce155a8b85fd0c419c0ba4dc004e74fdd",
    },
    "LICENSE.python-build-standalone-MPL-2.0.txt": {
        "url": "https://raw.githubusercontent.com/astral-sh/python-build-standalone/20261110/LICENSE",
        "sha256": "1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5",
    },
}

SOURCE_PACKAGES = {
    "cpython": {
        "filename": "Python-3.14.10.tar.xz",
        "url": "https://www.python.org/ftp/python/3.14.10/Python-3.14.10.tar.xz",
        "sha256": "de414a57119abef52784d8993e341d53aed635fc2b05c2607d9f98a2c5b72032",
        "size": 24_240_392,
        "license": "PSF-2.0",
        "source_ref": "CPython v3.14.10",
    },
    "python_build_standalone": {
        "filename": "python-build-standalone-20261110.tar.gz",
        "url": "https://github.com/astral-sh/python-build-standalone/archive/refs/tags/20261110.tar.gz",
        "sha256": "63b8166347616283ee6d3ed449e957bc71b65368bc60945d21ab957e6fcaee49",
        "size": 470_059,
        "license": "MPL-2.0",
        "source_ref": "20261110 / 9244801782b78e4c4afc5c82a4524aa2101d3298",
    },
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_verified(url: str, destination: Path, expected_hash: str, expected_size: int = 0) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        actual_hash = sha256_file(destination)
        actual_size = destination.stat().st_size
        if actual_hash == expected_hash and (not expected_size or actual_size == expected_size):
            return destination
        raise RuntimeError(
            "Cached runtime asset does not match its pinned digest; preserve it and use a fresh cache path: "
            f"{destination} (sha256={actual_hash}, bytes={actual_size})"
        )

    fd, temporary_name = tempfile.mkstemp(prefix=destination.name + ".", suffix=".partial", dir=str(destination.parent))
    temporary = Path(temporary_name)
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "RBL-Studio-0.8.0-runtime-packager"})
        digest = hashlib.sha256()
        total = 0
        with os.fdopen(fd, "wb") as output, urllib.request.urlopen(request, timeout=90) as response:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                output.write(chunk)
                digest.update(chunk)
                total += len(chunk)
        actual_hash = digest.hexdigest()
        if actual_hash != expected_hash:
            raise RuntimeError(f"SHA-256 mismatch for {url}: expected {expected_hash}, got {actual_hash}")
        if expected_size and total != expected_size:
            raise RuntimeError(f"Size mismatch for {url}: expected {expected_size}, got {total}")
        os.replace(str(temporary), str(destination))
        return destination
    except Exception:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise


def safe_parts(name: str) -> Tuple[str, ...]:
    if not name or "\x00" in name or "\\" in name:
        raise RuntimeError(f"Unsafe archive path: {name!r}")
    path = PurePosixPath(name)
    parts = path.parts
    if path.is_absolute() or not parts or any(part in ("", ".", "..") for part in parts):
        raise RuntimeError(f"Unsafe archive path: {name!r}")
    if ":" in parts[0]:
        raise RuntimeError(f"Drive-qualified archive path is not allowed: {name!r}")
    return parts


def _target_path(destination: Path, parts: Tuple[str, ...]) -> Path:
    target = destination.joinpath(*parts)
    root = destination.resolve()
    parent = target.parent.resolve()
    try:
        parent.relative_to(root)
    except ValueError as error:
        raise RuntimeError(f"Archive path escapes extraction root: {target}") from error
    return target


def extract_zip_safely(archive_path: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    seen = set()
    with zipfile.ZipFile(str(archive_path), "r") as archive:
        for info in archive.infolist():
            name = info.filename.rstrip("/")
            if not name:
                continue
            parts = safe_parts(name)
            if parts in seen:
                raise RuntimeError(f"Duplicate ZIP member: {name}")
            seen.add(parts)
            target = _target_path(destination, parts)
            mode = info.external_attr >> 16
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if stat.S_ISLNK(mode) or (mode and not stat.S_ISREG(mode)):
                raise RuntimeError(f"Unsupported ZIP member type: {name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info, "r") as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            if os.name != "nt" and mode:
                target.chmod(mode & 0o777)


def _safe_link_target(relative_name: PurePosixPath, link_name: str) -> str:
    if not link_name or "\x00" in link_name or "\\" in link_name:
        raise RuntimeError(f"Unsafe TAR symlink target: {link_name!r}")
    link = PurePosixPath(link_name)
    if link.is_absolute() or (link.parts and ":" in link.parts[0]):
        raise RuntimeError(f"Absolute TAR symlink target is not allowed: {link_name!r}")
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative_name.as_posix()), link_name))
    if resolved in ("", ".", "..") or resolved.startswith("../") or resolved.startswith("/"):
        raise RuntimeError(f"TAR symlink escapes extraction root: {relative_name} -> {link_name}")
    return resolved


def extract_tar_safely(archive_path: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    seen = set()
    pending_links: List[Tuple[PurePosixPath, str]] = []
    member_paths: List[PurePosixPath] = []

    with tarfile.open(str(archive_path), "r:gz") as archive:
        members = archive.getmembers()
        for member in members:
            parts = safe_parts(member.name.rstrip("/"))
            if parts[0] != "python":
                raise RuntimeError(f"Unexpected top-level TAR member: {member.name}")
            relative_parts = parts[1:]
            if not relative_parts:
                if member.isdir():
                    continue
                raise RuntimeError(f"Unexpected TAR root member type: {member.name}")
            relative = PurePosixPath(*relative_parts)
            if relative in seen:
                raise RuntimeError(f"Duplicate TAR member: {member.name}")
            seen.add(relative)
            member_paths.append(relative)
            target = _target_path(destination, tuple(relative.parts))

            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                if os.path.lexists(str(target)):
                    nested = [item.as_posix() for item in member_paths if item != relative and relative in item.parents]
                    existing_kind = "directory" if target.is_dir() else "symlink" if target.is_symlink() else "file"
                    raise RuntimeError(
                        f"TAR member path collision: {member.name} already exists as {existing_kind}; "
                        f"earlier nested members={nested[:8]}"
                    )
                target.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                if source is None:
                    raise RuntimeError(f"Could not read TAR member: {member.name}")
                with source, target.open("xb") as output:
                    shutil.copyfileobj(source, output)
                if os.name != "nt":
                    target.chmod(member.mode & 0o777)
            elif member.issym():
                _safe_link_target(relative, member.linkname)
                pending_links.append((relative, member.linkname))
            else:
                raise RuntimeError(f"Unsupported TAR member type: {member.name}")

    symlink_names = {path for path, _ in pending_links}
    for path in member_paths:
        for parent in path.parents:
            if parent in symlink_names:
                raise RuntimeError(f"TAR member is nested beneath a symlink: {path}")

    for relative, link_name in pending_links:
        target = _target_path(destination, tuple(relative.parts))
        target.parent.mkdir(parents=True, exist_ok=True)
        if os.path.lexists(str(target)):
            raise RuntimeError(f"TAR symlink destination already exists: {target}")
        os.symlink(link_name, str(target))


def verify_runtime(runtime_dir: Path, platform: str) -> None:
    package = PACKAGES[platform]
    interpreter = runtime_dir.joinpath(*PurePosixPath(package["interpreter"]).parts)
    if not interpreter.is_file():
        raise RuntimeError(f"Staged CPython interpreter is missing: {interpreter}")
    if platform == "windows":
        if not (runtime_dir / "LICENSE.txt").is_file():
            raise RuntimeError("Windows embeddable runtime is missing LICENSE.txt")
        if not (runtime_dir / "licenses" / "LICENSE.cpython.txt").is_file():
            raise RuntimeError("Windows embeddable runtime is missing the full CPython third-party license inventory")
        pth = runtime_dir / "python314._pth"
        if not pth.is_file():
            raise RuntimeError("Windows embeddable runtime is missing python314._pth")
        for line in pth.read_text(encoding="utf-8").splitlines():
            if line.strip().lower() == "import site":
                raise RuntimeError("Windows embeddable runtime unexpectedly enables site imports")
        imports = "import argparse, subprocess, pathlib, hashlib, json, ssl, sqlite3, ctypes, zlib, tomllib, winreg"
    else:
        if not (runtime_dir / "lib" / "python3.14" / "LICENSE.txt").is_file():
            raise RuntimeError("Linux CPython runtime is missing its LICENSE.txt")
        if not (runtime_dir / "licenses" / "LICENSE.cpython.txt").is_file():
            raise RuntimeError("Linux CPython runtime is missing the full CPython third-party license inventory")
        if not (runtime_dir / "licenses" / "LICENSE.python-build-standalone-MPL-2.0.txt").is_file():
            raise RuntimeError("Linux CPython runtime is missing the python-build-standalone MPL license")
        imports = "import argparse, subprocess, pathlib, hashlib, json, ssl, sqlite3, ctypes, zlib, tomllib, fcntl"
    probe = (
        "import sys; assert sys.version_info[:3] == (3, 14, 10), sys.version; "
        + imports
        + "; print(sys.version.split()[0])"
    )
    env = os.environ.copy()
    if platform == "windows":
        env["PATH"] = os.environ.get("SystemRoot", r"C:\Windows") + r"\System32"
    result = subprocess.run(
        [str(interpreter), "-I", "-c", probe],
        cwd=str(runtime_dir),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        timeout=45,
    )
    if result.returncode != 0 or result.stdout.strip() != PYTHON_VERSION:
        raise RuntimeError(
            f"Staged CPython runtime failed its isolated startup test ({result.returncode}): "
            f"{result.stdout}\n{result.stderr}"
        )


def write_metadata(runtime_dir: Path, platform: str) -> None:
    package = PACKAGES[platform]
    manifest = {
        "schema_version": 1,
        "python_version": PYTHON_VERSION,
        "platform": package["platform"],
        "archive": package["archive"],
        "upstream_url": package["url"],
        "archive_size": package["size"],
        "archive_sha256": package["sha256"],
        "interpreter": package["interpreter"],
        "license_files": package["license_files"],
        "source_archives": {
            key: {
                "filename": value["filename"],
                "url": value["url"],
                "sha256": value["sha256"],
                "size": value["size"],
                "license": value["license"],
                "source_ref": value["source_ref"],
            }
            for key, value in SOURCE_PACKAGES.items()
        },
        "redistribution_review": "source and license materials included; formal legal sign-off pending",
    }
    if "glibc_min" in package:
        manifest["glibc_min"] = package["glibc_min"]
    (runtime_dir / "rbl-python-runtime.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    lines = [
        "# RBL Studio app-local CPython runtime",
        "",
        f"- CPython version: `{PYTHON_VERSION}`",
        f"- Platform: `{package['platform']}`",
        f"- Upstream archive: `{package['archive']}`",
        f"- Upstream URL: {package['url']}",
        f"- Archive size: `{package['size']}` bytes",
        f"- SHA-256: `{package['sha256']}`",
        f"- Interpreter path: `{package['interpreter']}`",
        "- The runtime is invoked app-locally; no system Python or inherited Python PATH is required.",
        "- No additional Python packages are required by the RBL backend; the backend uses the standard library.",
        "- Corresponding source archives for CPython and python-build-standalone are staged under `source/` with SHA-256 sidecars.",
    ]
    if "glibc_min" in package:
        lines.extend([
            f"- Linux ABI baseline: glibc `{package['glibc_min']}` or newer; target is x86-64 GNU/Linux, not musl/Alpine.",
            "- The Python build distribution's MPL-2.0 and CPython PSF license texts are preserved under `licenses/`.",
        ])
    else:
        lines.append("- The upstream embeddable ZIP `LICENSE.txt` and the full CPython/third-party license inventory under `licenses/` are retained; `python314._pth` remains isolated with `site` disabled.")
    lines.extend([
        "",
        "This inventory records provenance and hashes; it is not legal approval for redistribution.",
        "",
    ])
    (runtime_dir / "UPSTREAM.md").write_text("\n".join(lines), encoding="utf-8")


def _stage_source_archives(runtime_dir: Path, cache_dir: Path) -> None:
    source_dir = runtime_dir / "source"
    source_dir.mkdir(parents=True, exist_ok=True)
    for metadata in SOURCE_PACKAGES.values():
        filename = metadata["filename"]
        archive = download_verified(
            metadata["url"], cache_dir / filename, metadata["sha256"], metadata["size"]
        )
        destination = source_dir / filename
        if destination.exists():
            if destination.stat().st_size != metadata["size"] or sha256_file(destination) != metadata["sha256"]:
                raise RuntimeError(f"existing corresponding-source archive failed integrity check: {destination}")
        else:
            shutil.copy2(archive, destination)
        checksum_path = source_dir / f"{filename}.sha256"
        checksum_path.write_text(metadata["sha256"] + f"  {filename}\n", encoding="ascii")


def _verify_source_archives(runtime_dir: Path) -> None:
    source_dir = runtime_dir / "source"
    for metadata in SOURCE_PACKAGES.values():
        filename = metadata["filename"]
        archive = source_dir / filename
        checksum_path = source_dir / f"{filename}.sha256"
        if not archive.is_file() or not checksum_path.is_file():
            raise RuntimeError(f"corresponding-source archive or checksum is missing: {archive}")
        if archive.stat().st_size != metadata["size"] or sha256_file(archive) != metadata["sha256"]:
            raise RuntimeError(f"corresponding-source archive failed its pinned size/hash check: {archive}")
        expected_line = metadata["sha256"] + f"  {filename}"
        if checksum_path.read_text(encoding="ascii").strip() != expected_line:
            raise RuntimeError(f"corresponding-source checksum sidecar does not match: {checksum_path}")


def _copy_license_files(runtime_dir: Path, platform: str, cache_dir: Path) -> None:
    licenses_dir = runtime_dir / "licenses"
    licenses_dir.mkdir(parents=True, exist_ok=True)
    names = ["LICENSE.cpython.txt"]
    if platform == "linux":
        names.append("LICENSE.python-build-standalone-MPL-2.0.txt")
    for name in names:
        metadata = PBS_LICENSES[name]
        license_path = download_verified(metadata["url"], cache_dir / name, metadata["sha256"])
        shutil.copy2(str(license_path), str(licenses_dir / name))


def _verify_existing_linux_package(output: Path, cache_dir: Path) -> None:
    package = PACKAGES["linux"]
    archive_path = output / "python-runtime.tar.gz"
    if not archive_path.is_file():
        raise RuntimeError(f"Linux runtime archive is missing: {archive_path}")
    if archive_path.stat().st_size != package["size"] or sha256_file(archive_path) != package["sha256"]:
        raise RuntimeError(f"Linux runtime archive failed its pinned size/hash check: {archive_path}")
    for relative in ("rbl-python-runtime.json", "UPSTREAM.md", "python-runtime.sha256", "licenses/LICENSE.cpython.txt", "licenses/LICENSE.python-build-standalone-MPL-2.0.txt"):
        if not (output / relative).is_file():
            raise RuntimeError(f"Linux runtime sidecar is missing: {output / relative}")
    staging = Path(tempfile.mkdtemp(prefix="rbl-python-runtime-verify-"))
    try:
        extract_tar_safely(archive_path, staging)
        _copy_license_files(staging, "linux", cache_dir)
        verify_runtime(staging, "linux")
    finally:
        shutil.rmtree(str(staging), ignore_errors=True)


def stage_runtime(platform: str, output: Path, cache_dir: Path) -> None:
    package = PACKAGES[platform]
    output = output.resolve()
    cache_dir.mkdir(parents=True, exist_ok=True)

    if output.exists():
        manifest_path = output / "rbl-python-runtime.json"
        if not manifest_path.is_file():
            raise RuntimeError(f"Refusing to overwrite unmanaged runtime directory: {output}")
        current = json.loads(manifest_path.read_text(encoding="utf-8"))
        if current.get("platform") != package["platform"] or current.get("archive_sha256") != package["sha256"]:
            raise RuntimeError(f"Runtime directory already contains a different platform/version: {output}")
        if platform == "windows":
            verify_runtime(output, platform)
        else:
            _verify_existing_linux_package(output, cache_dir)
        _verify_source_archives(output)
        print(f"CPython {PYTHON_VERSION} already staged: {output}")
        return

    archive_path = download_verified(
        package["url"], cache_dir / package["archive"], package["sha256"], package["size"]
    )
    output.parent.mkdir(parents=True, exist_ok=True)

    if platform == "windows":
        staging = Path(tempfile.mkdtemp(prefix=".python-runtime-stage-", dir=str(output.parent)))
        try:
            extract_zip_safely(archive_path, staging)
            _copy_license_files(staging, platform, cache_dir)
            verify_runtime(staging, platform)
            write_metadata(staging, platform)
            _stage_source_archives(staging, cache_dir)
            os.replace(str(staging), str(output))
        except Exception:
            shutil.rmtree(str(staging), ignore_errors=True)
            raise
    else:
        # The upstream Linux archive contains case-sensitive terminfo names.
        # Validate/extract it on the Linux filesystem (normally /tmp), never on
        # a Windows-mounted checkout such as /mnt/c or /mnt/q.
        runtime_staging = Path(tempfile.mkdtemp(prefix="rbl-python-runtime-"))
        sidecar_staging = Path(tempfile.mkdtemp(prefix=".python-runtime-package-", dir=str(output.parent)))
        try:
            extract_tar_safely(archive_path, runtime_staging)
            _copy_license_files(runtime_staging, platform, cache_dir)
            verify_runtime(runtime_staging, platform)
            write_metadata(runtime_staging, platform)
            _stage_source_archives(runtime_staging, cache_dir)

            shutil.copy2(str(archive_path), str(sidecar_staging / "python-runtime.tar.gz"))
            shutil.copy2(str(runtime_staging / "rbl-python-runtime.json"), str(sidecar_staging / "rbl-python-runtime.json"))
            shutil.copy2(str(runtime_staging / "UPSTREAM.md"), str(sidecar_staging / "UPSTREAM.md"))
            shutil.copytree(str(runtime_staging / "licenses"), str(sidecar_staging / "licenses"))
            shutil.copytree(str(runtime_staging / "source"), str(sidecar_staging / "source"))
            (sidecar_staging / "python-runtime.sha256").write_text(
                package["sha256"] + "  python-runtime.tar.gz\n", encoding="ascii"
            )
            os.replace(str(sidecar_staging), str(output))
        except Exception:
            shutil.rmtree(str(sidecar_staging), ignore_errors=True)
            raise
        finally:
            shutil.rmtree(str(runtime_staging), ignore_errors=True)

    print(f"Staged CPython {PYTHON_VERSION} ({package['platform']}): {output}")
    print(f"Archive SHA-256: {package['sha256']}")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=sorted(PACKAGES), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=REPO_ROOT / "build" / "cache" / "python-runtime",
    )
    args = parser.parse_args()
    try:
        stage_runtime(args.platform, args.output, args.cache_dir)
    except Exception as error:
        parser.exit(1, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
