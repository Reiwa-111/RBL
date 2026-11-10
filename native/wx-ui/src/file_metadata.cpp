#include "file_metadata.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

namespace rbl::wxui {

#ifndef _WIN32
namespace {

int PosixErrorOrIoError() noexcept {
    const int code = errno;
    // Some Windows-backed WSL filesystems have returned a failed syscall with
    // errno left at zero. Never surface an error_code{} for a failed operation.
    return code != 0 ? code : EIO;
}
struct ScopedFileDescriptor {
    int value{-1};
    ~ScopedFileDescriptor() { if (value >= 0) ::close(value); }
    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;
    explicit ScopedFileDescriptor(int fd = -1) : value(fd) {}
};

int OpenDirectoryPathNoFollow(const std::filesystem::path& directory_path, std::error_code& error) {
#if !defined(O_NOFOLLOW) || !defined(O_DIRECTORY) || !defined(AT_SYMLINK_NOFOLLOW)
    error = std::make_error_code(std::errc::operation_not_supported);
    return -1;
#else
    std::error_code path_error;
    const auto absolute = std::filesystem::absolute(directory_path, path_error).lexically_normal();
    if (path_error) {
        error = path_error;
        return -1;
    }
    int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
    int current = ::open(absolute.root_path().c_str(), flags);
    if (current < 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return -1;
    }
    for (const auto& component : absolute.relative_path()) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            ::close(current);
            error = std::make_error_code(std::errc::invalid_argument);
            return -1;
        }
        const auto name = component.string();
        const int next = ::openat(current, name.c_str(), flags);
        if (next < 0) {
            const int saved_errno = PosixErrorOrIoError();
            ::close(current);
            error = std::error_code(saved_errno, std::generic_category());
            return -1;
        }
        struct stat info{};
        const int stat_result = ::fstat(next, &info);
        if (stat_result != 0 || !S_ISDIR(info.st_mode)) {
            const int saved_errno = stat_result != 0 ? PosixErrorOrIoError() : ENOTDIR;
            ::close(next);
            ::close(current);
            error = std::error_code(saved_errno, std::generic_category());
            return -1;
        }
        ::close(current);
        current = next;
    }
    error.clear();
    return current;
#endif
}

int OpenParentDirectoryNoFollow(const std::filesystem::path& file_path, std::error_code& error) {
    std::error_code path_error;
    const auto absolute = std::filesystem::absolute(file_path, path_error).lexically_normal();
    if (path_error) {
        error = path_error;
        return -1;
    }
    const auto leaf = absolute.filename();
    if (leaf.empty() || leaf == "." || leaf == "..") {
        error = std::make_error_code(std::errc::invalid_argument);
        return -1;
    }
    return OpenDirectoryPathNoFollow(absolute.parent_path(), error);
}
} // namespace
#endif

#ifndef _WIN32
int OpenDirectoryNoFollow(const std::filesystem::path& directory, std::error_code& error) {
    return OpenDirectoryPathNoFollow(directory, error);
}
#endif

