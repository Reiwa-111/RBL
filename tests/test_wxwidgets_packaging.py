#!/usr/bin/env python3
"""Safety tests for the pinned wxWidgets source archive packager."""
from __future__ import annotations

import sys
import os
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import package_wxwidgets


class WxWidgetsPackagingTests(unittest.TestCase):
    def _archive(self, root: Path, members: list[tuple[str, bytes | str, str]]) -> Path:
        archive = root / "fixture.tar.bz2"
        with tarfile.open(archive, "w:bz2") as tar:
            for name, payload, kind in members:
                info = tarfile.TarInfo(name)
                if kind == "file":
                    data = payload if isinstance(payload, bytes) else payload.encode()
                    info.size = len(data)
                    import io
                    tar.addfile(info, io.BytesIO(data))
                elif kind == "symlink":
                    info.type = tarfile.SYMTYPE
                    info.linkname = str(payload)
                    tar.addfile(info)
                elif kind == "hardlink":
                    info.type = tarfile.LNKTYPE
                    info.linkname = str(payload)
                    tar.addfile(info)
                else:
                    raise AssertionError(kind)
        return archive

    def test_pinned_upstream_identity(self):
        self.assertEqual(package_wxwidgets.VERSION, "3.2.4")
        self.assertEqual(package_wxwidgets.SHA256, "0640e1ab716db5af2ecb7389dbef6138d7679261fbff730d23845ba838ca133e")
        self.assertTrue(package_wxwidgets.URL.startswith("https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.4/"))

    def test_safe_extract_accepts_relative_symlink(self):
        if os.name == "nt":
            self.skipTest("creating POSIX symlinks requires a Linux filesystem")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = self._archive(root, [
                ("wxWidgets-3.2.4/docs/license.txt", "license", "file"),
                ("wxWidgets-3.2.4/docs/LICENSE", "license.txt", "symlink"),
            ])
            output = root / "out"
            package_wxwidgets.safe_extract_tar_bz2(archive, output)
            self.assertEqual((output / "wxWidgets-3.2.4/docs/LICENSE").read_text(), "license")

    def test_safe_extract_rejects_traversal_and_absolute_paths(self):
        for member in ("../escape", "/absolute", "C:/drive", "wxWidgets-3.2.4/../../escape"):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                archive = self._archive(root, [(member, "x", "file")])
                with self.assertRaises(ValueError):
                    package_wxwidgets.safe_extract_tar_bz2(archive, root / "out")

    def test_safe_extract_rejects_escaping_symlink_and_hardlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = self._archive(root, [("wxWidgets-3.2.4/link", "../../outside", "symlink")])
            with self.assertRaises(ValueError):
                package_wxwidgets.safe_extract_tar_bz2(archive, root / "out")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = self._archive(root, [("wxWidgets-3.2.4/hard", "wxWidgets-3.2.4/file", "hardlink")])
            with self.assertRaises(ValueError):
                package_wxwidgets.safe_extract_tar_bz2(archive, root / "out")

    def test_safe_extract_rejects_duplicate_member(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = self._archive(root, [
                ("wxWidgets-3.2.4/file", "one", "file"),
                ("wxWidgets-3.2.4/file", "two", "file"),
            ])
            with self.assertRaises(ValueError):
                package_wxwidgets.safe_extract_tar_bz2(archive, root / "out")


if __name__ == "__main__":
    unittest.main(verbosity=2)
