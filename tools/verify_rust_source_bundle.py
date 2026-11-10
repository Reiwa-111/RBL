from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import hashlib
import io
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

LIVE_SOURCE_FILES = (
    "Cargo.toml",
    "Cargo.lock",
    "LICENSE",
    "src/bin/rbl-studio-worker.rs",
    "src/process.rs",
    "src/settings.rs",
)


def cargo_closure(root: Path, target: str) -> set[tuple[str, str]]:
    result = subprocess.run(
        ["cargo", "tree", "--locked", "--offline", "--target", target,
         "--prefix", "none", "--edges", "normal"],
        cwd=root, check=True, capture_output=True, text=True, encoding="utf-8",
    )
    packages: set[tuple[str, str]] = set()
    for line in result.stdout.splitlines():
        match = re.match(r"([^ ]+) v([^ ]+)", line)
        if match and match.group(1) != "rbl-studio":
            packages.add((match.group(1), match.group(2)))
    return packages


def cargo_lock_registry_checksums(root: Path) -> dict[tuple[str, str], str]:
    """Read registry package checksums without requiring Python 3.11 tomllib."""
    text = (root / "Cargo.lock").read_text(encoding="utf-8")
    packages: dict[tuple[str, str], str] = {}
    for block in re.split(r"(?m)^\[\[package\]\]\s*$", text)[1:]:
        def field(name: str) -> str | None:
            match = re.search(rf'(?m)^{re.escape(name)} = "([^"]+)"$', block)
            return match.group(1) if match else None

        name, version = field("name"), field("version")
        source, checksum = field("source"), field("checksum")
        if source and source.startswith("registry+"):
            if not name or not version or not checksum:
                raise RuntimeError("Cargo.lock registry package is missing name/version/checksum")
            key = (name, version)
            if key in packages:
                raise RuntimeError(f"duplicate Cargo.lock registry package: {name} {version}")
            packages[key] = checksum
    return packages


def normalized_notice(data: bytes) -> bytes:
    text = data.decode("utf-8")
    lines = [
        line for line in text.splitlines()
        if not line.startswith("- The verified corresponding-source archive is ")
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def decompress_with_libzstd(archive: Path, output: Path) -> None:
    library = ctypes.util.find_library("zstd")
    if not library:
        raise RuntimeError("no zstd executable supplied and libzstd is unavailable")
    lib = ctypes.CDLL(library)
    lib.ZSTD_getFrameContentSize.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    lib.ZSTD_getFrameContentSize.restype = ctypes.c_ulonglong
    lib.ZSTD_decompress.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t
    ]
    lib.ZSTD_decompress.restype = ctypes.c_size_t
    lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
    lib.ZSTD_isError.restype = ctypes.c_uint
    lib.ZSTD_getErrorName.argtypes = [ctypes.c_size_t]
    lib.ZSTD_getErrorName.restype = ctypes.c_char_p

    compressed = archive.read_bytes()
    source = ctypes.create_string_buffer(compressed)
    frame_size = lib.ZSTD_getFrameContentSize(source, len(compressed))
    if frame_size in (0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF) or frame_size <= 0:
        raise RuntimeError("zstd frame does not expose a valid decompressed size")
    if frame_size > 512 * 1024 * 1024:
        raise RuntimeError(f"unexpectedly large source tar: {frame_size} bytes")
    destination = ctypes.create_string_buffer(frame_size)
    result = lib.ZSTD_decompress(destination, frame_size, source, len(compressed))
    if lib.ZSTD_isError(result):
        detail = lib.ZSTD_getErrorName(result).decode("utf-8", errors="replace")
        raise RuntimeError(f"libzstd decompression failed: {detail}")
    with output.open("wb") as stream:
        stream.write(destination.raw[:result])


def read_bundle(archive: Path, zstd: str | None) -> dict[str, bytes]:
    with tempfile.TemporaryDirectory(prefix="rbl-source-verify-") as temp:
        tar_path = Path(temp) / "bundle.tar"
        if zstd:
            subprocess.run(
                [zstd, "-d", "-f", str(archive), "-o", str(tar_path)],
                check=True, capture_output=True,
            )
        else:
            decompress_with_libzstd(archive, tar_path)
        files: dict[str, bytes] = {}
        root_name: str | None = None
        with tarfile.open(tar_path, "r:") as tar:
            for member in tar:
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts or not path.parts:
                    raise RuntimeError(f"unsafe archive member path: {member.name}")
                if root_name is None:
                    root_name = path.parts[0]
                if path.parts[0] != root_name:
                    raise RuntimeError(f"multiple top-level roots: {member.name}")
                if member.isdir():
                    continue
                if not member.isfile() or len(path.parts) < 2:
                    raise RuntimeError(f"unexpected archive member: {member.name}")
                rel = PurePosixPath(*path.parts[1:]).as_posix()
                if rel in files:
                    raise RuntimeError(f"duplicate archive member: {rel}")
                stream = tar.extractfile(member)
                if stream is None:
                    raise RuntimeError(f"cannot read archive member: {member.name}")
                files[rel] = stream.read()
        return files


