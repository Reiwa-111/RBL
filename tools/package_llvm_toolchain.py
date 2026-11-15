#!/usr/bin/env python3
"""Stage the hash-pinned ClangBuiltLinux LLVM assembler/linker tools."""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import os
import re
import shutil
import subprocess
import tempfile
import urllib.request
import tarfile
from pathlib import Path


VERSION = "23.1.5"
ARCHIVE_NAME = f"llvm-{VERSION}-x86_64.tar.xz"
ARCHIVE_URL = f"https://cdn.kernel.org/pub/tools/llvm/files/llvm-{VERSION}-x86_64.tar.xz"
ARCHIVE_SIZE = 86_722_684
ARCHIVE_SHA256 = "b69433120ad62420d789f601c58a5472873c724be088c99a70b72fd30ca4af3c"
SOURCE_COMMIT = "392dfff97d29b59d77e5cac6d4b6ef2585fe7117"
LICENSE_URL = f"https://raw.githubusercontent.com/llvm/llvm-project/{SOURCE_COMMIT}/LICENSE.TXT"
LICENSE_SHA256 = "8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee"
SIGNATURE_NAME = f"llvm-{VERSION}-x86_64.tar.sign"
SIGNATURE_URL = f"https://cdn.kernel.org/pub/tools/llvm/files/{SIGNATURE_NAME}"
SIGNATURE_SIZE = 228
SIGNATURE_SHA256 = "8117ea80f8373ca2839a9c390f6479debf30088123bf1a695a1c3621d86e187d"
CHECKSUMS_NAME = "sha256sums.asc"
CHECKSUMS_URL = "https://cdn.kernel.org/pub/tools/llvm/files/sha256sums.asc"
CHECKSUMS_SIZE = 35_676
CHECKSUMS_SHA256 = "e34ca95049180335c036bc17a3dc0ae569dab4c041a8a0714b0cef7ccddbc57a"
SIGNING_KEY_NAME = "llvm-signing-key.asc"
SIGNING_KEY_URL = "https://keys.openpgp.org/vks/v1/by-fingerprint/7BE325C731319CCD01D8CA921D6B269171C01A96"
SIGNING_KEY_SIZE = 4_268
SIGNING_KEY_SHA256 = "167b86e6659e876bc3b4761f64e2e1fcebcfa594fe28673ae15188f95dfd96db"
SIGNING_KEY_PRIMARY_FINGERPRINT = "2437CB76E544CB6AB3D9DFD399739260CB6CB716"
SIGNING_KEY_SUBKEY_FINGERPRINT = "7BE325C731319CCD01D8CA921D6B269171C01A96"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: Path, expected_hash: str, expected_size: int | None = None) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        if (expected_size is not None and destination.stat().st_size != expected_size) or sha256_file(destination) != expected_hash:
            raise RuntimeError(f"cached upstream file failed integrity check; preserved: {destination}")
        return destination
    temporary = destination.with_name(f".{destination.name}.part")
    request = urllib.request.Request(url, headers={"User-Agent": "RBLStudio-toolchain-packager/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
            output.flush()
            os.fsync(output.fileno())
        if expected_size is not None and temporary.stat().st_size != expected_size:
            raise RuntimeError(f"downloaded file size mismatch for {url}: {temporary.stat().st_size}")
        actual = sha256_file(temporary)
        if actual != expected_hash:
            raise RuntimeError(f"downloaded file SHA-256 mismatch for {url}: {actual}")
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()
    return destination


def extract_llvm_binary(archive: Path, destination: Path) -> None:
    expected = f"llvm-{VERSION}-x86_64/bin/llvm"
    with tarfile.open(archive, mode="r:xz") as tar:
        try:
            member = tar.getmember(expected)
        except KeyError as exc:
            raise RuntimeError(f"LLVM archive is missing expected tool binary: {expected}") from exc
        if not member.isfile() or member.size <= 0 or member.size > 256 * 1024 * 1024:
            raise RuntimeError(f"unexpected LLVM tool binary archive member: {member}")
        source = tar.extractfile(member)
        if source is None:
            raise RuntimeError("unable to read LLVM tool binary from archive")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with source, destination.open("xb") as output:
            shutil.copyfileobj(source, output)
        destination.chmod(0o755)


def verify_archive_signature(archive: Path, signature: Path, signing_key: Path, temporary_root: Path) -> None:
    gpg = shutil.which("gpg")
    if not gpg:
        raise RuntimeError("GPG is required to verify the signed Linux LLVM toolchain archive")

    key_listing = subprocess.run(
        [gpg, "--batch", "--with-colons", "--show-keys", "--fingerprint", "--fingerprint", str(signing_key)],
        text=True, capture_output=True,
    )
    if key_listing.returncode:
        raise RuntimeError(f"unable to inspect the pinned LLVM signing key:\n{key_listing.stderr}")
    fingerprints = [line.split(":")[9].upper() for line in key_listing.stdout.splitlines() if line.startswith("fpr:") and len(line.split(":")) > 9]
    if SIGNING_KEY_PRIMARY_FINGERPRINT not in fingerprints or SIGNING_KEY_SUBKEY_FINGERPRINT not in fingerprints:
        raise RuntimeError(f"LLVM signing key fingerprint mismatch: {fingerprints}")

    home = temporary_root / "gpg-home"
    home.mkdir(mode=0o700)
    imported = subprocess.run([gpg, "--homedir", str(home), "--batch", "--import", str(signing_key)], text=True, capture_output=True)
    if imported.returncode:
        raise RuntimeError(f"unable to import the pinned LLVM signing key:\n{imported.stderr}")

    # kernel.org publishes a detached signature for the uncompressed .tar;
    # verify that exact signed payload, not the .tar.xz wrapper.
    tar_path = temporary_root / f"llvm-{VERSION}-x86_64.tar"
    with lzma.open(archive, "rb") as compressed, tar_path.open("xb") as uncompressed:
        shutil.copyfileobj(compressed, uncompressed)
    verified = subprocess.run(
        [gpg, "--homedir", str(home), "--batch", "--status-fd=1", "--verify", str(signature), str(tar_path)],
        text=True, capture_output=True,
    )
    status = verified.stdout + verified.stderr
    valid_signatures = [
        line.split()
        for line in status.splitlines()
        if line.startswith("[GNUPG:] VALIDSIG ")
    ]
    signature_matches_pins = any(
        len(fields) >= 3
        and fields[2].upper() == SIGNING_KEY_SUBKEY_FINGERPRINT
        and fields[-1].upper() == SIGNING_KEY_PRIMARY_FINGERPRINT
        for fields in valid_signatures
    )
    if verified.returncode or not signature_matches_pins:
        raise RuntimeError(f"LLVM archive GPG signature verification failed:\n{status}")


def verify_staged_output(output: Path, manifest: dict) -> None:
    """Verify a cached package before reusing it; a manifest alone is not evidence."""
    if manifest.get("signature_verified") is not True:
        raise RuntimeError("cached LLVM package does not record a verified upstream signature")
    checksum_file = output / "SHA256SUMS"
    if not checksum_file.is_file():
        raise RuntimeError(f"cached LLVM package is missing {checksum_file.name}")
    root = output.resolve()
    entries = checksum_file.read_text(encoding="ascii").splitlines()
    if not entries:
        raise RuntimeError("cached LLVM package has an empty SHA256SUMS")
    seen: set[str] = set()
    for line in entries:
        fields = line.split(maxsplit=1)
        if len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0]):
            raise RuntimeError("cached LLVM package has a malformed SHA256SUMS entry")
        relative_name = fields[1]
        relative = Path(relative_name)
        if relative.is_absolute() or ".." in relative.parts or relative_name in seen:
            raise RuntimeError("cached LLVM package has an unsafe or duplicate SHA256SUMS path")
        seen.add(relative_name)
        candidate = output / relative
        try:
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError) as exc:
            raise RuntimeError(f"cached LLVM package path escapes or is missing: {relative_name}") from exc
        if not candidate.is_file() or sha256_file(candidate) != fields[0]:
            raise RuntimeError(f"cached LLVM package file failed SHA-256 verification: {relative_name}")
    for alias in (output / "bin/clang", output / "bin/ld.lld"):
        if not alias.is_symlink() or os.readlink(alias) != "llvm":
            raise RuntimeError(f"cached LLVM tool alias is missing or unexpected: {alias.relative_to(output)}")
    binary_hash = manifest.get("binary_sha256", {}).get("bin/llvm")
    if not binary_hash or sha256_file(output / "bin/llvm") != binary_hash:
        raise RuntimeError("cached LLVM binary does not match its manifest checksum")


