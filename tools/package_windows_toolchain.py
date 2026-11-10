#!/usr/bin/env python3
"""Create a minimal, auditable MinGW-w64 bundle for RBL's Windows linker path.

This intentionally packages the GCC driver/linker and GNU assembler, not the C
frontend, headers, C++ frontend, debugger, make, or MSYS2 shell. The bundle is
derived from an installed UCRT64 MinGW-w64 root and fails closed if a required
file or non-system DLL dependency cannot be resolved.

Usage:
  python tools/package_windows_toolchain.py --source C:/msys64/ucrt64
  python tools/package_windows_toolchain.py --source C:/msys64/ucrt64 --check
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = REPO_ROOT / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64"
SYSTEM_DLL_PREFIXES = ("api-ms-win-", "ext-ms-win-")
SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "combase.dll", "comdlg32.dll",
    "crypt32.dll", "gdi32.dll", "imm32.dll", "kernel32.dll", "msvcrt.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll", "version.dll",
    "winmm.dll", "ws2_32.dll", "wldap32.dll", "wsock32.dll",
}
LINK_ARCHIVES = (
    "libm.a", "libmingw32.a", "libgcc.a", "libgcc_eh.a", "libmingwex.a",
    "libmsvcrt.a", "libkernel32.a", "libpthread.a", "libadvapi32.a",
    "libshell32.a", "libuser32.a",
)
LICENSE_FILES = (
    "binutils/COPYING", "binutils/COPYING.LIB", "binutils/COPYING3", "binutils/COPYING3.LIB",
    "gcc/COPYING.RUNTIME", "gcc/COPYING3",
    "crt/COPYING", "crt/COPYING.MinGW-w64-runtime.txt",
    "crt/COPYING.MinGW-w64.txt",
    "gettext-runtime/COPYING", "gettext-runtime/intl/COPYING.LIB",
    "libiconv/COPYING", "libiconv/COPYING.LIB", "libiconv/libcharset/COPYING.LIB",
    "libwinpthread/COPYING", "winpthreads/COPYING",
    "zlib/LICENSE", "zstd/LICENSE",
)
PACKAGE_SOURCES = (
    ("mingw-w64-ucrt-x86_64-gcc", "16.2.0-4", "mingw-w64-gcc-16.2.0-4.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-libgcc", "16.2.0-4", "mingw-w64-gcc-16.2.0-4.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-binutils", "2.47-4", "mingw-w64-binutils-2.47-4.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-crt", "14.0.0.r426.g4564ee4b5-1", "mingw-w64-crt-14.0.0.r426.g4564ee4b5-1.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-gettext-runtime", "1.0-1", "mingw-w64-gettext-1.0-1.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-libiconv", "1.19-1", "mingw-w64-libiconv-1.19-1.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-libwinpthread", "14.0.0.r426.g4564ee4b5-1", "mingw-w64-winpthreads-14.0.0.r426.g4564ee4b5-1.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-winpthreads", "14.0.0.r426.g4564ee4b5-1", "mingw-w64-winpthreads-14.0.0.r426.g4564ee4b5-1.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-zlib", "1.4-2", "mingw-w64-zlib-1.4-2.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-zstd", "1.5.7-2", "mingw-w64-zstd-1.5.7-2.src.tar.zst"),
    ("mingw-w64-ucrt-x86_64-windows-default-manifest", "20260815-1", "mingw-w64-windows-default-manifest-20260815-1.src.tar.zst"),
)


def run(args: list[str], *, capture: bool = True, env: dict[str, str] | None = None) -> str:
    proc = subprocess.run(args, text=True, encoding="utf-8", errors="replace",
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None, env=env, check=False)
    if proc.returncode:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(args)}\n{proc.stdout or ''}")
    return proc.stdout or ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_manifest(dest: Path) -> dict:
    """Validate the manifest structure and the complete payload inventory."""
    dest = dest.resolve()
    manifest_path = dest / "rbl-toolchain-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema") != 1:
        raise RuntimeError("unsupported or malformed toolchain manifest schema")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise RuntimeError("toolchain manifest has no file inventory")
    packages = manifest.get("packages")
    expected_packages = {name: (version, archive) for name, version, archive in PACKAGE_SOURCES}
    if not isinstance(packages, list):
        raise RuntimeError("toolchain manifest has no package/source inventory")
    actual_packages = {}
    for package in packages:
        if not isinstance(package, dict):
            raise RuntimeError("invalid package metadata entry")
        name = package.get("id")
        if not isinstance(name, str) or name in actual_packages:
            raise RuntimeError(f"invalid or duplicate package ID: {name!r}")
        actual_packages[name] = (package.get("version"), package.get("source_archive"))
    for name, (version, archive) in expected_packages.items():
        expected_url = f"https://mirror.msys2.org/mingw/sources/{archive}"
        if actual_packages.get(name) != (version, expected_url):
            raise RuntimeError(f"package/source metadata mismatch for {name}")
    if set(actual_packages) != set(expected_packages):
        raise RuntimeError("package/source inventory contains missing or unrecognized package IDs")

    errors: list[str] = []
    expected: set[str] = set()
    actual_total = 0
    for name, info in files.items():
        if not isinstance(name, str) or not name or "\\" in name:
            errors.append(f"invalid manifest path: {name!r}")
            continue
        relative = Path(name)
        if relative.is_absolute() or any(part in ("", ".", "..") for part in name.split("/")):
            errors.append(f"unsafe manifest path: {name!r}")
            continue
        path = dest / relative
        try:
            path.resolve().relative_to(dest)
        except ValueError:
            errors.append(f"manifest path escapes bundle: {name}")
            continue
        if path.is_symlink():
            errors.append(f"symlink payload is not allowed: {name}")
            continue
        if not isinstance(info, dict):
            errors.append(f"invalid file metadata: {name}")
            continue
        size, digest = info.get("size"), info.get("sha256")
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            errors.append(f"invalid size metadata: {name}")
            continue
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            errors.append(f"invalid SHA-256 metadata: {name}")
            continue
        expected.add(relative.as_posix())
        if not path.is_file():
            errors.append(f"missing: {name}")
        else:
            actual_total += path.stat().st_size
            if path.stat().st_size != size or sha256(path) != digest:
                errors.append(f"hash/size mismatch: {name}")

    actual = {
        path.relative_to(dest).as_posix()
        for path in dest.rglob("*")
        if path.is_file() and path != manifest_path
    }
    for name in sorted(actual - expected):
        errors.append(f"unmanifested payload file: {name}")
    for name in sorted(expected - actual):
        if not any(item.endswith(name) for item in errors):
            errors.append(f"missing from payload: {name}")

    if manifest.get("file_count") != len(expected):
        errors.append(f"file_count mismatch: expected {len(expected)}, manifest says {manifest.get('file_count')!r}")
    if manifest.get("total_bytes") != actual_total:
        errors.append(f"total_bytes mismatch: actual {actual_total}, manifest says {manifest.get('total_bytes')!r}")
    if errors:
        raise RuntimeError("bundle validation failed:\n" + "\n".join(errors))
    return manifest


def imports(objdump: Path, binary: Path) -> list[str]:
    output = run([str(objdump), "-p", str(binary)])
    names = []
    for line in output.splitlines():
        line = line.strip()
        if line.lower().startswith("dll name:"):
            name = line.split(":", 1)[1].strip()
            if name:
                names.append(name)
    return names


def is_system_dll(name: str) -> bool:
    lowered = name.lower()
    return lowered in SYSTEM_DLLS or lowered.startswith(SYSTEM_DLL_PREFIXES)


def copy_file(source: Path, source_root: Path, dest_root: Path, copied: dict[str, str]) -> Path:
    source = source.resolve()
    try:
        relative = source.relative_to(source_root.resolve())
    except ValueError as exc:
        raise RuntimeError(f"required file escapes toolchain root: {source}") from exc
    target = dest_root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    copied[relative.as_posix()] = sha256(target)
    return target


def find_file(root: Path, name: str, search_dirs: list[Path]) -> Path:
    for directory in search_dirs:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    # The required set is small; this fallback supports minor GCC layout changes.
    matches = list(root.glob(f"**/{name}"))
    if matches:
        return matches[0]
    raise RuntimeError(f"required MinGW-w64 file not found: {name}")


def build_manifest(root: Path, dest: Path) -> dict:
    root = root.resolve()
    bin_dir = root / "bin"
    gcc, assembler, objdump = (bin_dir / name for name in
                               ("gcc.exe", "as.exe", "objdump.exe"))
    for required in (gcc, assembler, objdump):
        if not required.is_file():
            raise RuntimeError(f"missing required tool: {required}")
    target = run([str(gcc), "-dumpmachine"]).strip()
    if target != "x86_64-w64-mingw32":
        raise RuntimeError(f"expected x86_64-w64-mingw32 GCC, got {target!r}")
    version = run([str(gcc), "-dumpversion"]).strip()
    collect2_text = run([str(gcc), "-print-prog-name=collect2"]).strip()
    collect2 = Path(collect2_text.replace("/", os.sep))
    if not collect2.is_absolute():
        collect2 = bin_dir / collect2
    collect2 = collect2.resolve()
    if not collect2.is_file():
        raise RuntimeError(f"GCC collect2 not found: {collect2}")
    linker_text = run([str(gcc), "-print-prog-name=ld"]).strip()
    linker = Path(linker_text.replace("/", os.sep))
    if not linker.is_absolute():
        linker = bin_dir / linker
    linker = linker.resolve()
    if not linker.is_file():
        raise RuntimeError(f"GCC target linker not found: {linker}")
    tool_env = os.environ.copy()
    tool_env["PATH"] = str(bin_dir) + os.pathsep + tool_env.get("PATH", "")
    gcc_lib_dir = collect2.parent
    gcc_root = gcc_lib_dir.parents[3]  # <prefix>/lib/gcc/<target>/<version> -> <prefix>
    if gcc_root != root:
        # Validate rather than silently produce a malformed relocatable bundle.
        raise RuntimeError(f"unexpected GCC libexec layout: {gcc_lib_dir} (prefix inferred as {gcc_root})")

    library_dirs = [root / "lib", root / target / "lib", gcc_lib_dir]
    required_files = [gcc, assembler, linker, collect2,
                      root / "lib" / "crt2.o", root / "lib" / "default-manifest.o",
                      gcc_lib_dir / "crtbegin.o", gcc_lib_dir / "crtend.o"]
    for archive in LINK_ARCHIVES:
        required_files.append(find_file(root, archive, library_dirs))
    for path in required_files:
        if not path.is_file():
            raise RuntimeError(f"required linker input missing: {path}")

    # Keep the original MinGW prefix layout. GCC's built-in sysroot is relative
    # to bin/../../<prefix-name>, and collect2 is relative to bin/../lib/gcc.
    if dest.name != root.name:
        raise RuntimeError(f"destination prefix directory must be named {root.name!r}: {dest}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        shutil.rmtree(dest)
    copied: dict[str, str] = {}
    copied_tools = [copy_file(path, root, dest, copied) for path in required_files]

    # Recursively resolve non-Windows DLL imports for the tools and helper DLLs.
    dll_queue = list(copied_tools)
    seen: set[str] = set()
    while dll_queue:
        binary = dll_queue.pop()
        for dll_name in imports(objdump, binary):
            lowered = dll_name.lower()
            if is_system_dll(lowered) or lowered in seen:
                continue
            seen.add(lowered)
            dll_source = bin_dir / dll_name
            if not dll_source.is_file():
                raise RuntimeError(f"unresolved non-system DLL {dll_name!r}, imported by {binary}")
            dll_target = copy_file(dll_source, root, dest, copied)
            dll_queue.append(dll_target)

    # Preserve the upstream license texts for every redistributed component.
    for relative in LICENSE_FILES:
        license_source = root / "share" / "licenses" / relative
        if not license_source.is_file():
            raise RuntimeError(f"required third-party license file missing: {license_source}")
        license_target = dest / "share" / "licenses" / relative
        license_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(license_source, license_target)
        copied[(Path("share/licenses") / relative).as_posix()] = sha256(license_target)

    # objdump itself is a packaging-time inspector, not a user runtime tool.
    total_bytes = sum((dest / rel).stat().st_size for rel in copied)
    manifest = {
        "schema": 1,
        "purpose": "RBL Windows x86_64 native user-program assembly and static linking",
        "target": target,
        "gcc_version": version,
        "assembler_version": run([str(assembler), "--version"]).splitlines()[0],
        "linker_version": run([str(linker), "--version"], env=tool_env).splitlines()[0],
        "source_distribution": "MSYS2 UCRT64 MinGW-w64",
        "packages": [
            {"id": name, "version": version,
             "source_archive": f"https://mirror.msys2.org/mingw/sources/{archive}"}
            for name, version, archive in PACKAGE_SOURCES
        ],
        "source_archives_bundled": False,
        "runtime_scope": "gcc driver + collect2 + GNU ld + GNU as + required DLL closure + link inputs",
        "excluded": ["cc1.exe", "cc1plus.exe", "g++.exe", "gdb.exe", "make.exe", "headers", "MSYS2 shell", "LTO plugin", "lto-wrapper.exe"],
        "linker_flags_required": ["-static", "-lm", "-fno-use-linker-plugin"],
        "files": {name: {"sha256": digest, "size": (dest / name).stat().st_size}
                  for name, digest in sorted(copied.items())},
        "file_count": len(copied),
        "total_bytes": total_bytes,
    }
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(os.environ.get("RBL_MINGW_ROOT", r"C:\msys64\ucrt64")))
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    parser.add_argument("--check", action="store_true", help="validate an existing bundle without changing it")
    args = parser.parse_args()
    try:
        if args.check:
            manifest = verify_manifest(args.dest)
            print(f"PASS: {manifest['file_count']} files, {manifest['total_bytes']} bytes; complete inventory and hashes verified")
            return 0
        manifest = build_manifest(args.source, args.dest)
        path = args.dest / "rbl-toolchain-manifest.json"
        path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Created {args.dest}")
        print(f"Files: {manifest['file_count']}; bytes: {manifest['total_bytes']}")
        print(f"Manifest: {path}")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
