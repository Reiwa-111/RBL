#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rbl::windows_path {

struct DirectoryPathParts {
    std::wstring root;
    std::vector<std::wstring> components;
};

inline bool starts_with_ascii_case_insensitive(std::wstring_view value,
                                               std::wstring_view prefix) {
    if (value.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        wchar_t ch = value[i];
        if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
        wchar_t expected = prefix[i];
        if (expected >= L'a' && expected <= L'z') {
            expected = static_cast<wchar_t>(expected - L'a' + L'A');
        }
        if (ch != expected) return false;
    }
    return true;
}

inline bool is_drive_letter(wchar_t value) {
    return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
}

inline bool parse_unc_root(const std::wstring& path, std::size_t server_start,
                           std::wstring& root, std::size_t& remainder_start) {
    const std::size_t server_end = path.find(L'\\', server_start);
    if (server_end == std::wstring::npos || server_end == server_start) return false;
    const std::size_t share_start = server_end + 1;
    if (share_start >= path.size()) return false;
    const std::size_t share_end = path.find(L'\\', share_start);
    const std::size_t share_limit = share_end == std::wstring::npos ? path.size() : share_end;
    if (share_limit == share_start) return false;

    const std::wstring_view server(path.data() + server_start, server_end - server_start);
    const std::wstring_view share(path.data() + share_start, share_limit - share_start);
    if (server == L"." || server == L".." || share == L"." || share == L".." ||
        server.find(L':') != std::wstring_view::npos || share.find(L':') != std::wstring_view::npos) {
        return false;
    }

    root.assign(path.data(), share_limit);
    root.push_back(L'\\');
    remainder_start = share_end == std::wstring::npos ? path.size() : share_end + 1;
    return true;
}

// Parse Windows absolute directory paths without relying on std::filesystem's
// root_name()/root_path() implementation. MinGW's libstdc++ does not reliably
// recognize UNC roots, so \server\share can otherwise be mistaken for the
// current drive's root.
inline bool split_absolute_directory_path(std::wstring path, DirectoryPathParts& result) {
    result = {};
    if (path.empty() || path.find(L'\0') != std::wstring::npos) return false;
    std::replace(path.begin(), path.end(), L'/', L'\\');

    std::size_t remainder_start = 0;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        if (path.size() >= 4 && path[2] == L'?' && path[3] == L'\\') {
            if (starts_with_ascii_case_insensitive(std::wstring_view(path).substr(4), L"UNC\\")) {
                if (!parse_unc_root(path, 8, result.root, remainder_start)) return false;
            } else if (path.size() >= 7 && is_drive_letter(path[4]) && path[5] == L':' && path[6] == L'\\') {
                result.root.assign(path.data(), 7);
                remainder_start = 7;
            } else {
                constexpr std::wstring_view volume_prefix = L"Volume{";
                const std::size_t volume_end = path.find(L'\\', 4);
                const std::size_t volume_limit = volume_end == std::wstring::npos ? path.size() : volume_end;
                const std::wstring_view volume(path.data() + 4, volume_limit - 4);
                if (!starts_with_ascii_case_insensitive(volume, volume_prefix) ||
                    volume.size() < volume_prefix.size() + 2 || volume.back() != L'}') {
                    return false;
                }
                result.root.assign(path.data(), volume_limit);
                result.root.push_back(L'\\');
                remainder_start = volume_end == std::wstring::npos ? path.size() : volume_end + 1;
            }
        } else {
            // Device namespace paths (\\.\...) are not filesystem directory roots.
            if (path.size() >= 4 && path[2] == L'.' && path[3] == L'\\') return false;
            if (!parse_unc_root(path, 2, result.root, remainder_start)) return false;
        }
    } else if (path.size() >= 3 && is_drive_letter(path[0]) &&
               path[1] == L':' && path[2] == L'\\') {
        result.root.assign(path.data(), 3);
        remainder_start = 3;
    } else {
        return false;
    }

    std::size_t position = remainder_start;
    while (position < path.size()) {
        while (position < path.size() && path[position] == L'\\') ++position;
        if (position == path.size()) break;
        const std::size_t end = path.find(L'\\', position);
        const std::size_t limit = end == std::wstring::npos ? path.size() : end;
        std::wstring component = path.substr(position, limit - position);
        if (component.empty() || component == L"." || component == L".." ||
            component.find(L':') != std::wstring::npos) {
            result = {};
            return false;
        }
        result.components.push_back(std::move(component));
        position = end == std::wstring::npos ? path.size() : end + 1;
    }
    return !result.root.empty();
}

} // namespace rbl::windows_path
