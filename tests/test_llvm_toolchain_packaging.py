#!/usr/bin/env python3
"""Regression tests for the pinned Linux LLVM toolchain packager."""
from __future__ import annotations

import io
import lzma
import os
import sys
import tarfile
import tempfile
import unittest
import json
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import package_llvm_toolchain


class LLVMToolchainPackagingTests(unittest.TestCase):
    def test_upstream_identity_is_pinned(self):
        self.assertEqual(package_llvm_toolchain.VERSION, "23.1.5")
        self.assertEqual(package_llvm_toolchain.ARCHIVE_SIZE, 86_722_684)
        self.assertEqual(package_llvm_toolchain.ARCHIVE_SHA256, "b69433120ad62420d789f601c58a5472873c724be088c99a70b72fd30ca4af3c")
        self.assertEqual(package_llvm_toolchain.SOURCE_COMMIT, "392dfff97d29b59d77e5cac6d4b6ef2585fe7117")
        self.assertEqual(package_llvm_toolchain.LICENSE_SHA256, "8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee")
        self.assertEqual(package_llvm_toolchain.SIGNATURE_SHA256, "8117ea80f8373ca2839a9c390f6479debf30088123bf1a695a1c3621d86e187d")
        self.assertEqual(package_llvm_toolchain.SIGNING_KEY_PRIMARY_FINGERPRINT, "2437CB76E544CB6AB3D9DFD399739260CB6CB716")
        self.assertEqual(package_llvm_toolchain.SIGNING_KEY_SUBKEY_FINGERPRINT, "7BE325C731319CCD01D8CA921D6B269171C01A96")

    def test_extract_only_expected_regular_tool_member(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fixture.tar.xz"
            with tarfile.open(archive, "w:xz") as tar:
                info = tarfile.TarInfo(f"llvm-{package_llvm_toolchain.VERSION}-x86_64/bin/llvm")
                payload = b"ELF fixture"
                info.size = len(payload)
                tar.addfile(info, io.BytesIO(payload))
            output = root / "out" / "bin" / "llvm"
            package_llvm_toolchain.extract_llvm_binary(archive, output)
            self.assertEqual(output.read_bytes(), payload)
            if os.name != "nt":
                self.assertTrue(output.stat().st_mode & 0o111)

    def test_extract_rejects_missing_expected_member(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fixture.tar.xz"
            with tarfile.open(archive, "w:xz") as tar:
                info = tarfile.TarInfo("untrusted/path/llvm")
                payload = b"not the tool"
                info.size = len(payload)
                tar.addfile(info, io.BytesIO(payload))
            with self.assertRaises(RuntimeError):
                package_llvm_toolchain.extract_llvm_binary(archive, root / "out" / "llvm")

    def test_signature_verification_fails_closed_without_gpg(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(package_llvm_toolchain.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "GPG is required"):
                package_llvm_toolchain.verify_archive_signature(Path(temp) / "archive", Path(temp) / "sig", Path(temp) / "key", Path(temp))

    def test_signature_verification_rejects_wrong_key_fingerprint(self):
        listing = "pub:::::::::\nfpr:::::::::AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(package_llvm_toolchain.shutil, "which", return_value="gpg"), \
             mock.patch.object(package_llvm_toolchain.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=listing, stderr="")):
            with self.assertRaisesRegex(RuntimeError, "fingerprint mismatch"):
                package_llvm_toolchain.verify_archive_signature(Path(temp) / "archive", Path(temp) / "sig", Path(temp) / "key", Path(temp))

    def test_signature_verification_rejects_invalid_signature(self):
        primary = package_llvm_toolchain.SIGNING_KEY_PRIMARY_FINGERPRINT
        subkey = package_llvm_toolchain.SIGNING_KEY_SUBKEY_FINGERPRINT
        listing = f"pub:::::::::\nfpr:::::::::{primary}\nsub:::::::::\nfpr:::::::::{subkey}\n"
        results = [
            mock.Mock(returncode=0, stdout=listing, stderr=""),
            mock.Mock(returncode=0, stdout="imported", stderr=""),
            mock.Mock(returncode=1, stdout="[GNUPG:] BADSIG", stderr="bad signature"),
        ]
        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(package_llvm_toolchain.shutil, "which", return_value="gpg"), \
             mock.patch.object(package_llvm_toolchain.subprocess, "run", side_effect=results):
            (Path(temp) / "archive").write_bytes(lzma.compress(b"fixture"))
            with self.assertRaisesRegex(RuntimeError, "signature verification failed"):
                package_llvm_toolchain.verify_archive_signature(Path(temp) / "archive", Path(temp) / "sig", Path(temp) / "key", Path(temp))

    def test_signature_verification_requires_pinned_primary_and_subkey_pair(self):
        primary = package_llvm_toolchain.SIGNING_KEY_PRIMARY_FINGERPRINT
        subkey = package_llvm_toolchain.SIGNING_KEY_SUBKEY_FINGERPRINT
        listing = f"pub:::::::::\nfpr:::::::::{primary}\nsub:::::::::\nfpr:::::::::{subkey}\n"
        # A valid signature from the pinned subkey must also identify the pinned primary key.
        status = f"[GNUPG:] VALIDSIG {subkey} 20261112 0 4 0 1 8 00 {('A' * 40)}"
        results = [
            mock.Mock(returncode=0, stdout=listing, stderr=""),
            mock.Mock(returncode=0, stdout="imported", stderr=""),
            mock.Mock(returncode=0, stdout=status, stderr=""),
        ]
        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(package_llvm_toolchain.shutil, "which", return_value="gpg"), \
             mock.patch.object(package_llvm_toolchain.subprocess, "run", side_effect=results):
            (Path(temp) / "archive").write_bytes(lzma.compress(b"fixture"))
            with self.assertRaisesRegex(RuntimeError, "signature verification failed"):
                package_llvm_toolchain.verify_archive_signature(Path(temp) / "archive", Path(temp) / "sig", Path(temp) / "key", Path(temp))

    def test_corrupt_cached_archive_is_rejected_and_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            cached = Path(temp) / "llvm.tar.xz"
            cached.write_bytes(b"evil")
            with self.assertRaisesRegex(RuntimeError, "cached upstream file failed integrity check"):
                package_llvm_toolchain.download("https://invalid.example/archive", cached, "0" * 64, 4)
            self.assertEqual(cached.read_bytes(), b"evil")

    def test_existing_output_without_verified_signature_is_not_reused_or_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "output"
            output.mkdir()
            (output / "manifest.json").write_text(json.dumps({"signature_verified": False}), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "refusing to overwrite existing LLVM toolchain directory"):
                package_llvm_toolchain.package(Path(temp) / "cache", output)

    def test_cached_output_reuse_rechecks_all_staged_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "output"
            (output / "bin").mkdir(parents=True)
            (output / "bin/llvm").write_bytes(b"tampered")
            (output / "manifest.json").write_text(json.dumps({
                "archive_sha256": package_llvm_toolchain.ARCHIVE_SHA256,
                "source_commit": package_llvm_toolchain.SOURCE_COMMIT,
                "signature_verified": True,
                "binary_sha256": {"bin/llvm": "0" * 64},
            }), encoding="utf-8")
            (output / "SHA256SUMS").write_text("0" * 64 + "  bin/llvm\n", encoding="ascii")
            with self.assertRaisesRegex(RuntimeError, "failed SHA-256 verification"):
                package_llvm_toolchain.package(Path(temp) / "cache", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
