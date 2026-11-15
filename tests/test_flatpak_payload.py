import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.prepare_flatpak_payload import (
    require_case_sensitive_parent,
    verify_payload_symlinks,
    write_payload_checksums,
)


class FlatpakPayloadTests(unittest.TestCase):
    @unittest.skipIf(sys.platform == "win32", "default Windows temporary filesystem is case-insensitive")
    def test_case_sensitive_parent_is_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            require_case_sensitive_parent(Path(temporary) / "payload")

    def test_case_alias_is_rejected(self):
        original_exists = Path.exists

        def alias_exists(path):
            if path.name == "RBLCASPROBE" and path.parent.name.startswith(".rbl-flatpak-case-probe-"):
                return True
            return original_exists(path)

        with tempfile.TemporaryDirectory() as temporary, patch.object(Path, "exists", alias_exists):
            with self.assertRaisesRegex(RuntimeError, "case-sensitive filesystem"):
                require_case_sensitive_parent(Path(temporary) / "payload")

    def test_payload_checksums_cover_regular_files_and_file_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "target.bin").write_bytes(b"payload")
            try:
                (root / "alias.bin").symlink_to("target.bin")
            except OSError:
                self.skipTest("symlink creation is unavailable")
            verify_payload_symlinks(root)
            write_payload_checksums(root)
            entries = (root / "SHA256SUMS").read_text(encoding="ascii").splitlines()
            self.assertEqual(len(entries), 2)
            self.assertTrue(any(entry.endswith("  target.bin") for entry in entries))
            self.assertTrue(any(entry.endswith("  alias.bin") for entry in entries))

    @unittest.skipIf(sys.platform == "win32", "POSIX symlink escape fixture")
    def test_escaping_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()
            (root / "escape").symlink_to("/etc/passwd")
            with self.assertRaisesRegex(RuntimeError, "escaping symlink"):
                verify_payload_symlinks(root)


if __name__ == "__main__":
    unittest.main()