def package(cache: Path, output: Path) -> Path:
    if output.exists():
        manifest_path = output / "manifest.json"
        if manifest_path.is_file():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest.get("archive_sha256") == ARCHIVE_SHA256 and manifest.get("source_commit") == SOURCE_COMMIT and manifest.get("signature_verified") is True:
                verify_staged_output(output, manifest)
                return output
        raise RuntimeError(f"refusing to overwrite existing LLVM toolchain directory: {output}")

    archive = download(ARCHIVE_URL, cache / ARCHIVE_NAME, ARCHIVE_SHA256, ARCHIVE_SIZE)
    license_file = download(LICENSE_URL, cache / "LLVM-LICENSE.TXT", LICENSE_SHA256, 15_141)
    signature_file = download(SIGNATURE_URL, cache / SIGNATURE_NAME, SIGNATURE_SHA256, SIGNATURE_SIZE)
    checksums_file = download(CHECKSUMS_URL, cache / CHECKSUMS_NAME, CHECKSUMS_SHA256, CHECKSUMS_SIZE)
    signing_key = download(SIGNING_KEY_URL, cache / SIGNING_KEY_NAME, SIGNING_KEY_SHA256, SIGNING_KEY_SIZE)
    checksums_text = checksums_file.read_text(encoding="ascii", errors="replace")
    if not re.search(rf"^{ARCHIVE_SHA256}\s+{re.escape(ARCHIVE_NAME)}$", checksums_text, re.MULTILINE):
        raise RuntimeError("kernel.org SHA-256 listing does not contain the pinned LLVM archive hash")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        with tempfile.TemporaryDirectory(prefix="rbl-llvm-signature-") as signature_temp:
            verify_archive_signature(archive, signature_file, signing_key, Path(signature_temp))
        bin_dir = temporary / "bin"
        bin_dir.mkdir()
        extract_llvm_binary(archive, bin_dir / "llvm")
        (bin_dir / "clang").symlink_to("llvm")
        (bin_dir / "ld.lld").symlink_to("llvm")

        license_dir = temporary / "licenses" / "LLVM"
        license_dir.mkdir(parents=True)
        shutil.copy2(license_file, license_dir / "LICENSE.TXT")
        shutil.copy2(license_file, temporary / "LICENSE.TXT")
        shutil.copy2(license_file, temporary / "THIRD_PARTY_NOTICES.txt")

        provenance_dir = temporary / "provenance"
        provenance_dir.mkdir()
        for source_file, target_name, source_hash in (
            (signature_file, SIGNATURE_NAME, SIGNATURE_SHA256),
            (checksums_file, CHECKSUMS_NAME, CHECKSUMS_SHA256),
            (signing_key, SIGNING_KEY_NAME, SIGNING_KEY_SHA256),
        ):
            shutil.copy2(source_file, provenance_dir / target_name)
            (provenance_dir / f"{target_name}.sha256").write_text(source_hash + f"  {target_name}\n", encoding="ascii")

        (temporary / "UPSTREAM.md").write_text(
            "# Linux assembler/linker runtime\n\n"
            f"- Component: ClangBuiltLinux LLVM toolchain `{VERSION}`\n"
            f"- Binary archive: `{ARCHIVE_NAME}`\n"
            f"- URL: {ARCHIVE_URL}\n"
            f"- SHA-256: `{ARCHIVE_SHA256}`\n"
            f"- LLVM source commit: `{SOURCE_COMMIT}`\n"
            f"- License URL: {LICENSE_URL}\n"
            f"- License SHA-256: `{LICENSE_SHA256}`\n"
            f"- Detached signature: `{SIGNATURE_NAME}`; verified over the decompressed `.tar` payload.\n"
            f"- Signing-key primary fingerprint: `{SIGNING_KEY_PRIMARY_FINGERPRINT}`\n"
            f"- Signing-key subkey fingerprint: `{SIGNING_KEY_SUBKEY_FINGERPRINT}`\n"
            f"- Signature/checksum/key evidence is retained under `provenance/`.\n"
            "- License: Apache-2.0 with LLVM Exceptions; full upstream license text is included.\n"
            "- `clang` invokes LLVM's integrated assembler (`-x assembler`); `ld.lld` invokes LLD.\n"
            "- Observed ELF symbol requirements: GLIBC 2.27 and GLIBCXX 3.4.22 (host libstdc++ must provide the latter).\n",
            encoding="utf-8",
        )
        manifest = {
            "component": "ClangBuiltLinux LLVM integrated assembler + LLD",
            "version": VERSION,
            "architecture": "x86_64",
            "archive": ARCHIVE_NAME,
            "archive_url": ARCHIVE_URL,
            "archive_size": ARCHIVE_SIZE,
            "archive_sha256": ARCHIVE_SHA256,
            "source_commit": SOURCE_COMMIT,
            "license_url": LICENSE_URL,
            "license_sha256": LICENSE_SHA256,
            "license": "Apache-2.0 WITH LLVM-exception",
            "minimum_glibc": "2.27",
            "minimum_glibcxx": "3.4.22",
            "signature_verified": True,
            "signature_key_primary_fingerprint": SIGNING_KEY_PRIMARY_FINGERPRINT,
            "signature_key_subkey_fingerprint": SIGNING_KEY_SUBKEY_FINGERPRINT,
            "signature_file_sha256": SIGNATURE_SHA256,
            "checksums_file_sha256": CHECKSUMS_SHA256,
            "signing_key_sha256": SIGNING_KEY_SHA256,
            "tools": {"assembler": "bin/clang (integrated assembler)", "linker": "bin/ld.lld"},
            "binary_sha256": {"bin/llvm": sha256_file(bin_dir / "llvm")},
        }
        (temporary / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        checksum_files = [
            path for path in temporary.rglob("*")
            if path.is_file() and path.name != "SHA256SUMS"
        ]
        checksum_lines = [
            f"{sha256_file(path)}  {path.relative_to(temporary).as_posix()}"
            for path in sorted(checksum_files, key=lambda item: item.relative_to(temporary).as_posix())
        ]
        (temporary / "SHA256SUMS").write_text("\n".join(checksum_lines) + "\n", encoding="ascii")

        env = os.environ.copy()
        clang = subprocess.run([str(bin_dir / "clang"), "--version"], env=env, text=True, capture_output=True)
        lld = subprocess.run([str(bin_dir / "ld.lld"), "--version"], env=env, text=True, capture_output=True)
        if clang.returncode or lld.returncode or VERSION not in (clang.stdout + clang.stderr + lld.stdout + lld.stderr):
            raise RuntimeError(f"LLVM tool startup/version probe failed:\n{clang.stdout}\n{clang.stderr}\n{lld.stdout}\n{lld.stderr}")

        smoke_source = temporary / "smoke.s"
        smoke_object = temporary / "smoke.o"
        smoke_source.write_text(".text\n.globl _start\n_start:\n  xor %edi, %edi\n  mov $60, %eax\n  syscall\n", encoding="ascii")
        assemble = subprocess.run([str(bin_dir / "clang"), "-target", "x86_64-pc-linux-gnu", "-c", "-x", "assembler", str(smoke_source), "-o", str(smoke_object)], env=env, text=True, capture_output=True)
        if assemble.returncode:
            raise RuntimeError(f"LLVM integrated assembler smoke test failed:\n{assemble.stdout}\n{assemble.stderr}")
        (temporary / "smoke.s").unlink()
        smoke_object.unlink()
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("build/cache/llvm-23.1.5"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = package(args.cache.resolve(), args.output.resolve())
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
