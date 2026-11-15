#include "file_metadata.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "POSIX file metadata test failed: " << message << '\n';
    std::abort();
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(output.good(), "open temporary test file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    Require(output.good(), "write temporary test file");
}

std::string ReadPlainFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "read temporary test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    auto pattern = (std::filesystem::temp_directory_path() / "rbl-posix-metadata-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char* created = ::mkdtemp(writable.data());
    Require(created != nullptr, "create temporary directory");
    const std::filesystem::path root(created);
    const auto source = root / "source.txt";
    const auto destination = root / "destination.txt";
    const auto file_link = root / "source-link.txt";
    const auto directory_link = root / "directory-link";
    WriteFile(source, "source payload");
    WriteFile(destination, "destination payload");
    Require(::symlink(source.filename().c_str(), file_link.c_str()) == 0, "create file symlink");
    Require(::symlink(root.c_str(), directory_link.c_str()) == 0, "create directory symlink");

    std::error_code error;
    std::string bytes;
    rbl::wxui::FileIdentity source_identity;
    Require(rbl::wxui::ReadFileNoFollow(source, 1024, bytes, source_identity, error), "read regular file without following links");
    Require(!error && bytes == "source payload" && source_identity.valid, "regular-file identity and content");

    rbl::wxui::FileIdentity rejected_identity;
    Require(!rbl::wxui::ReadFileNoFollow(file_link, 1024, bytes, rejected_identity, error), "reject file symlink");
    Require(static_cast<bool>(error) && !rejected_identity.valid, "symlink rejection reports an error");

    const int directory_fd = rbl::wxui::OpenDirectoryNoFollow(root, error);
    Require(directory_fd >= 0 && !error, "open directory without following links");
    Require(!rbl::wxui::ReadFileAt(directory_fd, "source-link.txt", 1024,
        bytes, rejected_identity, error), "reject descriptor-relative symlink");
    Require(static_cast<bool>(error) && !rejected_identity.valid, "descriptor-relative rejection reports an error");

    rbl::wxui::FileIdentity descriptor_identity;
    Require(rbl::wxui::ReadFileAt(directory_fd, "source.txt", 1024,
        bytes, descriptor_identity, error), "read descriptor-relative regular file");
    Require(!error && bytes == "source payload" &&
        rbl::wxui::SameFileIdentity(source_identity, descriptor_identity), "descriptor-relative identity matches");

    rbl::wxui::FileIdentity path_identity;
    Require(rbl::wxui::QueryFileIdentityNoFollow(source, path_identity, error), "query no-follow path identity");
    Require(rbl::wxui::SameFileIdentity(source_identity, path_identity), "path identity matches opened file");
    Require(!rbl::wxui::QueryFileIdentityAt(directory_fd, "source-link.txt",
        rejected_identity, error), "reject symlink identity query");

    Require(rbl::wxui::ExchangeFileNamesAt(directory_fd, "source.txt", "destination.txt", error), "atomically exchange file names");
    Require(!error, "name exchange has no error");
    Require(ReadPlainFile(source) == "destination payload", "left name now refers to right file");
    Require(ReadPlainFile(destination) == "source payload", "right name now refers to left file");

    ::close(directory_fd);
    const int rejected_directory_fd = rbl::wxui::OpenDirectoryNoFollow(directory_link, error);
    Require(rejected_directory_fd < 0 && static_cast<bool>(error), "reject directory symlink");
    Require(std::filesystem::is_symlink(directory_link), "directory symlink remains untouched");
    std::filesystem::remove_all(root);
    return 0;
}
