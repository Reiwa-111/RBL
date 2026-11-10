#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace rbl::wxui {

struct FileIdentity {
    std::uint64_t first{};
    std::uint64_t second{};
    bool valid{};
    std::uint64_t link_count{};
};

inline bool SameFileIdentity(const FileIdentity& left, const FileIdentity& right) {
    return left.valid && right.valid &&
        left.first == right.first && left.second == right.second;
}

bool ReadFileNoFollow(const std::filesystem::path& path,
                      std::uint64_t max_bytes,
                      std::string& bytes,
                      FileIdentity& identity,
                      std::error_code& error);

#ifndef _WIN32
// Open a directory by walking every component with openat(O_NOFOLLOW).
// The returned descriptor is owned by the caller and must be closed.
int OpenDirectoryNoFollow(const std::filesystem::path& directory,
                          std::error_code& error);

// Descriptor-relative operations. `leaf` must be a single filename, not a path.
bool ReadFileAt(int parent_fd, const std::string& leaf,
                std::uint64_t max_bytes, std::string& bytes,
                FileIdentity& identity, std::error_code& error);
bool QueryFileIdentityAt(int parent_fd, const std::string& leaf,
                         FileIdentity& identity, std::error_code& error);
// Atomically exchange two leaf names in one pinned directory. Linux uses
// renameat2(RENAME_EXCHANGE); unsupported filesystems report
// operation_not_supported so callers can choose a documented fallback.
bool ExchangeFileNamesAt(int parent_fd, const std::string& left_leaf,
                         const std::string& right_leaf, std::error_code& error);

// Resolve every parent component with openat(O_NOFOLLOW|O_DIRECTORY), then
// inspect the leaf relative to the pinned parent directory descriptor.
bool QueryFileIdentityNoFollow(const std::filesystem::path& path,
                               FileIdentity& identity,
                               std::error_code& error);

// Copy metadata onto an already-written same-filesystem temporary file.
// Content is not changed. Failure leaves the source untouched.
bool CopyPosixMetadata(const std::filesystem::path& source,
                       const std::filesystem::path& temporary,
                       const FileIdentity& expected_source,
                       const FileIdentity& expected_temporary,
                       bool& metadata_warning,
                       std::error_code& error);
bool CopyPosixMetadataAt(int parent_fd,
                         const std::string& source_leaf,
                         const std::string& temporary_leaf,
                         const FileIdentity& expected_source,
                         const FileIdentity& expected_temporary,
                         bool& metadata_warning,
                         std::error_code& error);
#endif

} // namespace rbl::wxui