bool ReadFileNoFollow(const std::filesystem::path& path,
                      std::uint64_t max_bytes,
                      std::string& bytes,
                      FileIdentity& identity,
                      std::error_code& error) {
    bytes.clear();
    identity = {};
#ifdef _WIN32
    std::vector<HANDLE> parent_handles;
    struct ParentHandleGuard {
        std::vector<HANDLE>& handles;
        ~ParentHandleGuard() {
            for (const HANDLE handle : handles) {
                if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
            }
        }
    } parent_guard{parent_handles};
    const auto parent_path = path.parent_path();
    auto current_directory = parent_path.root_path();
    if (current_directory.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const auto lock_parent = [&](const std::filesystem::path& directory) {
        const HANDLE handle = ::CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!::GetFileInformationByHandle(handle, &info)) {
            const DWORD code = ::GetLastError();
            ::CloseHandle(handle);
            error = std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            ::CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        parent_handles.push_back(handle);
        return true;
    };
    if (!lock_parent(current_directory)) return false;
    for (const auto& component : parent_path.relative_path()) {
        current_directory /= component;
        if (!lock_parent(current_directory)) return false;
    }

    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    BY_HANDLE_FILE_INFORMATION before{};
    if (!::GetFileInformationByHandle(file, &before)) {
        const DWORD code = ::GetLastError();
        ::CloseHandle(file);
        error = std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    if ((before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        ::CloseHandle(file);
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    const std::uint64_t file_size =
        (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32U) | before.nFileSizeLow;
    if (file_size > max_bytes || file_size > std::numeric_limits<std::size_t>::max()) {
        ::CloseHandle(file);
        error = std::make_error_code(std::errc::file_too_large);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(file_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD count = 0;
        if (!::ReadFile(file, bytes.data() + offset, request, &count, nullptr) || count == 0) {
            const DWORD code = ::GetLastError();
            ::CloseHandle(file);
            bytes.clear();
            error = code == ERROR_SUCCESS
                ? std::make_error_code(std::errc::state_not_recoverable)
                : std::error_code(static_cast<int>(code), std::system_category());
            return false;
        }
        offset += count;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    const bool stable = ::GetFileInformationByHandle(file, &after) &&
        after.dwVolumeSerialNumber == before.dwVolumeSerialNumber &&
        after.nFileIndexHigh == before.nFileIndexHigh &&
        after.nFileIndexLow == before.nFileIndexLow &&
        after.nFileSizeHigh == before.nFileSizeHigh &&
        after.nFileSizeLow == before.nFileSizeLow &&
        after.dwFileAttributes == before.dwFileAttributes &&
        after.ftLastWriteTime.dwHighDateTime == before.ftLastWriteTime.dwHighDateTime &&
        after.ftLastWriteTime.dwLowDateTime == before.ftLastWriteTime.dwLowDateTime;
    ::CloseHandle(file);
    if (!stable) {
        bytes.clear();
        error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    identity.first = before.dwVolumeSerialNumber;
    identity.second = (static_cast<std::uint64_t>(before.nFileIndexHigh) << 32U) |
        before.nFileIndexLow;
    identity.link_count = before.nNumberOfLinks;
    identity.valid = true;
    HANDLE verify = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (verify == INVALID_HANDLE_VALUE) {
        bytes.clear();
        identity = {};
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
        return false;
    }
    BY_HANDLE_FILE_INFORMATION path_after{};
    const bool same_path_object = ::GetFileInformationByHandle(verify, &path_after) &&
        (path_after.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
        path_after.dwVolumeSerialNumber == before.dwVolumeSerialNumber &&
        path_after.nFileIndexHigh == before.nFileIndexHigh &&
        path_after.nFileIndexLow == before.nFileIndexLow;
    ::CloseHandle(verify);
    if (!same_path_object) {
        bytes.clear();
        identity = {};
        error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
#else
    std::error_code parent_error;
    const auto absolute_path = std::filesystem::absolute(path, parent_error).lexically_normal();
    if (parent_error) {
        error = parent_error;
        return false;
    }
    ScopedFileDescriptor parent_directory(OpenParentDirectoryNoFollow(absolute_path, parent_error));
    if (parent_directory.value < 0) {
        error = parent_error;
        return false;
    }
    return ReadFileAt(parent_directory.value, absolute_path.filename().string(),
                      max_bytes, bytes, identity, error);
#endif
    error.clear();
    return true;
}

} // namespace rbl::wxui

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/xattr.h>
#endif

namespace rbl::wxui {
namespace {

bool QueryRegularFileIdentityAt(int directory_fd, const std::string& leaf,
                              FileIdentity& identity,
                              std::error_code& error) {
    struct stat info{};
    if (::fstatat(directory_fd, leaf.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    if (!S_ISREG(info.st_mode)) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    identity.first = static_cast<std::uint64_t>(info.st_dev);
    identity.second = static_cast<std::uint64_t>(info.st_ino);
    identity.link_count = static_cast<std::uint64_t>(info.st_nlink);
    identity.valid = true;
    error.clear();
    return true;
}

} // namespace

bool QueryFileIdentityNoFollow(const std::filesystem::path& path,
                               FileIdentity& identity,
                               std::error_code& error) {
    ScopedFileDescriptor parent(OpenParentDirectoryNoFollow(path, error));
    if (parent.value < 0) return false;
    return QueryRegularFileIdentityAt(parent.value, path.filename().string(), identity, error);
}

bool QueryFileIdentityAt(int parent_fd, const std::string& leaf,
                         FileIdentity& identity, std::error_code& error) {
    if (leaf.empty() || leaf == "." || leaf == ".." ||
        leaf.find('/') != std::string::npos) {
        identity = {};
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    return QueryRegularFileIdentityAt(parent_fd, leaf, identity, error);
}

bool ExchangeFileNamesAt(int parent_fd, const std::string& left_leaf,
                         const std::string& right_leaf, std::error_code& error) {
    const auto valid_leaf = [](const std::string& leaf) {
        return !leaf.empty() && leaf != "." && leaf != ".." &&
            leaf.find('/') == std::string::npos;
    };
    if (parent_fd < 0 || !valid_leaf(left_leaf) || !valid_leaf(right_leaf) ||
        left_leaf == right_leaf) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
#if defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, parent_fd, left_leaf.c_str(),
                  parent_fd, right_leaf.c_str(), RENAME_EXCHANGE) == 0) {
        error.clear();
        return true;
    }
    const int code = PosixErrorOrIoError();
    if (code == ENOSYS || code == EINVAL || code == EOPNOTSUPP || code == ENOTSUP) {
        error = std::make_error_code(std::errc::operation_not_supported);
    } else {
        error = std::error_code(code, std::generic_category());
    }
    return false;
#else
    error = std::make_error_code(std::errc::operation_not_supported);
    return false;
#endif
}

bool ReadFileAt(int parent_fd, const std::string& leaf,
                std::uint64_t max_bytes, std::string& bytes,
                FileIdentity& identity, std::error_code& error) {
    bytes.clear();
    identity = {};
    if (parent_fd < 0 || leaf.empty() || leaf == "." || leaf == ".." ||
        leaf.find('/') != std::string::npos) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    int flags = O_RDONLY;
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::openat(parent_fd, leaf.c_str(), flags);
    if (fd < 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    struct stat before{};
    if (::fstat(fd, &before) != 0) {
        const int code = PosixErrorOrIoError(); ::close(fd);
        error = std::error_code(code, std::generic_category()); return false;
    }
    if (!S_ISREG(before.st_mode) || before.st_size < 0) {
        ::close(fd); error = std::make_error_code(std::errc::invalid_argument); return false;
    }
    const auto file_size = static_cast<std::uint64_t>(before.st_size);
    if (file_size > max_bytes || file_size > std::numeric_limits<std::size_t>::max()) {
        ::close(fd); error = std::make_error_code(std::errc::file_too_large); return false;
    }
    bytes.resize(static_cast<std::size_t>(file_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::read(fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            const int code = count < 0 ? PosixErrorOrIoError() : EIO; ::close(fd); bytes.clear();
            error = std::error_code(code, std::generic_category()); return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    struct stat after{};
    const bool stable = ::fstat(fd, &after) == 0 &&
        after.st_dev == before.st_dev && after.st_ino == before.st_ino &&
        after.st_size == before.st_size &&
        after.st_mtim.tv_sec == before.st_mtim.tv_sec &&
        after.st_mtim.tv_nsec == before.st_mtim.tv_nsec &&
        after.st_ctim.tv_sec == before.st_ctim.tv_sec &&
        after.st_ctim.tv_nsec == before.st_ctim.tv_nsec;
    const int close_result = ::close(fd);
    if (!stable || close_result != 0) {
        bytes.clear();
        error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    struct stat named{};
    if (::fstatat(parent_fd, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(named.st_mode) || named.st_dev != before.st_dev || named.st_ino != before.st_ino) {
        bytes.clear();
        error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    identity = {static_cast<std::uint64_t>(before.st_dev),
                static_cast<std::uint64_t>(before.st_ino), true,
                static_cast<std::uint64_t>(before.st_nlink)};
    error.clear();
    return true;
}

static bool CopyPosixMetadataBetweenAt(int source_parent_fd,
                       const std::string& source_leaf,
                       int temporary_parent_fd,
                       const std::string& temporary_leaf,
                       const FileIdentity& expected_source,
                       const FileIdentity& expected_temporary,
                       bool& metadata_warning,
                       std::error_code& error) {
    if (source_parent_fd < 0 || temporary_parent_fd < 0 ||
        source_leaf.empty() || temporary_leaf.empty() ||
        source_leaf == "." || source_leaf == ".." ||
        temporary_leaf == "." || temporary_leaf == ".." ||
        source_leaf.find('/') != std::string::npos ||
        temporary_leaf.find('/') != std::string::npos) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    int source_flags = O_RDONLY;
    int temporary_flags = O_WRONLY;
#ifdef O_NONBLOCK
    // Identity/type validation happens after open. Avoid blocking forever if
    // an attacker races either pathname to a FIFO before that validation.
    source_flags |= O_NONBLOCK;
    temporary_flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
    source_flags |= O_CLOEXEC;
    temporary_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    source_flags |= O_NOFOLLOW;
    temporary_flags |= O_NOFOLLOW;
#endif
    const int source_fd = ::openat(source_parent_fd, source_leaf.c_str(), source_flags);
    if (source_fd < 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    const int temporary_fd = ::openat(temporary_parent_fd, temporary_leaf.c_str(), temporary_flags);
    if (temporary_fd < 0) {
        const int code = PosixErrorOrIoError();
        ::close(source_fd);
        error = std::error_code(code, std::generic_category());
        return false;
    }
    const auto fail = [&](int code) {
        ::close(temporary_fd);
        ::close(source_fd);
        error = std::error_code(code, std::generic_category());
        return false;
    };

    struct stat source_info{};
    struct stat temporary_info{};
    if (::fstat(source_fd, &source_info) != 0) return fail(PosixErrorOrIoError());
    if (::fstat(temporary_fd, &temporary_info) != 0) return fail(PosixErrorOrIoError());
    const FileIdentity opened_source{
        static_cast<std::uint64_t>(source_info.st_dev),
        static_cast<std::uint64_t>(source_info.st_ino), true};
    const FileIdentity opened_temporary{
        static_cast<std::uint64_t>(temporary_info.st_dev),
        static_cast<std::uint64_t>(temporary_info.st_ino), true};
    if (!SameFileIdentity(expected_source, opened_source) ||
        !SameFileIdentity(expected_temporary, opened_temporary)) {
        return fail(ESTALE);
    }

    // Atomic replacement creates a new inode. Preserve the old file's
    // ownership and mode rather than silently changing them to process
    // defaults. If ownership cannot be retained, refuse the save.
    if (source_info.st_uid != temporary_info.st_uid ||
        source_info.st_gid != temporary_info.st_gid) {
        if (::fchown(temporary_fd, source_info.st_uid, source_info.st_gid) != 0) {
            return fail(PosixErrorOrIoError());
        }
    }
    if (::fchmod(temporary_fd, source_info.st_mode & 07777) != 0) {
        return fail(PosixErrorOrIoError());
    }

#ifdef __linux__
    constexpr std::size_t kMaxCopiedXattrBytes = 1024 * 1024;
    const auto names_size = ::flistxattr(source_fd, nullptr, 0);
    if (names_size < 0 && errno != ENOTSUP && errno != EOPNOTSUPP) {
        return fail(PosixErrorOrIoError());
    }
    if (names_size > static_cast<ssize_t>(kMaxCopiedXattrBytes)) {
        return fail(E2BIG);
    }
    bool source_has_access_acl = false;
    if (names_size > 0) {
        std::vector<char> names(static_cast<std::size_t>(names_size));
        const auto names_read = ::flistxattr(source_fd, names.data(), names.size());
        if (names_read < 0) return fail(PosixErrorOrIoError());
        if (names_read != names_size) return fail(ESTALE);
        std::size_t copied_bytes = names.size();
        std::size_t offset = 0;
        while (offset < names.size()) {
            const char* name = names.data() + offset;
            const auto* terminator = static_cast<const char*>(
                std::memchr(name, '\0', names.size() - offset));
            if (!terminator) return fail(EINVAL);
            const std::size_t name_length = static_cast<std::size_t>(terminator - name);
            const std::string attribute(name, name_length);
            offset += name_length + 1;
            if (attribute == "system.posix_acl_access") source_has_access_acl = true;

            // Security labels and trusted attributes are kernel/policy
            // managed. Keep the destination's default value and report it.
            if (attribute.starts_with("security.") || attribute.starts_with("trusted.")) {
                metadata_warning = true;
                continue;
            }
            const auto value_size = ::fgetxattr(source_fd, attribute.c_str(), nullptr, 0);
            if (value_size < 0) return fail(PosixErrorOrIoError());
            if (value_size > static_cast<ssize_t>(kMaxCopiedXattrBytes) ||
                copied_bytes + static_cast<std::size_t>(value_size) > kMaxCopiedXattrBytes) {
                return fail(E2BIG);
            }
            std::vector<char> value(static_cast<std::size_t>(value_size));
            const auto value_read = ::fgetxattr(source_fd, attribute.c_str(),
                value.empty() ? nullptr : value.data(), value.size());
            if (value_read < 0) return fail(PosixErrorOrIoError());
            if (value_read != value_size) return fail(ESTALE);
            copied_bytes += value.size();
            if (::fsetxattr(temporary_fd, attribute.c_str(),
                    value.empty() ? nullptr : value.data(), value.size(), 0) != 0) {
                if (attribute == "system.posix_acl_access" ||
                    attribute.starts_with("user.")) {
                    return fail(PosixErrorOrIoError());
                }
                metadata_warning = true;
            }
        }
    }
    // The temporary file may inherit a default ACL from its parent directory.
    // If the replaced file had no access ACL, retaining that inherited ACL
    // would silently broaden or otherwise change the file's effective access.
    if (!source_has_access_acl &&
        ::fremovexattr(temporary_fd, "system.posix_acl_access") != 0 &&
        errno != ENODATA && errno != ENOTSUP && errno != EOPNOTSUPP) {
        return fail(PosixErrorOrIoError());
    }
#else
    metadata_warning = true;
#endif

    if (::fsync(temporary_fd) != 0) return fail(PosixErrorOrIoError());
    struct stat source_after{};
    struct stat temporary_after{};
    if (::fstat(source_fd, &source_after) != 0) return fail(PosixErrorOrIoError());
    if (::fstat(temporary_fd, &temporary_after) != 0) return fail(PosixErrorOrIoError());
    if (source_after.st_dev != source_info.st_dev || source_after.st_ino != source_info.st_ino ||
        source_after.st_mode != source_info.st_mode ||
        source_after.st_uid != source_info.st_uid ||
        source_after.st_gid != source_info.st_gid ||
        source_after.st_size != source_info.st_size ||
        source_after.st_mtim.tv_sec != source_info.st_mtim.tv_sec ||
        source_after.st_mtim.tv_nsec != source_info.st_mtim.tv_nsec ||
        source_after.st_ctim.tv_sec != source_info.st_ctim.tv_sec ||
        source_after.st_ctim.tv_nsec != source_info.st_ctim.tv_nsec ||
        temporary_after.st_dev != temporary_info.st_dev ||
        temporary_after.st_ino != temporary_info.st_ino ||
        temporary_after.st_mode != source_info.st_mode ||
        temporary_after.st_uid != source_info.st_uid ||
        temporary_after.st_gid != source_info.st_gid) {
        return fail(ESTALE);
    }
    if (::close(temporary_fd) != 0) {
        const int code = PosixErrorOrIoError();
        ::close(source_fd);
        error = std::error_code(code, std::generic_category());
        return false;
    }
    if (::close(source_fd) != 0) {
        error = std::error_code(PosixErrorOrIoError(), std::generic_category());
        return false;
    }
    FileIdentity source_after_close;
    FileIdentity temporary_after_close;
    if (!QueryRegularFileIdentityAt(source_parent_fd, source_leaf, source_after_close, error) ||
        !QueryRegularFileIdentityAt(temporary_parent_fd, temporary_leaf, temporary_after_close, error) ||
        !SameFileIdentity(expected_source, source_after_close) ||
        !SameFileIdentity(expected_temporary, temporary_after_close)) {
        if (!error) error = std::make_error_code(std::errc::state_not_recoverable);
        return false;
    }
    error.clear();
    return true;
}

bool CopyPosixMetadataAt(int parent_fd,
                         const std::string& source_leaf,
                         const std::string& temporary_leaf,
                         const FileIdentity& expected_source,
                         const FileIdentity& expected_temporary,
                         bool& metadata_warning,
                         std::error_code& error) {
    return CopyPosixMetadataBetweenAt(parent_fd, source_leaf, parent_fd, temporary_leaf,
        expected_source, expected_temporary, metadata_warning, error);
}

bool CopyPosixMetadata(const std::filesystem::path& source,
                       const std::filesystem::path& temporary,
                       const FileIdentity& expected_source,
                       const FileIdentity& expected_temporary,
                       bool& metadata_warning,
                       std::error_code& error) {
    std::error_code parent_error;
    ScopedFileDescriptor source_parent(OpenParentDirectoryNoFollow(source, parent_error));
    if (source_parent.value < 0) { error = parent_error; return false; }
    ScopedFileDescriptor temporary_parent(OpenParentDirectoryNoFollow(temporary, parent_error));
    if (temporary_parent.value < 0) { error = parent_error; return false; }
    return CopyPosixMetadataBetweenAt(source_parent.value, source.filename().string(),
        temporary_parent.value, temporary.filename().string(), expected_source,
        expected_temporary, metadata_warning, error);
}

} // namespace rbl::wxui

#endif // !_WIN32
