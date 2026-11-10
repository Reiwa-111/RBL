from __future__ import annotations

import os
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native" / "wx-ui" / "src" / "main_frame.cpp"


class ProjectTreePosixSecurityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")
        start = cls.source.index("void MainFrame::PopulateProjectNode(")
        cls.populate = cls.source[start:]
        start = cls.source.index("void MainFrame::OpenDocumentPath(")
        end = cls.source.index("void MainFrame::", start + 1)
        cls.open_document = cls.source[start:end]
        start = cls.source.index("class ProjectPathData final")
        end = cls.source.index("struct ProjectSearchHit", start)
        cls.project_data = cls.source[start:end]

    def test_posix_project_tree_uses_pinned_root_and_descriptor_relative_walk(self):
        for required in (
            "OpenDirectoryNoFollow(path, error)",
            "std::shared_ptr<PosixProjectRoot> root",
            "std::filesystem::path relative_path",
            "std::vector<FileIdentity> ancestor_identities",
            "::openat(current.get(), name.c_str(), flags)",
            "O_DIRECTORY | O_NOFOLLOW",
            "::fdopendir(scan_fd)",
            "::readdir(directory_stream)",
            "::fstatat(::dirfd(directory_stream), name.c_str(), &entry_info,",
            "AT_SYMLINK_NOFOLLOW",
        ):
            with self.subTest(required=required):
                self.assertIn(required, self.source)

    def test_no_persistent_child_descriptors_and_root_is_shared(self):
        self.assertIn("std::shared_ptr<PosixProjectRoot> root", self.project_data)
        self.assertNotIn("int fd", self.project_data)
        self.assertNotIn("std::vector<int", self.project_data)
        self.assertIn("ScopedProjectFd current(::dup(data->root->fd))", self.populate)
        self.assertIn("const int next_fd = ::openat(current.get()", self.populate)
        self.assertIn("int scan_fd = !error ? ::dup(current.get()) : -1", self.populate)
        self.assertIn("::closedir(directory_stream)", self.populate)

    def test_every_directory_component_and_final_node_identity_are_verified(self):
        self.assertIn("SameProjectIdentity(data->ancestor_identities[component_index], component_info)",
                      self.populate)
        self.assertIn("SameProjectIdentity(data->expected_identity, directory_info)", self.populate)
        self.assertIn("static_cast<std::uint64_t>(entry_info.st_nlink)", self.populate)
        self.assertIn("SameFileIdentity(*expected_identity, opened_identity)", self.open_document)
        self.assertNotIn("expected_identity->link_count != opened_identity.link_count", self.open_document)

    def test_project_file_open_checks_enumerated_identity_after_read(self):
        self.assertIn("&data->expected_identity", self.source)
        self.assertIn("ReadFileNoFollow(path, MaxEditableFileBytes, bytes, opened_identity, read_error)",
                      self.open_document)
        self.assertIn("SameFileIdentity(*expected_identity, opened_identity)", self.open_document)
        self.assertIn("document.file_identity.valid", self.open_document)
        self.assertIn("SameFileIdentity(*expected_identity, document.file_identity)", self.open_document)
        self.assertIn("document.file_identity = opened_identity", self.open_document)
        self.assertIn("document.file_identity = installed_identity", self.source)

    def test_expected_identity_does_not_accept_existing_tab_without_matching_identity(self):
        existing_tab = self.open_document[
            self.open_document.index("for (const auto& [page, document] : documents_)"):]
        self.assertIn("if (expected_identity &&", existing_tab)
        self.assertIn("!document.file_identity.valid", existing_tab)
        self.assertIn("!SameFileIdentity(*expected_identity, document.file_identity)", existing_tab)
        self.assertIn("The already-open document does not match", existing_tab)

    def test_search_activation_rechecks_document_identity_before_navigation(self):
        start = self.source.index("void MainFrame::OnSearchResultActivated(")
        end = self.source.index("void MainFrame::PopulateProjectNode(", start)
        activation = self.source[start:end]
        identity_gate = activation.index("SameFileIdentity(document->second.file_identity, *expected_identity)")
        navigation = activation.index("editor->GotoPos(position)")
        self.assertLess(identity_gate, navigation)
        rejection = activation[activation.index("if (document == documents_.end()"):
                                  activation.index("long line = 1;")]
        self.assertIn("expected_identity", rejection)
        self.assertIn("!document->second.file_identity.valid", activation)

    def test_search_identity_vector_uses_actual_inserted_row_and_reset(self):
        start = self.source.index("void MainFrame::OnSearchResultsTimer(")
        end = self.source.index("void MainFrame::OnSearchResultActivated(", start)
        timer = self.source[start:end]
        self.assertIn("if (row < 0) continue", timer)
        self.assertIn("const auto row_index = static_cast<std::size_t>(row)", timer)
        self.assertIn("search_result_identities_.resize(row_index + 1)", timer)
        self.assertIn("search_result_identities_[row_index] = hit.identity", timer)
        self.assertIn("search_results_->DeleteAllItems();\n    search_result_identities_.clear();", self.source)

    def test_find_results_carry_opened_file_identity_to_activation(self):
        self.assertIn("FileIdentity identity;", self.source)
        self.assertIn("search_file_identity = {", self.source)
        self.assertIn("ToUtf8(snippet.Left(220)), search_file_identity", self.source)
        self.assertIn("hit.identity", self.source)
        self.assertIn("search_result_identities_[row_index] = hit.identity", self.source)
        self.assertIn("OpenDocumentPath(path_text, expected_identity)", self.source)

    @unittest.skipUnless(os.name == "posix", "POSIX directory descriptor semantics")
    def test_pinned_root_survives_root_path_replacement_with_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = pathlib.Path(temporary)
            workspace = base / "workspace"
            outside = base / "outside"
            workspace.mkdir()
            outside.mkdir()
            (workspace / "inside.txt").write_text("inside", encoding="utf-8")
            (outside / "inside.txt").write_text("outside", encoding="utf-8")
            flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
            root_fd = os.open(workspace, flags)
            try:
                workspace.rename(base / "workspace-moved")
                workspace.symlink_to(outside, target_is_directory=True)
                child_fd = os.open("inside.txt", os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0),
                                   dir_fd=root_fd)
                try:
                    self.assertEqual(os.read(child_fd, 32), b"inside")
                finally:
                    os.close(child_fd)
                self.assertEqual((workspace / "inside.txt").read_text(encoding="utf-8"), "outside")
            finally:
                os.close(root_fd)

    @unittest.skipUnless(os.name == "posix", "POSIX directory descriptor semantics")
    def test_parent_symlink_swap_is_rejected_during_component_walk(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = pathlib.Path(temporary)
            root = base / "root"
            outside = base / "outside"
            nested = root / "nested"
            nested.mkdir(parents=True)
            outside.mkdir()
            flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
            root_fd = os.open(root, flags)
            try:
                nested.rename(root / "nested-old")
                nested.symlink_to(outside, target_is_directory=True)
                with self.assertRaises(OSError):
                    os.open("nested", flags, dir_fd=root_fd)
            finally:
                os.close(root_fd)

    @unittest.skipUnless(os.name == "posix", "POSIX inode identity semantics")
    def test_same_name_directory_replacement_fails_identity_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            child = root / "child"
            child.mkdir()
            original = child.stat(follow_symlinks=False)
            child.rename(root / "old-child")
            child.mkdir()
            replacement = child.stat(follow_symlinks=False)
            self.assertNotEqual((original.st_dev, original.st_ino),
                                (replacement.st_dev, replacement.st_ino))

    @unittest.skipUnless(os.name == "posix", "POSIX descriptor-relative lookup")
    def test_renamed_directory_cannot_be_reopened_by_stale_component(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            child = root / "child"
            child.mkdir()
            flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
            root_fd = os.open(root, flags)
            try:
                child.rename(root / "renamed")
                with self.assertRaises(FileNotFoundError):
                    os.open("child", flags, dir_fd=root_fd)
            finally:
                os.close(root_fd)


if __name__ == "__main__":
    unittest.main()
