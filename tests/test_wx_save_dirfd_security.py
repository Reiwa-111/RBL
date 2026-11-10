from __future__ import annotations

import errno
import os
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native" / "wx-ui" / "src" / "main_frame.cpp"
METADATA_HEADER = ROOT / "native" / "wx-ui" / "include" / "file_metadata.hpp"
METADATA_SOURCE = ROOT / "native" / "wx-ui" / "src" / "file_metadata.cpp"


class SaveDirfdSecurityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")
        start = cls.source.index("bool MainFrame::SaveDocument(")
        end = cls.source.index("bool MainFrame::SaveCurrent()", start)
        cls.save_document = cls.source[start:end]
        cls.metadata_source = METADATA_SOURCE.read_text(encoding="utf-8")

    def test_save_pins_parent_before_leaf_transaction(self) -> None:
        function = self.save_document
        pinned = function.index("OpenDirectoryNoFollow(target.parent_path()")
        initial_leaf_check = function.index("::fstatat(save_directory_guard.fd, target_leaf.c_str()")
        self.assertLess(pinned, initial_leaf_check)
        for operation in ("::openat(", "::linkat(", "::renameat(", "::unlinkat(",
                          "::fstatat(", "::fsync(save_directory_guard.fd)"):
            with self.subTest(operation=operation):
                self.assertIn(operation, self.source)
        self.assertGreaterEqual(function.count("::fsync(save_directory_guard.fd)"), 3)
        self.assertNotIn("save_directory_fd", function)
        self.assertNotIn("std::filesystem::rename(temporary, target", function)
        self.assertNotIn("std::filesystem::create_hard_link(temporary, target", function)

    def test_metadata_at_apis_are_declared_and_used(self) -> None:
        header = METADATA_HEADER.read_text(encoding="utf-8")
        for api in ("ReadFileAt(", "QueryFileIdentityAt(", "CopyPosixMetadataAt(", "ExchangeFileNamesAt("):
            with self.subTest(api=api):
                self.assertIn(api, header)
        for api in ("ReadFileAt(", "QueryFileIdentityAt(", "CopyPosixMetadataAt("):
            with self.subTest(save_api=api):
                self.assertIn(api, self.save_document)

    def test_windows_replace_path_remains_present(self) -> None:
        self.assertIn("::ReplaceFileW(target.c_str(), temporary.c_str()", self.save_document)
        self.assertIn("::MoveFileExW(temporary.c_str(), target.c_str()", self.save_document)

    def test_windows_existing_destination_fails_closed_without_new_content_recovery_link(self) -> None:
        function = self.save_document
        create = function.index("recovery_link_created = ::CreateHardLinkW(")
        failure = function.index("if (!recovery_link_created)", create)
        replace = function.index("::ReplaceFileW(target.c_str(), temporary.c_str()", failure)
        failure_block = function[failure:replace]
        self.assertIn("recovery_link_creation_failed = true", failure_block)
        self.assertIn("if (target_exists)", failure_block)
        self.assertIn("replace_error = std::error_code", failure_block)
        self.assertLess(failure, replace)

    def test_windows_validates_both_recovery_artifacts_before_cleanup(self) -> None:
        function = self.save_document
        cleanup = function[function.index("// Validate both recovery names before deleting either one."):]
        backup_validation = cleanup.index("QueryFileIdentity(replacement_backup_path")
        recovery_validation = cleanup.index("QueryFileIdentity(recovery_path")
        cleanup_gate = cleanup.index("if (!backup_cleanup_failed && !recovery_cleanup_failed)", recovery_validation)
        recovery_delete = cleanup.index("DeleteFileByIdentity(recovery_path, recovery_identity", cleanup_gate)
        backup_delete = cleanup.index("DeleteFileByIdentity(replacement_backup_path, backup_identity", recovery_delete)
        original_delete = cleanup.index("DeleteFileByIdentity(original_backup_path, original_backup_identity", backup_delete)
        self.assertLess(backup_validation, recovery_validation)
        self.assertLess(recovery_validation, cleanup_gate)
        self.assertLess(cleanup_gate, recovery_delete)
        self.assertLess(recovery_delete, backup_delete)
        self.assertLess(backup_delete, original_delete)

    def test_windows_original_destination_is_hardlinked_before_replace(self) -> None:
        function = self.save_document
        create_backup = function.index("CreateHardLinkW(\n                original_backup_path.c_str(), target.c_str()")
        replace_call = function.index("::ReplaceFileW(target.c_str(), temporary.c_str()")
        preflight = function.index("QueryFileIdentity(original_backup_path, current_original_backup_identity")
        self.assertLess(create_backup, preflight)
        self.assertLess(preflight, replace_call)
        self.assertIn("SameFileIdentity(original_identity, current_original_backup_identity)", function[preflight:replace_call])

    def test_windows_recovery_cleanup_deletes_the_opened_identity_by_handle(self) -> None:
        start = self.source.index("bool DeleteFileByIdentity(")
        end = self.source.index("bool WriteExclusiveTemporary(", start)
        helper = self.source[start:end]
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT", helper)
        self.assertIn("GetFileInformationByHandle(file, &info)", helper)
        self.assertIn("SameFileIdentity(expected, actual)", helper)
        self.assertIn("SetFileInformationByHandle(file, FileDispositionInfo", helper)

    def test_windows_save_pins_each_parent_without_delete_sharing(self) -> None:
        lock_start = self.source.index("bool LockWindowsDirectoryChain(")
        lock_end = self.source.index("#endif", lock_start)
        lock = self.source[lock_start:lock_end]
        self.assertIn("FILE_SHARE_READ | FILE_SHARE_WRITE", lock)
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT", lock)
        self.assertIn("FILE_FLAG_BACKUP_SEMANTICS", lock)
        self.assertNotIn("FILE_SHARE_DELETE", lock)

    def test_posix_directory_sync_stages_precede_replace_and_cleanup(self) -> None:
        function = self.save_document
        pre_sync = function.index("// Stage 1: make the temporary name")
        first_fsync = function.index("::fsync(save_directory_guard.fd)", pre_sync)
        exchange = function.index("ExchangeFileNamesAt(save_directory_guard.fd, temporary_leaf,")
        install_link = function.index("::linkat(save_directory_guard.fd, temporary_leaf.c_str()")
        post_sync = function.index("// Stage 2: persist the installed target")
        second_fsync = function.index("::fsync(save_directory_guard.fd)", post_sync)
        cleanup_sync = function.index("// Stage 3: persist removal/quarantine")
        third_fsync = function.index("::fsync(save_directory_guard.fd)", cleanup_sync)
        self.assertLess(first_fsync, exchange)
        self.assertLess(first_fsync, install_link)
        self.assertLess(exchange, second_fsync)
        self.assertLess(install_link, second_fsync)
        self.assertLess(second_fsync, third_fsync)

    def test_first_save_uses_temp_as_recovery_name_without_extra_hardlink(self) -> None:
        function = self.save_document
        recovery_create = function.index("recovery_link_created = CreateExclusiveSiblingHardLinkAt(")
        recovery_guard = function.rfind("if (!replace_error && target_exists)", 0, recovery_create)
        self.assertNotEqual(recovery_guard, -1)
        first_save_branch = function.index("if (!replace_error && !target_exists)")
        self.assertIn("query_save_leaf(temporary_leaf", function[first_save_branch:])
        self.assertIn("Keep the temporary name as the recovery name", function)
        self.assertNotIn("recovery_leaf", function[first_save_branch:function.index("if (!replace_error) {", first_save_branch)])

    def test_recovery_set_is_validated_before_either_artifact_is_removed(self) -> None:
        function = self.save_document
        validate_set = function.index("bool recovery_set_valid = true;")
        backup_validate = function.index("query_save_leaf(replacement_backup_leaf", validate_set)
        recovery_validate = function.index("query_save_leaf(recovery_leaf", backup_validate)
        decision = function.index("if (!recovery_set_valid)", recovery_validate)
        recovery_unlink = function.index("unlink_save_leaf(recovery_leaf", decision)
        backup_unlink = function.index("unlink_save_leaf(replacement_backup_leaf", recovery_unlink)
        self.assertLess(validate_set, backup_validate)
        self.assertLess(backup_validate, recovery_validate)
        self.assertLess(recovery_validate, decision)
        self.assertLess(decision, backup_unlink)
        self.assertLess(recovery_unlink, backup_unlink)

    def test_existing_destination_fails_closed_without_atomic_name_exchange(self) -> None:
        function = self.save_document
        exchange = function.index("ExchangeFileNamesAt(save_directory_guard.fd, temporary_leaf,")
        post_exchange = function.index("if (!replace_error && atomic_exchange_used)", exchange)
        exchange_block = function[exchange:post_exchange]
        self.assertIn("replace_error = exchange_error", exchange_block)
        self.assertNotIn("::renameat(", exchange_block)
        self.assertNotIn("atomic_exchange_unavailable", function)
        self.assertIn("safely replace an existing file", function)
        displaced_read = function.index("const bool displaced_read = ReadFileAt", post_exchange)
        displaced_validation = function.index("const bool displaced_is_original", displaced_read)
        rollback = function.index("ExchangeFileNamesAt(save_directory_guard.fd, temporary_leaf,", displaced_validation + 1)
        self.assertLess(exchange, displaced_validation)
        self.assertLess(displaced_validation, rollback)
        self.assertIn("HashBytes(displaced_bytes) == disk_hash_at_check", function[displaced_validation:])
        self.assertIn("if (!displaced_read)", function[displaced_read:])
        self.assertIn("query_save_leaf(temporary_leaf, displaced_identity, identity_error)", function[displaced_read:])
        self.assertIn("atomic_exchange_used", function)
        self.assertIn("displaced_cleanup_failed", function)
        self.assertIn("renameat2(RENAME_EXCHANGE)", function)

    def test_failed_posix_syscalls_never_return_success_error_code(self) -> None:
        for source in (self.source, self.metadata_source):
            with self.subTest(source="main_frame" if source is self.source else "file_metadata"):
                start = source.index("int PosixErrorOrIoError() noexcept")
                end = source.index("\n}", start) + 2
                helper = source[start:end]
                self.assertIn("const int code = errno", helper)
                self.assertIn("return code != 0 ? code : EIO", helper)

    def test_cleanup_quarantines_before_identity_check_and_unlink(self) -> None:
        helper_start = self.source.index("bool UnlinkLeafIfIdentityAt(",
                                        self.source.index("bool CreateExclusiveSiblingHardLinkAt("))
        helper_end = self.source.index("\n}\n", helper_start) + 3
        helper = self.source[helper_start:helper_end]
        self.assertIn("::mkdirat(parent_fd", helper)
        move = helper.index("::renameat(parent_fd, leaf.c_str(), quarantine_fd, \"entry\")")
        move_parent_sync = helper.index("::fsync(parent_fd)", move)
        move_quarantine_sync = helper.index("::fsync(quarantine_fd)", move)
        verify = helper.index("::fstatat(quarantine_fd, \"entry\"", move)
        remove = helper.index("::unlinkat(quarantine_fd, \"entry\"", verify)
        self.assertLess(move, verify)
        self.assertLess(move, move_parent_sync)
        self.assertLess(move_parent_sync, move_quarantine_sync)
        self.assertLess(move_quarantine_sync, verify)
        self.assertLess(verify, remove)
        self.assertNotIn("::unlinkat(parent_fd, leaf.c_str()", helper)
        self.assertIn("::fsync(parent_fd)", helper)
        self.assertIn("::fsync(quarantine_fd)", helper)

    @unittest.skipUnless(os.name == "posix", "POSIX dirfd semantics")
    def test_quarantine_swap_preserves_unexpected_replacement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl-save-quarantine-") as temp:
            parent = pathlib.Path(temp)
            expected = parent / "expected-inode"
            artifact = parent / "artifact"
            expected.write_bytes(b"expected")
            artifact.write_bytes(b"swapped-in")
            expected_stat = expected.stat()
            parent_fd = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
            quarantine_fd = -1
            try:
                quarantine = ".rblstudio-quarantine-test"
                os.mkdir(quarantine, 0o700, dir_fd=parent_fd)
                quarantine_fd = os.open(quarantine,
                    os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0),
                    dir_fd=parent_fd)
                # Model the adversarial replacement immediately before the
                # implementation's atomic rename into its private quarantine.
                os.rename("artifact", "entry", src_dir_fd=parent_fd,
                          dst_dir_fd=quarantine_fd)
                moved = os.stat("entry", dir_fd=quarantine_fd, follow_symlinks=False)
                self.assertNotEqual((moved.st_dev, moved.st_ino),
                                    (expected_stat.st_dev, expected_stat.st_ino))
                self.assertEqual(pathlib.Path(temp, quarantine, "entry").read_bytes(), b"swapped-in")
                # On identity mismatch the implementation retains this exact
                # quarantine path and must not unlink the unexpected object.
                self.assertTrue(pathlib.Path(temp, quarantine, "entry").exists())
            finally:
                if quarantine_fd >= 0:
                    os.close(quarantine_fd)
                os.close(parent_fd)

    @unittest.skipUnless(os.name == "posix", "POSIX dirfd semantics")
    def test_pinned_parent_survives_rename_and_symlink_replacement(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rbl-save-dirfd-") as temp:
            root = pathlib.Path(temp)
            parent = root / "parent"
            outside = root / "outside"
            moved = root / "parent-moved"
            parent.mkdir()
            outside.mkdir()
            (parent / "target.rbl").write_bytes(b"inside")
            (outside / "target.rbl").write_bytes(b"outside")

            flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
            parent_fd = os.open(parent, flags)
            try:
                parent.rename(moved)
                parent.symlink_to(outside, target_is_directory=True)

                try:
                    fd = os.open("temp.rbl", os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                                 0o600, dir_fd=parent_fd)
                except OSError as exc:
                    # DrvFS may invalidate relative operations through an
                    # already-open directory after its pathname is renamed.
                    # Treat only known unsupported/fail-closed errors as a
                    # filesystem limitation; verify the replacement symlink
                    # target was not modified before reporting the SKIP.
                    unsupported = {
                        errno.EIO, errno.ENXIO, errno.ENOTSUP,
                        errno.EOPNOTSUPP, errno.EPERM, errno.ENOSYS,
                    }
                    if exc.errno not in unsupported:
                        raise
                    self.assertEqual((outside / "target.rbl").read_bytes(), b"outside")
                    self.assertFalse((outside / "temp.rbl").exists())
                    self.skipTest(
                        "filesystem fails closed for openat through renamed directory fd: "
                        f"{exc}"
                    )
                os.write(fd, b"replacement")
                os.fsync(fd)
                os.close(fd)
                os.link("temp.rbl", "recovery.rbl", src_dir_fd=parent_fd,
                        dst_dir_fd=parent_fd, follow_symlinks=False)
                info = os.stat("recovery.rbl", dir_fd=parent_fd, follow_symlinks=False)
                temp_info = os.stat("temp.rbl", dir_fd=parent_fd, follow_symlinks=False)
                self.assertEqual((info.st_dev, info.st_ino), (temp_info.st_dev, temp_info.st_ino))
                os.rename("temp.rbl", "target.rbl", src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
                self.assertEqual((moved / "target.rbl").read_bytes(), b"replacement")
                self.assertEqual((outside / "target.rbl").read_bytes(), b"outside")
                os.unlink("recovery.rbl", dir_fd=parent_fd)
                os.fsync(parent_fd)
            finally:
                os.close(parent_fd)


if __name__ == "__main__":
    unittest.main()
