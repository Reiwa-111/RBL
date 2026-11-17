#!/usr/bin/env python3
"""Prepare a self-contained RBL Studio Flatpak payload from a verified Linux build."""
from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.package_python_runtime import extract_tar_safely  # noqa: E402


APP_ID = "com.reiwa.rblstudio"
SCHEMA_VERSION = 1


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"required source directory is missing: {source}")
    shutil.copytree(
        source,
        destination,
        symlinks=True,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise RuntimeError(f"required source file is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def verify_payload_symlinks(root: Path) -> None:
    resolved_root = root.resolve()
    for path in root.rglob("*"):
        if not path.is_symlink():
            continue
        try:
            target = path.resolve(strict=True)
            target.relative_to(resolved_root)
        except (OSError, ValueError) as error:
            raise RuntimeError(f"Flatpak payload contains a dangling or escaping symlink: {path}") from error


def write_payload_checksums(root: Path) -> None:
    entries = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        relative = path.relative_to(root).as_posix()
        if "\n" in relative or "\\" in relative:
            raise RuntimeError(f"unsupported filename in Flatpak payload: {relative!r}")
        entries.append(f"{sha256_file(path)}  {relative}")
    if not entries:
        raise RuntimeError("refusing to create an empty Flatpak payload checksum manifest")
    (root / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="utf-8")


def require_case_sensitive_parent(parent: Path) -> None:
    parent.mkdir(parents=True, exist_ok=True)
    probe = Path(tempfile.mkdtemp(prefix=".rbl-flatpak-case-probe-", dir=str(parent)))
    try:
        lower = probe / "rblCaseProbe"
        upper = probe / "RBLCASPROBE"
        lower.write_text("case-sensitive filesystem probe", encoding="ascii")
        if upper.exists():
            raise RuntimeError(
                f"Flatpak payload output must be on a case-sensitive filesystem; "
                f"the selected filesystem aliases {lower.name} and {upper.name}: {parent}"
            )
    finally:
        shutil.rmtree(str(probe), ignore_errors=True)


def _write_launchers(output: Path) -> None:
    gui = r'''#!/bin/sh
set -eu
APP_ROOT=/app/share/rbl-studio
export RBL_STUDIO_ROOT="$APP_ROOT"
export PYTHONDONTWRITEBYTECODE=1
export RBL_STUDIO_WORKER=/app/bin/rbl-studio-worker
export RBL_LINUX_TOOLCHAIN_ROOT="$APP_ROOT/toolchain/linux-x86_64"
export RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1
export RBL_BUILD_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/RBLStudio/build"
export RBL_TOOLCHAIN=native
export GTK_USE_PORTAL=1
export LD_LIBRARY_PATH="$APP_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$RBL_LINUX_TOOLCHAIN_ROOT/bin:$PATH"
exec /app/bin/rbl-studio-wx "$@"
'''
    cli = r'''#!/bin/sh
set -eu
APP_ROOT=/app/share/rbl-studio
export RBL_STUDIO_ROOT="$APP_ROOT"
export PYTHONDONTWRITEBYTECODE=1
export RBL_LINUX_TOOLCHAIN_ROOT="$APP_ROOT/toolchain/linux-x86_64"
export RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1
export RBL_BUILD_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/RBLStudio/build"
export RBL_TOOLCHAIN=native
export LD_LIBRARY_PATH="$APP_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$RBL_LINUX_TOOLCHAIN_ROOT/bin:$PATH"
exec "$APP_ROOT/runtime/python/bin/python3" -B "$APP_ROOT/rbl.py" "$@"
'''
    (output / "rblstudio-launcher").write_text(gui, encoding="utf-8", newline="\n")
    (output / "rbl-cli").write_text(cli, encoding="utf-8", newline="\n")
    (output / "rblstudio-launcher").chmod(0o755)
    (output / "rbl-cli").chmod(0o755)
    desktop = f'''[Desktop Entry]
Type=Application
Name=RBL Studio
Comment=IDE for Reiwa Bat Language
Exec=rblstudio %F
Icon={APP_ID}
Terminal=false
Categories=Development;IDE;
MimeType=application/x-reiwa-rbl;
StartupNotify=true
'''
    (output / f"{APP_ID}.desktop").write_text(desktop, encoding="utf-8", newline="\n")


def prepare(build_dir: Path, output: Path) -> Path:
    build_dir = build_dir.resolve()
    output = output.resolve()
    require_case_sensitive_parent(output.parent)
    if output.exists():
        marker = output / ".rbl-flatpak-payload.json"
        if not marker.is_file():
            raise RuntimeError(f"refusing to overwrite unmanaged Flatpak payload directory: {output}")
        raise RuntimeError(f"managed Flatpak payload already exists; choose a fresh output path: {output}")

    required_build = [
        build_dir / "rbl-studio-wx",
        build_dir / "rbl-studio-worker",
        build_dir / "THIRD_PARTY_NOTICES.txt",
        build_dir / "LINUX_ABI_AUDIT.txt",
        build_dir / "PYTHON_RUNTIME_ELF_AUDIT.txt",
        build_dir / "lib",
        build_dir / "toolchain" / "linux-x86_64" / "bin" / "clang",
        build_dir / "toolchain" / "linux-x86_64" / "bin" / "ld.lld",
        build_dir / "toolchain" / "linux-x86_64" / "manifest.json",
        build_dir / "runtime" / "python" / "python-runtime.tar.gz",
        build_dir / "runtime" / "python" / "rbl-python-runtime.json",
        build_dir / "licenses",
    ]
    for path in required_build:
        if not path.exists():
            raise RuntimeError(f"Linux build payload is incomplete: {path}")

    llvm_manifest = json.loads((build_dir / "toolchain/linux-x86_64/manifest.json").read_text(encoding="utf-8"))
    if llvm_manifest.get("version") != "23.1.5":
        raise RuntimeError("unexpected app-local LLVM version")
    if llvm_manifest.get("signature_verified") is not True:
        raise RuntimeError("app-local LLVM toolchain has no verified upstream GPG signature")
    llvm_root = build_dir / "toolchain/linux-x86_64"
    llvm_checksum_lines = (llvm_root / "SHA256SUMS").read_text(encoding="ascii").splitlines()
    if not llvm_checksum_lines:
        raise RuntimeError("app-local LLVM toolchain SHA256SUMS is empty")
    for line in llvm_checksum_lines:
        fields = line.split(maxsplit=1)
        if len(fields) != 2:
            raise RuntimeError("malformed LLVM SHA256SUMS entry")
        relative = Path(fields[1].lstrip("* "))
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("unsafe path in LLVM SHA256SUMS")
        candidate = llvm_root / relative
        if not candidate.is_file() or sha256_file(candidate) != fields[0]:
            raise RuntimeError(f"LLVM toolchain file failed SHA-256 verification: {relative}")
    if sha256_file(build_dir / "toolchain/linux-x86_64/bin/llvm") != llvm_manifest["binary_sha256"]["bin/llvm"]:
        raise RuntimeError("app-local LLVM binary failed its manifest checksum")

    python_archive = build_dir / "runtime/python/python-runtime.tar.gz"
    python_manifest = json.loads((build_dir / "runtime/python/rbl-python-runtime.json").read_text(encoding="utf-8"))
    if sha256_file(python_archive) != python_manifest.get("archive_sha256"):
        raise RuntimeError("CPython standalone runtime archive failed its manifest checksum")
    if python_manifest.get("python_version") != "3.14.10":
        raise RuntimeError("unexpected app-local CPython version")

    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_name(f".{output.name}.staging")
    if staging.exists():
        raise RuntimeError(f"refusing to overwrite stale staging directory: {staging}")
    staging.mkdir()
    try:
        for name in (
            "tools", "compiler", "tests", "benchmarks", "examples",
            "docs", "assets", "rbl-ide",
        ):
            copy_tree(ROOT / name, staging / name)
        # The source checkout's runtime/python directory is the Windows
        # app-local runtime. Flatpak must extract the verified Linux archive
        # below instead of copying those Windows binaries into the sandbox.
        shutil.copytree(
            ROOT / "runtime",
            staging / "runtime",
            symlinks=True,
            ignore=shutil.ignore_patterns("python", "__pycache__", "*.pyc"),
        )
        copy_tree(ROOT / "bin/linux-x86_64", staging / "bin/linux-x86_64")
        copy_tree(build_dir / "lib", staging / "lib")
        copy_tree(build_dir / "toolchain/linux-x86_64", staging / "toolchain/linux-x86_64")
        copy_tree(build_dir / "licenses", staging / "licenses")

        for name in ("VERSION", "rbl.py", "rbl", "rbl.toml", "Cargo.toml", "Cargo.lock", "Makefile", "README.md", "LICENSE", "RELEASE_MANIFEST.txt"):
            source = ROOT / name
            if source.is_file():
                copy_file(source, staging / name)
        copy_file(build_dir / "THIRD_PARTY_NOTICES.txt", staging / "THIRD_PARTY_NOTICES.txt")
        copy_file(build_dir / "LINUX_ABI_AUDIT.txt", staging / "LINUX_ABI_AUDIT.txt")
        copy_file(build_dir / "PYTHON_RUNTIME_ELF_AUDIT.txt", staging / "PYTHON_RUNTIME_ELF_AUDIT.txt")
        copy_file(build_dir / "rbl-studio-wx", staging / "rbl-studio-wx")
        copy_file(build_dir / "rbl-studio-worker", staging / "rbl-studio-worker")

        # extract_tar_safely validates the `python/` prefix and extracts its
        # contents into the supplied root, so target the app-local python dir.
        python_target = staging / "runtime/python"
        if python_target.exists() or python_target.is_symlink():
            existing = [str(path.relative_to(staging)) for path in python_target.rglob("*")][:12] if python_target.is_dir() else []
            raise RuntimeError(f"Python runtime destination unexpectedly exists before extraction: {python_target}; entries={existing}")
        extract_tar_safely(python_archive, python_target)
        copy_tree(build_dir / "runtime/python/licenses", python_target / "licenses")
        for name in ("rbl-python-runtime.json", "UPSTREAM.md", "python-runtime.sha256"):
            copy_file(build_dir / "runtime/python" / name, python_target / name)

        _write_launchers(staging)
        copy_file(ROOT / "assets/rbl_256.png", staging / f"{APP_ID}.png")
        marker_data = {
            "schema_version": SCHEMA_VERSION,
            "app_id": APP_ID,
            "rbl_version": (ROOT / "VERSION").read_text(encoding="utf-8").strip(),
            "python_version": "3.14.10",
            "wxwidgets_version": "3.2.4",
            "llvm_version": "23.1.5",
            "release_source_model": "github_repository_tree",
            "build_dir": str(build_dir),
        }
        (staging / ".rbl-flatpak-payload.json").write_text(json.dumps(marker_data, indent=2) + "\n", encoding="utf-8")
        verify_payload_symlinks(staging)
        write_payload_checksums(staging)

        required_root_markers = (
            "VERSION", "rbl.py", "tools/rbltool.py", "tests/run_tests.py",
            "benchmarks/run_benchmarks.py", "compiler", "runtime", "toolchain",
            "runtime/python/bin/python3", "lib",
            "LINUX_ABI_AUDIT.txt", "PYTHON_RUNTIME_ELF_AUDIT.txt", "SHA256SUMS",
        )
        for marker in required_root_markers:
            if not (staging / marker).exists():
                raise RuntimeError(f"prepared Flatpak application root is missing {marker}")

        staging.rename(output)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/flatpak-payload")
    args = parser.parse_args()
    result = prepare(args.build_dir, args.output)
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
