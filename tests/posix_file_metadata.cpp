#include "file_metadata.hpp"

#include <algorithm>
#include <cerrno>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <stdexcept>
#include <system_error>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

namespace {

bool WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    return output.good();
}

bool ReadFileEquals(const std::filesystem::path& path, const std::string& expected) {
    std::ifstream input(path, std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    return input.good() || input.eof() ? actual == expected : false;
}

rbl::wxui::FileIdentity IdentityOf(const std::filesystem::path& path) {
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        throw std::system_error(errno, std::generic_category(), "lstat");
    }
    return {static_cast<std::uint64_t>(info.st_dev),
            static_cast<std::uint64_t>(info.st_ino), true};
}

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() /
        ("rbl-posix-metadata-" + std::to_string(::getpid()));
    std::error_code cleanup_error;
    fs::remove_all(directory, cleanup_error);
    fs::create_directories(directory);

    try {
        const auto source = directory / "source.rbl";
        const auto temporary = directory / "temporary.rbl";
        Require(WriteFile(source, "original bytes\n"), "could not create source file");
        Require(WriteFile(temporary, "new bytes\n"), "could not create temporary file");
        Require(::chmod(source.c_str(), 0640) == 0, "chmod source failed");

        bool xattr_supported = false;
#ifdef __linux__
        constexpr char xattr_name[] = "user.rblstudio.test";
        constexpr char xattr_value[] = "metadata-preserved";
        if (::setxattr(source.c_str(), xattr_name, xattr_value, sizeof(xattr_value) - 1, 0) == 0) {
            xattr_supported = true;
        } else if (errno != ENOTSUP && errno != EOPNOTSUPP) {
            throw std::system_error(errno, std::generic_category(), "setxattr source");
        }
#endif

        const auto source_identity = IdentityOf(source);
        const auto temporary_identity = IdentityOf(temporary);
        bool metadata_warning = false;
        std::error_code error;
        Require(rbl::wxui::CopyPosixMetadata(source, temporary, source_identity,
                    temporary_identity, metadata_warning, error),
                "CopyPosixMetadata failed: " + error.message());

        struct stat source_info{};
        struct stat temporary_info{};
        Require(::stat(source.c_str(), &source_info) == 0, "stat source failed");
        Require(::stat(temporary.c_str(), &temporary_info) == 0, "stat temporary failed");
        Require((source_info.st_mode & 07777) == (temporary_info.st_mode & 07777),
                "mode bits were not preserved");
        Require(source_info.st_uid == temporary_info.st_uid, "owner was not preserved");
        Require(source_info.st_gid == temporary_info.st_gid, "group was not preserved");
        Require(ReadFileEquals(source, "original bytes\n"), "source contents changed");
        Require(ReadFileEquals(temporary, "new bytes\n"), "temporary contents changed");
        std::string read_bytes;
        rbl::wxui::FileIdentity read_identity;
        std::error_code read_error;
        Require(rbl::wxui::ReadFileNoFollow(source, 1024, read_bytes, read_identity, read_error),
                "ReadFileNoFollow failed: " + read_error.message());
        Require(read_bytes == "original bytes\n", "ReadFileNoFollow returned incorrect bytes");
        Require(rbl::wxui::SameFileIdentity(source_identity, read_identity),
                "ReadFileNoFollow returned an unexpected identity");
        rbl::wxui::FileIdentity queried_identity;
        Require(rbl::wxui::QueryFileIdentityNoFollow(source, queried_identity, read_error),
                "QueryFileIdentityNoFollow failed: " + read_error.message());
        Require(rbl::wxui::SameFileIdentity(source_identity, queried_identity),
                "QueryFileIdentityNoFollow returned an unexpected identity");
        const int at_directory_fd = rbl::wxui::OpenDirectoryNoFollow(directory, read_error);
        Require(at_directory_fd >= 0, "OpenDirectoryNoFollow failed: " + read_error.message());
        Require(rbl::wxui::ReadFileAt(at_directory_fd, "source.rbl", 1024,
                    read_bytes, read_identity, read_error),
                "ReadFileAt failed: " + read_error.message());
        Require(read_bytes == "original bytes\n" &&
                rbl::wxui::SameFileIdentity(source_identity, read_identity),
                "ReadFileAt returned wrong content or identity");
        Require(rbl::wxui::QueryFileIdentityAt(at_directory_fd, "source.rbl",
                    queried_identity, read_error) &&
                rbl::wxui::SameFileIdentity(source_identity, queried_identity),
                "QueryFileIdentityAt returned wrong identity");
        Require(rbl::wxui::CopyPosixMetadataAt(at_directory_fd, "source.rbl", "temporary.rbl",
                    source_identity, temporary_identity, metadata_warning, read_error),
                "CopyPosixMetadataAt failed: " + read_error.message());
        const auto at_symlink = directory / "source-at-symlink.rbl";
        Require(::symlink(source.c_str(), at_symlink.c_str()) == 0,
                "create At API symlink failed");
        Require(!rbl::wxui::ReadFileAt(at_directory_fd, at_symlink.filename().string(), 1024,
                    read_bytes, read_identity, read_error),
                "ReadFileAt followed a symlink leaf");
        Require(!rbl::wxui::QueryFileIdentityAt(at_directory_fd, at_symlink.filename().string(),
                    queried_identity, read_error),
                "QueryFileIdentityAt accepted a symlink leaf");
        fs::remove(at_symlink);
        ::close(at_directory_fd);
        std::cout << "descriptor-relative-at-apis: PASS\n";

        const auto pinned_parent = directory / "pinned-parent";
        const auto outside_parent = directory / "outside-parent";
        const auto moved_parent = directory / "pinned-parent-moved";
        fs::create_directory(pinned_parent);
        fs::create_directory(outside_parent);
        Require(WriteFile(pinned_parent / "needle.rbl", "inside pinned parent\n"),
                "create pinned-parent file failed");
        Require(WriteFile(outside_parent / "needle.rbl", "outside symlink target\n"),
                "create outside-parent file failed");
        const int pinned_parent_fd = rbl::wxui::OpenDirectoryNoFollow(pinned_parent, read_error);
        Require(pinned_parent_fd >= 0, "open pinned parent failed: " + read_error.message());
        fs::rename(pinned_parent, moved_parent);
        Require(::symlink(outside_parent.c_str(), pinned_parent.c_str()) == 0,
                "replace pinned parent pathname with symlink failed");
        const bool pinned_read_succeeded = rbl::wxui::ReadFileAt(pinned_parent_fd,
            "needle.rbl", 1024, read_bytes, read_identity, read_error);
        if (pinned_read_succeeded) {
            Require(read_bytes == "inside pinned parent\n",
                    "pinned directory descriptor followed replacement symlink");
            std::cout << "pinned-parent-rename-symlink-swap: PASS\n";
        } else {
            // DrvFS can invalidate relative operations on an open directory
            // after rename. Fail-closed is acceptable there; following the
            // replacement symlink or returning partial data is not.
            Require(read_error.value() != 0,
                    "failed ReadFileAt returned a success error_code");
            Require(read_bytes.empty(),
                    "failed ReadFileAt returned partial/untrusted bytes");
            std::cout << "pinned-parent-rename-symlink-swap: SKIP (filesystem failed closed)\n";
        }
        const int pinned_created_fd = ::openat(pinned_parent_fd, "created-through-dirfd.rbl",
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (pinned_created_fd >= 0) {
            ::close(pinned_created_fd);
            Require(!fs::exists(outside_parent / "created-through-dirfd.rbl"),
                    "openat escaped through replacement symlink");
        } else {
            const int create_error = errno != 0 ? errno : EIO;
            Require(!fs::exists(outside_parent / "created-through-dirfd.rbl"),
                    "failed openat create nevertheless wrote through replacement symlink");
            std::cout << "pinned-parent-dirfd-create: SKIP (" <<
                std::error_code(create_error, std::generic_category()).message() << ")\n";
        }
        ::close(pinned_parent_fd);
        const auto source_alias = directory / "source-hardlink.rbl";
        Require(::link(source.c_str(), source_alias.c_str()) == 0, "create source hard link failed");
        Require(rbl::wxui::ReadFileNoFollow(source, 1024, read_bytes, read_identity, read_error),
                "ReadFileNoFollow failed for hard-linked source: " + read_error.message());
        Require(read_identity.link_count == 2, "ReadFileNoFollow did not report the hard-link count");
        fs::remove(source_alias);
        std::cout << "hard-link-count: PASS\n";
        const auto replacement_backup = directory / "source-replacement-backup.rbl";
        Require(::link(source.c_str(), replacement_backup.c_str()) == 0,
                "create replacement backup hard link failed");
        Require(rbl::wxui::ReadFileNoFollow(source, 1024, read_bytes, read_identity, read_error),
                "ReadFileNoFollow failed after creating replacement backup: " + read_error.message());
        Require(read_identity.link_count == 2,
                "replacement backup did not increment the original inode link count");
        fs::remove(replacement_backup);
        std::cout << "replacement-backup-link-count: PASS\n";
        const auto real_parent = directory / "real-parent";
        const auto symlink_parent = directory / "symlink-parent";
        fs::create_directory(real_parent);
        const auto parent_file = real_parent / "parent-file.rbl";
        Require(WriteFile(parent_file, "parent traversal guard\n"), "create parent file failed");
        rbl::wxui::FileIdentity parent_file_identity;
        Require(rbl::wxui::QueryFileIdentityNoFollow(parent_file, parent_file_identity, read_error),
                "query parent file identity failed: " + read_error.message());
        Require(::symlink(real_parent.c_str(), symlink_parent.c_str()) == 0,
                "create parent symlink failed");
        Require(!rbl::wxui::ReadFileNoFollow(symlink_parent / parent_file.filename(), 1024,
                    read_bytes, read_identity, read_error),
                "ReadFileNoFollow followed a parent symlink");
        Require(!rbl::wxui::QueryFileIdentityNoFollow(
                    symlink_parent / parent_file.filename(), queried_identity, read_error),
                "QueryFileIdentityNoFollow followed a parent symlink");
        const auto parent_temp = real_parent / "parent-temp.rbl";
        Require(WriteFile(parent_temp, "temporary metadata target\n"),
                "create parent metadata temp failed");
        bool parent_metadata_warning = false;
        std::error_code parent_metadata_error;
        Require(!rbl::wxui::CopyPosixMetadata(
                    symlink_parent / parent_file.filename(), symlink_parent / parent_temp.filename(),
                    parent_file_identity, IdentityOf(parent_temp),
                    parent_metadata_warning, parent_metadata_error),
                "CopyPosixMetadata followed a parent symlink");
        std::cout << "parent-symlink-components-rejected: PASS\n";

        // A pinned root descriptor must remain anchored to the originally
        // selected directory even if its pathname is replaced by a symlink.
        const auto pinned_root = directory / "pinned-root";
        const auto pinned_moved = directory / "pinned-root-moved";
        const auto outside_root = directory / "outside-root";
        fs::create_directories(pinned_root / "child");
        fs::create_directories(outside_root / "child");
        Require(WriteFile(pinned_root / "child" / "inside.rbl", "inside pinned root\n"),
                "create pinned-root file failed");
        Require(WriteFile(outside_root / "child" / "inside.rbl", "outside replacement\n"),
                "create outside-root file failed");
        std::error_code root_error;
        const int pinned_root_fd = rbl::wxui::OpenDirectoryNoFollow(pinned_root, root_error);
        Require(pinned_root_fd >= 0, "OpenDirectoryNoFollow failed: " + root_error.message());
        fs::rename(pinned_root, pinned_moved);
        Require(::symlink(outside_root.c_str(), pinned_root.c_str()) == 0,
                "replace root pathname with symlink failed");
        const int symlink_root_fd = rbl::wxui::OpenDirectoryNoFollow(pinned_root, root_error);
        if (symlink_root_fd >= 0) ::close(symlink_root_fd);
        Require(symlink_root_fd < 0, "OpenDirectoryNoFollow accepted a symlink root");
        const int child_fd = ::openat(pinned_root_fd, "child", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (child_fd >= 0) {
            const int pinned_file_fd = ::openat(child_fd, "inside.rbl", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
            Require(pinned_file_fd >= 0, "openat pinned-root file failed");
            char pinned_bytes[64]{};
            const auto pinned_read = ::read(pinned_file_fd, pinned_bytes, sizeof(pinned_bytes));
            Require(pinned_read > 0 && std::string(pinned_bytes, static_cast<std::size_t>(pinned_read)) ==
                        "inside pinned root\n",
                    "pinned root descriptor followed the replacement pathname");
            ::close(pinned_file_fd);
            ::close(child_fd);
            std::cout << "pinned-root-descriptor-survives-path-replacement: PASS\n";
        } else {
            const int open_error = errno != 0 ? errno : EIO;
            std::cout << "pinned-root-descriptor-survives-path-replacement: SKIP (" <<
                std::error_code(open_error, std::generic_category()).message() << ")\n";
        }
        ::close(pinned_root_fd);
        fs::remove(pinned_root);
        std::string oversized_bytes;
        rbl::wxui::FileIdentity oversized_identity;
        Require(!rbl::wxui::ReadFileNoFollow(source, 1, oversized_bytes,
                    oversized_identity, read_error),
                "ReadFileNoFollow accepted a file over its byte limit");
        Require(read_error == std::errc::file_too_large,
                "ReadFileNoFollow returned the wrong oversized-file error");
#ifdef __linux__
        if (xattr_supported) {
            char value[64]{};
            const auto size = ::getxattr(temporary.c_str(), xattr_name, value, sizeof(value));
            Require(size == static_cast<ssize_t>(sizeof(xattr_value) - 1),
                    "user xattr size was not preserved");
            Require(std::string(value, static_cast<std::size_t>(size)) == xattr_value,
                    "user xattr value was not preserved");
        }

        // A new temporary file inherits the directory's default ACL. Saving
        // over a source without an access ACL must remove that inherited ACL,
        // otherwise named users/groups can gain access after replacement.
        struct PosixAclEntry {
            std::uint16_t tag;
            std::uint16_t permissions;
            std::uint32_t id;
        };
        struct PosixAclXattr {
            std::uint32_t version;
            PosixAclEntry entries[5];
        };
        static_assert(sizeof(PosixAclEntry) == 8);
        static_assert(sizeof(PosixAclXattr) == 44);
        const auto acl_directory = directory / "default-acl";
        fs::create_directory(acl_directory);
        const auto acl_source = acl_directory / "source-without-acl.rbl";
        const auto acl_temporary = acl_directory / "inherited-acl.rbl";
        Require(WriteFile(acl_source, "source without access ACL\n"),
                "create no-ACL source failed");
        Require(::chmod(acl_source.c_str(), 0640) == 0, "chmod no-ACL source failed");
        const PosixAclXattr default_acl{
            0x0002,
            {
                {0x01, 0x06, 0xFFFFFFFFU}, // ACL_USER_OBJ: rw-
                {0x04, 0x00, 0xFFFFFFFFU}, // ACL_GROUP_OBJ: ---
                {0x08, 0x04, static_cast<std::uint32_t>(::getgid() + 1)}, // named group: r--
                {0x10, 0x04, 0xFFFFFFFFU}, // ACL_MASK: r--
                {0x20, 0x00, 0xFFFFFFFFU}, // ACL_OTHER: ---
            },
        };
        if (::setxattr(acl_directory.c_str(), "system.posix_acl_default",
                       &default_acl, sizeof(default_acl), 0) == 0) {
            Require(WriteFile(acl_temporary, "temporary inherits default ACL\n"),
                    "create inherited-ACL temporary failed");
            const auto inherited_acl_size = ::getxattr(
                acl_temporary.c_str(), "system.posix_acl_access", nullptr, 0);
            Require(inherited_acl_size > 0, "temporary did not inherit the test default ACL");
            bool acl_warning = false;
            std::error_code acl_error;
            Require(rbl::wxui::CopyPosixMetadata(acl_source, acl_temporary,
                        IdentityOf(acl_source), IdentityOf(acl_temporary), acl_warning, acl_error),
                    "CopyPosixMetadata failed to remove inherited ACL: " + acl_error.message());
            errno = 0;
            const auto remaining_acl_size = ::getxattr(
                acl_temporary.c_str(), "system.posix_acl_access", nullptr, 0);
            Require(remaining_acl_size < 0 && errno == ENODATA,
                    "temporary retained an inherited access ACL absent from source");
            std::cout << "inherited-default-acl-removed: PASS\n";

            const auto acl_source_with_acl = directory / "source-with-access-acl.rbl";
            const auto acl_temporary_with_acl = acl_directory / "temporary-with-access-acl.rbl";
            Require(WriteFile(acl_source_with_acl, "source with explicit access ACL\n"),
                    "create explicit-ACL source failed");
            Require(::setxattr(acl_source_with_acl.c_str(), "system.posix_acl_access",
                               &default_acl, sizeof(default_acl), 0) == 0,
                    "set explicit source access ACL failed");
            Require(WriteFile(acl_temporary_with_acl, "temporary inherits a different ACL\n"),
                    "create explicit-ACL temporary failed");
            bool explicit_acl_warning = false;
            std::error_code explicit_acl_error;
            Require(rbl::wxui::CopyPosixMetadata(acl_source_with_acl, acl_temporary_with_acl,
                        IdentityOf(acl_source_with_acl), IdentityOf(acl_temporary_with_acl),
                        explicit_acl_warning, explicit_acl_error),
                    "CopyPosixMetadata failed to copy explicit ACL: " + explicit_acl_error.message());
            std::array<char, sizeof(PosixAclXattr)> source_acl_bytes{};
            std::array<char, sizeof(PosixAclXattr)> copied_acl_bytes{};
            const auto source_acl_size = ::getxattr(acl_source_with_acl.c_str(),
                "system.posix_acl_access", source_acl_bytes.data(), source_acl_bytes.size());
            const auto copied_acl_size = ::getxattr(acl_temporary_with_acl.c_str(),
                "system.posix_acl_access", copied_acl_bytes.data(), copied_acl_bytes.size());
            Require(source_acl_size > 0 && copied_acl_size == source_acl_size &&
                    std::equal(source_acl_bytes.begin(), source_acl_bytes.begin() + source_acl_size,
                               copied_acl_bytes.begin()),
                    "explicit source access ACL was not copied exactly");
            std::cout << "explicit-access-acl-copied: PASS\n";
        } else if (errno != ENOTSUP && errno != EOPNOTSUPP) {
            throw std::system_error(errno, std::generic_category(), "setxattr default ACL");
        }
#endif
        std::cout << "metadata-copy: PASS (mode/owner/group"
                  << (xattr_supported ? "/user-xattr" : "")
                  << (metadata_warning ? "; warning reported" : "") << ")\n";

        // A symlink at either pathname must not be followed.
        const auto source_link = directory / "source-link.rbl";
        Require(::symlink(source.c_str(), source_link.c_str()) == 0, "create source symlink failed");
        std::string symlink_bytes;
        rbl::wxui::FileIdentity symlink_identity;
        Require(!rbl::wxui::ReadFileNoFollow(source_link, 1024, symlink_bytes,
                    symlink_identity, read_error),
                "ReadFileNoFollow followed a source symlink");
        const auto symlink_result = rbl::wxui::CopyPosixMetadata(
            source_link, temporary, source_identity, temporary_identity, metadata_warning, error);
        Require(!symlink_result, "source symlink was followed");
        fs::remove(source_link);
        std::cout << "source-symlink-rejected: PASS\n";

        const auto temporary_link = directory / "temporary-link.rbl";
        Require(::symlink(temporary.c_str(), temporary_link.c_str()) == 0,
                "create temporary symlink failed");
        const auto temporary_link_result = rbl::wxui::CopyPosixMetadata(
            source, temporary_link, source_identity, temporary_identity, metadata_warning, error);
        Require(!temporary_link_result, "temporary symlink was followed");
        fs::remove(temporary_link);
        std::cout << "temporary-symlink-rejected: PASS\n";

        auto wrong_identity = source_identity;
        ++wrong_identity.second;
        const auto mismatch_result = rbl::wxui::CopyPosixMetadata(
            source, temporary, wrong_identity, temporary_identity, metadata_warning, error);
        Require(!mismatch_result, "source identity mismatch was accepted");
        Require(ReadFileEquals(temporary, "new bytes\n"),
                "temporary contents changed after identity mismatch");
        std::cout << "source-identity-mismatch: PASS\n";

        const auto displaced_temporary = directory / "temporary-original.rbl";
        fs::rename(temporary, displaced_temporary);
        Require(WriteFile(temporary, "substituted temporary\n"),
                "create substituted temporary failed");
        const auto temporary_replacement_result = rbl::wxui::CopyPosixMetadata(
            source, temporary, source_identity, temporary_identity, metadata_warning, error);
        Require(!temporary_replacement_result, "temporary path replacement was accepted");
        Require(ReadFileEquals(temporary, "substituted temporary\n"),
                "substituted temporary contents changed");
        std::cout << "temporary-path-replacement: PASS\n";
        const auto current_temporary_identity = IdentityOf(temporary);

        // A raced FIFO must be rejected without blocking the caller. O_NONBLOCK
        // is ignored for regular files but prevents an attacker-controlled
        // replacement from hanging the GUI during open().
        const auto source_fifo = directory / "source-fifo";
        if (::mkfifo(source_fifo.c_str(), 0600) == 0) {
            std::string fifo_bytes;
            rbl::wxui::FileIdentity fifo_identity;
            Require(!rbl::wxui::ReadFileNoFollow(source_fifo, 1024, fifo_bytes,
                        fifo_identity, read_error),
                    "ReadFileNoFollow accepted a FIFO");
            Require(!rbl::wxui::CopyPosixMetadata(source_fifo, temporary,
                        source_identity, current_temporary_identity, metadata_warning, error),
                    "CopyPosixMetadata accepted a FIFO source");
            const auto temporary_fifo = directory / "temporary-fifo";
            if (::mkfifo(temporary_fifo.c_str(), 0600) == 0) {
                Require(!rbl::wxui::CopyPosixMetadata(source, temporary_fifo,
                            source_identity, current_temporary_identity, metadata_warning, error),
                        "CopyPosixMetadata accepted a FIFO temporary path");
                fs::remove(temporary_fifo);
                std::cout << "fifo-paths-rejected-without-blocking: PASS\n";
            } else {
                const int code = errno != 0 ? errno : EIO;
                Require(code == ENOTSUP || code == EOPNOTSUPP || code == EPERM || code == ENOSYS,
                        "create temporary FIFO failed: " + std::error_code(code, std::generic_category()).message());
                std::cout << "fifo-temporary-path: SKIP (filesystem does not support FIFO creation)\n";
            }
            fs::remove(source_fifo);
        } else {
            const int code = errno != 0 ? errno : EIO;
            Require(code == ENOTSUP || code == EOPNOTSUPP || code == EPERM || code == ENOSYS,
                    "create source FIFO failed: " + std::error_code(code, std::generic_category()).message());
            std::cout << "fifo-paths-rejected-without-blocking: SKIP (filesystem does not support FIFO creation)\n";
        }

        const auto old_source = directory / "source-old.rbl";
        const auto replacement_source = directory / "source-replacement.rbl";
        fs::rename(source, old_source);
        Require(WriteFile(replacement_source, "replacement source\n"),
                "create replacement source failed");
        fs::rename(replacement_source, source);
        const auto replaced_source_result = rbl::wxui::CopyPosixMetadata(
            source, temporary, source_identity, current_temporary_identity, metadata_warning, error);
        Require(!replaced_source_result, "replaced source path identity was accepted");
        Require(ReadFileEquals(source, "replacement source\n"),
                "replacement source contents changed");
        Require(ReadFileEquals(temporary, "substituted temporary\n"),
                "temporary contents changed after source path replacement");
        std::cout << "source-path-replacement: PASS\n";

        // Exercise the same-directory hard-link recovery primitives used by
        // POSIX Save before an atomic exchange. This checks preservation of
        // both old and new bytes independently of the replacement operation.
        const auto atomic_target = directory / "atomic-target.rbl";
        const auto atomic_temporary = directory / "atomic-temporary.rbl";
        const auto atomic_backup = directory / ".rblstudio-backup-test";
        const auto atomic_recovery = directory / ".rblstudio-recovery-test";
        Require(WriteFile(atomic_target, "displaced bytes\n"), "create atomic target failed");
        Require(WriteFile(atomic_temporary, "replacement bytes\n"), "create atomic temporary failed");
        const auto displaced_identity = IdentityOf(atomic_target);
        const auto replacement_identity = IdentityOf(atomic_temporary);
        Require(::link(atomic_target.c_str(), atomic_backup.c_str()) == 0,
                "create destination recovery hard link failed");
        Require(::link(atomic_temporary.c_str(), atomic_recovery.c_str()) == 0,
                "create new-content recovery hard link failed");
        Require(rbl::wxui::SameFileIdentity(displaced_identity, IdentityOf(atomic_backup)),
                "destination recovery link identity mismatch");
        Require(rbl::wxui::SameFileIdentity(replacement_identity, IdentityOf(atomic_recovery)),
                "new-content recovery link identity mismatch");
        fs::rename(atomic_temporary, atomic_target);
        Require(ReadFileEquals(atomic_target, "replacement bytes\n"),
                "atomic replacement did not install new contents");
        Require(ReadFileEquals(atomic_backup, "displaced bytes\n"),
                "destination recovery link did not preserve old contents");
        Require(ReadFileEquals(atomic_recovery, "replacement bytes\n"),
                "new-content recovery link did not preserve new contents");
        Require(rbl::wxui::SameFileIdentity(replacement_identity, IdentityOf(atomic_target)),
                "installed target identity mismatch");
        Require(rbl::wxui::SameFileIdentity(displaced_identity, IdentityOf(atomic_backup)),
                "displaced target identity mismatch");
        fs::remove(atomic_backup);
        fs::remove(atomic_recovery);
        std::cout << "replacement-recovery-hardlinks: PASS\n";

        // First-save installation uses linkat() so the destination is created
        // only if it is still absent. A raced existing leaf must remain intact.
        const auto first_save_temporary = directory / "first-save-temporary.rbl";
        const auto first_save_target = directory / "first-save-target.rbl";
        const auto first_save_conflict = directory / "first-save-conflict.rbl";
        Require(WriteFile(first_save_temporary, "first-save bytes\n"),
                "create first-save temporary failed");
        Require(WriteFile(first_save_conflict, "raced leaf bytes\n"),
                "create first-save conflict failed");
        std::error_code first_save_open_error;
        const int first_save_dir_fd = rbl::wxui::OpenDirectoryNoFollow(directory, first_save_open_error);
        Require(first_save_dir_fd >= 0,
                "open first-save directory failed: " + first_save_open_error.message());
        const auto first_save_identity = IdentityOf(first_save_temporary);
        if (::linkat(first_save_dir_fd, "first-save-temporary.rbl",
                     first_save_dir_fd, "first-save-target.rbl", 0) == 0) {
            Require(ReadFileEquals(first_save_target, "first-save bytes\n"),
                    "linkat first-save did not install expected bytes");
            Require(rbl::wxui::SameFileIdentity(first_save_identity, IdentityOf(first_save_target)),
                    "linkat first-save installed an unexpected identity");
            errno = 0;
            Require(::linkat(first_save_dir_fd, "first-save-conflict.rbl",
                             first_save_dir_fd, "first-save-target.rbl", 0) != 0 && errno == EEXIST,
                    "linkat overwrote an already-created destination");
            Require(ReadFileEquals(first_save_target, "first-save bytes\n"),
                    "raced destination changed after exclusive linkat failure");
            Require(rbl::wxui::SameFileIdentity(first_save_identity, IdentityOf(first_save_target)),
                    "exclusive linkat failure changed the destination identity");
            std::cout << "linkat-first-save-exclusive-no-replace: PASS\n";
        } else {
            const int code = errno != 0 ? errno : EIO;
            Require(code == ENOTSUP || code == EOPNOTSUPP || code == EPERM || code == ENOSYS,
                    "linkat first-save failed: " + std::error_code(code, std::generic_category()).message());
            Require(!fs::exists(first_save_target),
                    "unsupported linkat first-save unexpectedly created a destination");
            std::cout << "linkat-first-save-exclusive-no-replace: SKIP (filesystem unsupported)\n";
        }
        ::close(first_save_dir_fd);

        const auto exchange_target = directory / "exchange-target.rbl";
        const auto exchange_temporary = directory / "exchange-temporary.rbl";
        Require(WriteFile(exchange_target, "raced destination bytes\n"),
                "create exchange target failed");
        Require(WriteFile(exchange_temporary, "new exchange bytes\n"),
                "create exchange temporary failed");
        const auto exchange_old_identity = IdentityOf(exchange_target);
        const auto exchange_new_identity = IdentityOf(exchange_temporary);
        std::error_code exchange_error;
        const int exchange_dir_fd = rbl::wxui::OpenDirectoryNoFollow(directory, exchange_error);
        Require(exchange_dir_fd >= 0, "open exchange directory failed: " + exchange_error.message());
        if (rbl::wxui::ExchangeFileNamesAt(exchange_dir_fd, "exchange-target.rbl",
                                           "exchange-temporary.rbl", exchange_error)) {
            Require(ReadFileEquals(exchange_target, "new exchange bytes\n"),
                    "atomic name exchange did not install the new target");
            Require(ReadFileEquals(exchange_temporary, "raced destination bytes\n"),
                    "atomic name exchange did not preserve the displaced target");
            Require(rbl::wxui::SameFileIdentity(exchange_new_identity, IdentityOf(exchange_target)),
                    "atomic name exchange installed an unexpected target identity");
            Require(rbl::wxui::SameFileIdentity(exchange_old_identity, IdentityOf(exchange_temporary)),
                    "atomic name exchange lost the displaced target identity");
            std::cout << "atomic-name-exchange-preserves-displaced-destination: PASS\n";
        } else if (exchange_error == std::errc::operation_not_supported) {
            Require(ReadFileEquals(exchange_target, "raced destination bytes\n"),
                    "unsupported exchange changed the destination");
            Require(ReadFileEquals(exchange_temporary, "new exchange bytes\n"),
                    "unsupported exchange changed the temporary file");
            Require(rbl::wxui::SameFileIdentity(exchange_old_identity, IdentityOf(exchange_target)),
                    "unsupported exchange changed the destination identity");
            Require(rbl::wxui::SameFileIdentity(exchange_new_identity, IdentityOf(exchange_temporary)),
                    "unsupported exchange changed the temporary identity");
            std::cout << "atomic-exchange-unsupported-leaves-names-unchanged: SKIP (filesystem unsupported)\n";
        } else {
            throw std::system_error(exchange_error, "ExchangeFileNamesAt failed");
        }
        ::close(exchange_dir_fd);
    } catch (const std::exception& exception) {
        std::cerr << "posix-file-metadata: FAIL: " << exception.what() << '\n';
        fs::remove_all(directory, cleanup_error);
        return 1;
    }

    fs::remove_all(directory, cleanup_error);
    std::cout << "posix-file-metadata: PASS\n";
    return 0;
}
