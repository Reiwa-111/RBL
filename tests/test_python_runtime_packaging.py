from pathlib import Path
import io
import os
import stat
import tarfile
import tempfile
import unittest
import zipfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.package_python_runtime import (
    PACKAGES,
    SOURCE_PACKAGES,
    _verify_source_archives,
    _safe_link_target,
    extract_tar_safely,
    extract_zip_safely,
    safe_parts,
)


class PythonRuntimePackagingTests(unittest.TestCase):
    def test_pinned_runtime_assets_match_supported_platforms(self):
        self.assertEqual(PACKAGES["windows"]["sha256"], "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159")
        self.assertEqual(PACKAGES["linux"]["sha256"], "c2ff2e97422f5d3896a16e0317fd212ba977bbf2688d93a6f7a03d817506bd30")
        self.assertEqual(PACKAGES["linux"]["glibc_min"], "2.17")

    def test_corresponding_source_archives_are_pinned_and_verified(self):
        self.assertEqual(SOURCE_PACKAGES["cpython"]["sha256"], "de414a57119abef52784d8993e341d53aed635fc2b05c2607d9f98a2c5b72032")
        self.assertEqual(SOURCE_PACKAGES["python_build_standalone"]["source_ref"], "20261110 / 9244801782b78e4c4afc5c82a4524aa2101d3298")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            for metadata in SOURCE_PACKAGES.values():
                filename = metadata["filename"]
                (source / filename).write_bytes(b"source archive fixture")
                (source / f"{filename}.sha256").write_text(
                    metadata["sha256"] + f"  {filename}\n", encoding="ascii"
                )
            with self.assertRaises(RuntimeError):
                _verify_source_archives(root)

    def test_safe_parts_rejects_absolute_traversal_and_windows_paths(self):
        for value in ("../escape", "/absolute", "C:/drive", "python\\..\\escape"):
            with self.subTest(value=value):
                with self.assertRaises(RuntimeError):
                    safe_parts(value)

    def test_safe_link_target_allows_internal_parent_reference(self):
        self.assertEqual(
            _safe_link_target(Path("share/terminfo/1/1178"), "../a/adm1178"),
            "share/terminfo/a/adm1178",
        )

    def test_safe_link_target_rejects_escape(self):
        with self.assertRaises(RuntimeError):
            _safe_link_target(Path("bin/python3"), "../../../../outside")

    def test_zip_extraction_rejects_path_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            archive_path = base / "bad.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("../escape.txt", "not allowed")
            with self.assertRaises(RuntimeError):
                extract_zip_safely(archive_path, base / "out")
            self.assertFalse((base / "escape.txt").exists())

    def test_zip_extraction_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            archive_path = base / "link.zip"
            info = zipfile.ZipInfo("link")
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(info, "target")
            with self.assertRaises(RuntimeError):
                extract_zip_safely(archive_path, base / "out")

    def test_tar_extraction_rejects_path_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            archive_path = base / "bad.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                data = b"bad"
                member = tarfile.TarInfo("python/../../escape.txt")
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            with self.assertRaises(RuntimeError):
                extract_tar_safely(archive_path, base / "out")
            self.assertFalse((base / "escape.txt").exists())

    def test_tar_extraction_rejects_symlink_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            archive_path = base / "escape-link.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                target = tarfile.TarInfo("python/bin/python3.14")
                target.size = 1
                archive.addfile(target, io.BytesIO(b"x"))
                link = tarfile.TarInfo("python/bin/python3")
                link.type = tarfile.SYMTYPE
                link.linkname = "../../../../outside"
                archive.addfile(link)
            with self.assertRaises(RuntimeError):
                extract_tar_safely(archive_path, base / "out")

    @unittest.skipIf(os.name == "nt", "creating POSIX symlinks requires a Linux filesystem")
    def test_tar_extraction_preserves_safe_relative_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            archive_path = base / "good.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                target = tarfile.TarInfo("python/bin/python3.14")
                target.mode = 0o755
                target.size = 7
                archive.addfile(target, io.BytesIO(b"runtime"))
                link = tarfile.TarInfo("python/bin/python3")
                link.type = tarfile.SYMTYPE
                link.linkname = "python3.14"
                archive.addfile(link)
            output = base / "out"
            extract_tar_safely(archive_path, output)
            self.assertTrue((output / "bin/python3").is_symlink())
            self.assertEqual((output / "bin/python3").read_text(encoding="utf-8"), "runtime")


if __name__ == "__main__":
    unittest.main()