def verify(root: Path, archive: Path, zstd: str | None, targets: list[str]) -> tuple[int, int]:
    files = read_bundle(archive, zstd)
    manifest = files.get("SHA256SUMS.txt")
    if manifest is None:
        raise RuntimeError("archive has no SHA256SUMS.txt")
    expected: dict[str, str] = {}
    for line in manifest.decode("ascii").splitlines():
        digest, rel = line.split("  ", 1)
        path = PurePosixPath(rel)
        if path.is_absolute() or ".." in path.parts or rel in expected:
            raise RuntimeError(f"invalid checksum entry: {line}")
        expected[rel] = digest
    if set(expected) != set(files) - {"SHA256SUMS.txt"}:
        raise RuntimeError("checksum manifest does not cover the exact archive file set")
    for rel, digest in expected.items():
        actual = hashlib.sha256(files[rel]).hexdigest()
        if actual != digest:
            raise RuntimeError(f"archive checksum mismatch for {rel}")

    for rel in LIVE_SOURCE_FILES:
        if files.get(rel) != (root / rel).read_bytes():
            raise RuntimeError(f"source bundle is stale: {rel} differs from the live repository")
    archived_notice = files.get("native/wx-ui/RUST_DEPENDENCY_NOTICES.md")
    live_notice = (root / "native/wx-ui/RUST_DEPENDENCY_NOTICES.md").read_bytes()
    if archived_notice is None or archived_notice != normalized_notice(live_notice):
        raise RuntimeError("source bundle dependency inventory is stale")

    active: set[tuple[str, str]] = set()
    for target in targets:
        active |= cargo_closure(root, target)

    bt = chr(96)
    notice_rows: set[tuple[str, str]] = set()
    for line in live_notice.decode("utf-8").splitlines():
        fields = line.split("|")
        if len(fields) >= 6:
            raw_name = fields[1].strip()
            raw_version = fields[2].strip()
            if raw_name.startswith(bt) and raw_version.startswith(bt):
                notice_rows.add((raw_name.strip(bt), raw_version.strip(bt)))
    if active != notice_rows:
        raise RuntimeError(
            "license table does not exactly match active target closure: "
            f"missing={sorted(active - notice_rows)}, extra={sorted(notice_rows - active)}"
        )

    lock_checksums = cargo_lock_registry_checksums(root)
    for name, version in sorted(active):
        crate_rel = f"crates/{name}-{version}.crate"
        crate_data = files.get(crate_rel)
        expected_digest = lock_checksums.get((name, version))
        if crate_data is None:
            raise RuntimeError(f"active crate source archive missing: {crate_rel}")
        if expected_digest is None:
            raise RuntimeError(f"active registry crate has no Cargo.lock checksum: {name} {version}")
        actual_digest = hashlib.sha256(crate_data).hexdigest()
        if actual_digest != expected_digest:
            raise RuntimeError(
                f"crate archive does not match Cargo.lock checksum: {name} {version}"
            )

    expected_crates = {f"{name}-{version}.crate" for name, version in active}
    actual_crates = {
        PurePosixPath(rel).name for rel in files
        if rel.startswith("crates/") and rel.endswith(".crate")
    }
    if actual_crates != expected_crates:
        raise RuntimeError(
            f"crate archive/license table mismatch: missing={sorted(expected_crates - actual_crates)}, "
            f"extra={sorted(actual_crates - expected_crates)}"
        )

    license_root = root / "native/wx-ui/licenses/Rust"
    license_dirs = set()
    for path in license_root.iterdir():
        if path.is_dir():
            name, version = path.name.rsplit("-", 1)
            license_dirs.add((name, version))
    if license_dirs != notice_rows:
        raise RuntimeError(
            f"license directory/table mismatch: missing={sorted(notice_rows - license_dirs)}, "
            f"extra={sorted(license_dirs - notice_rows)}"
        )

    for line in live_notice.decode("utf-8").splitlines():
        fields = line.split("|")
        if len(fields) < 6:
            continue
        raw_name = fields[1].strip()
        raw_version = fields[2].strip()
        if not raw_name.startswith(bt) or not raw_version.startswith(bt):
            continue
        name = raw_name.strip(bt)
        version = raw_version.strip(bt)
        directory = license_root / f"{name}-{version}"
        crate_rel = f"crates/{name}-{version}.crate"
        try:
            with tarfile.open(fileobj=io.BytesIO(files[crate_rel]), mode="r:gz") as crate:
                crate_members = [member for member in crate.getmembers() if member.isfile()]
                for filename in (item.strip() for item in fields[5].split(",")):
                    copied_path = directory / filename
                    if not copied_path.is_file():
                        raise RuntimeError(f"listed license file missing: {copied_path}")
                    matches = [
                        member for member in crate_members
                        if PurePosixPath(member.name).name == filename
                    ]
                    if len(matches) != 1:
                        raise RuntimeError(
                            f"crate must contain exactly one {filename}: {name} {version}"
                        )
                    stream = crate.extractfile(matches[0])
                    if stream is None or stream.read() != copied_path.read_bytes():
                        raise RuntimeError(
                            f"copied license differs from crate source: {name} {version}/{filename}"
                        )
        except (tarfile.TarError, OSError) as error:
            raise RuntimeError(f"cannot verify license files for {name} {version}: {error}") from error

    return len(expected), len(actual_crates)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--zstd", default=None)
    parser.add_argument(
        "--target", action="append", dest="targets",
        help="Cargo target triple to verify; default checks both shipped targets",
    )
    args = parser.parse_args()
    targets = args.targets or ["x86_64-pc-windows-gnu", "x86_64-unknown-linux-gnu"]
    count, crates = verify(args.root.resolve(), args.archive.resolve(), args.zstd, targets)
    print(f"Rust source bundle verification: PASS ({crates} licensed crates, {count} checksums; targets={','.join(targets)})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"Rust source bundle verification: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
