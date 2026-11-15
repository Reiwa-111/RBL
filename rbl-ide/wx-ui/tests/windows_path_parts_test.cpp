#include "windows_path_parts.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect_parts(const std::wstring& input, const std::wstring& expected_root,
                  const std::vector<std::wstring>& expected_components) {
    rbl::windows_path::DirectoryPathParts parts;
    if (!rbl::windows_path::split_absolute_directory_path(input, parts)) {
        std::wcerr << L"FAIL: rejected valid path: " << input << L'\n';
        return false;
    }
    if (parts.root != expected_root || parts.components != expected_components) {
        std::wcerr << L"FAIL: unexpected path split: " << input << L" root=" << parts.root << L'\n';
        return false;
    }
    return true;
}

bool expect_rejected(const std::wstring& input) {
    rbl::windows_path::DirectoryPathParts parts;
    if (rbl::windows_path::split_absolute_directory_path(input, parts)) {
        std::wcerr << L"FAIL: accepted unsafe/relative path: " << input << L'\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expect_parts(L"C:\\", L"C:\\", {});
    ok &= expect_parts(L"c:/alpha/beta", L"c:\\", {L"alpha", L"beta"});
    ok &= expect_parts(L"\\\\server\\share", L"\\\\server\\share\\", {});
    ok &= expect_parts(L"\\\\server\\share\\alpha\\beta", L"\\\\server\\share\\",
                       {L"alpha", L"beta"});
    ok &= expect_parts(L"\\\\?\\C:\\alpha\\beta", L"\\\\?\\C:\\",
                       {L"alpha", L"beta"});
    ok &= expect_parts(L"\\\\?\\UNC\\server\\share\\alpha", L"\\\\?\\UNC\\server\\share\\",
                       {L"alpha"});
    ok &= expect_parts(L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\alpha",
                       L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\", {L"alpha"});

    ok &= expect_rejected(L"relative\\path");
    ok &= expect_rejected(L"C:drive-relative");
    ok &= expect_rejected(L"\\root-relative");
    ok &= expect_rejected(L"\\\\server");
    ok &= expect_rejected(L"\\\\server\\");
    ok &= expect_rejected(L"\\\\.\\PhysicalDrive0");
    ok &= expect_rejected(L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1");
    ok &= expect_rejected(L"C:\\alpha\\..\\beta");
    ok &= expect_rejected(L"C:\\alpha\\.\\beta");
    ok &= expect_rejected(L"C:\\alpha:stream");

    if (!ok) return 1;
    std::cout << "Windows path root parsing: PASS (drive, UNC, extended drive/UNC, volume GUID, rejection cases)\n";
    return 0;
}
