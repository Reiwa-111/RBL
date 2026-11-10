import pathlib
import os
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native" / "wx-ui" / "src" / "main_frame.cpp"


class FindInFilesPosixSecurityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = SOURCE.read_text(encoding="utf-8")
        start = text.index("void MainFrame::OnFindInFiles(")
        end = text.index("void MainFrame::OnCancelSearch(", start)
        cls.function = text[start:end]
        cls.source = text

    def test_posix_walk_is_descriptor_relative(self):
        function = self.function
        for required in (
            "OpenDirectoryPathNoFollow(root)",
            "::fdopendir(scan_fd)",
            "::readdir(directory_stream)",
            "::fstatat(directory.handle->fd, entry_name.c_str(), &entry_info, AT_SYMLINK_NOFOLLOW)",
            "::openat(directory.parent->fd,",
            "::openat(directory.handle->fd, filename.c_str(), file_flags)",
            "O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK",
            "O_NOFOLLOW | O_NONBLOCK",
        ):
            with self.subTest(required=required):
                self.assertIn(required, function)

    def test_posix_walk_does_not_reopen_entries_by_path(self):
        function = self.function
        self.assertNotIn("::lstat(directory_path.c_str()", function)
        self.assertNotIn("::open(directory_path.c_str()", function)
        self.assertNotIn("::open(path.c_str()", function)
        self.assertIn("file_info_before.st_dev != entry_info.st_dev", function)
        self.assertIn("file_info_before.st_ino != entry_info.st_ino", function)
        self.assertIn("directory.expected_device", function)
        self.assertIn("directory.expected_inode", function)
        self.assertIn("std::shared_ptr<PosixDirectoryHandle> parent", self.source)
        self.assertIn("std::shared_ptr<PosixDirectoryHandle> handle", self.source)
        self.assertIn("if (component == \"..\")", self.source)

    def test_filesystem_skips_are_reported_separately_from_limits(self):
        function = self.function
        self.assertIn("some paths were skipped", self.source)
        self.assertIn("results.skipped_paths.empty()", self.source)
        self.assertIn("results.truncated ? \" — scan limit reached; results may be incomplete\"", self.source)
        self.assertNotIn("results.truncated = true;", function[function.index("const auto record_skipped_path"):function.index("#ifdef _WIN32\n            WinDirectoryChainGuard")])

    @unittest.skipUnless(os.name == "posix", "POSIX directory descriptor semantics")
    def test_pinned_directory_fd_survives_parent_symlink_swap(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = pathlib.Path(temporary)
            workspace = base / "workspace"
            outside = base / "outside"
            workspace.mkdir()
            outside.mkdir()
            (workspace / "needle.txt").write_text("inside", encoding="utf-8")
            (outside / "needle.txt").write_text("outside", encoding="utf-8")

            flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
            pinned_fd = os.open(workspace, flags)
            try:
                moved_workspace = base / "workspace-moved"
                workspace.rename(moved_workspace)
                workspace.symlink_to(outside, target_is_directory=True)

                child_fd = os.open("needle.txt", os.O_RDONLY, dir_fd=pinned_fd)
                try:
                    self.assertEqual(os.read(child_fd, 32), b"inside")
                finally:
                    os.close(child_fd)
                self.assertEqual((workspace / "needle.txt").read_text(encoding="utf-8"), "outside")
            finally:
                os.close(pinned_fd)


if __name__ == "__main__":
    unittest.main()
