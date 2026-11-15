#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace rbl::wxui {

struct ApplicationRootResult {
    std::filesystem::path root;
    std::string error;

    explicit operator bool() const noexcept { return !root.empty(); }
};

inline bool IsApplicationRoot(const std::filesystem::path& candidate) {
    std::error_code error;
    if (!std::filesystem::is_directory(candidate, error) || error) return false;
    for (const auto* marker : {
             "VERSION", "rbl.py", "tools/rbltool.py", "tests/run_tests.py",
             "benchmarks/run_benchmarks.py"}) {
        if (!std::filesystem::is_regular_file(candidate / marker, error) || error) return false;
    }
    for (const auto* marker : {"compiler", "runtime", "toolchain"}) {
        if (!std::filesystem::is_directory(candidate / marker, error) || error) return false;
    }
    return true;
}

inline ApplicationRootResult ResolveApplicationRoot(
    const std::optional<std::filesystem::path>& explicit_root,
    const std::filesystem::path& executable_path) {
    std::error_code error;
    if (explicit_root) {
        const auto candidate = std::filesystem::weakly_canonical(*explicit_root, error);
        if (!error && IsApplicationRoot(candidate)) return {candidate, {}};
        return {{}, "RBL_STUDIO_ROOT is not a complete RBL Studio root (missing VERSION, rbl.py, tools/tests/benchmarks scripts, or compiler/runtime/toolchain payload)"};
    }

    // Resolve the executable itself before walking parents: if the GUI binary
    // is reached through a symlink, its lexical ancestors may not contain the
    // installation payload even though the target's ancestors do.
    auto executable = std::filesystem::weakly_canonical(executable_path, error);
    if (error) {
        return {{}, "cannot resolve the RBL Studio executable path: " + error.message()};
    }
    auto candidate = executable.parent_path();
    while (!candidate.empty()) {
        if (IsApplicationRoot(candidate)) {
            auto canonical = std::filesystem::weakly_canonical(candidate, error);
            if (!error) return {std::move(canonical), {}};
            return {{}, "cannot canonicalize the RBL Studio application root: " + error.message()};
        }
        const auto parent = candidate.parent_path();
        if (parent == candidate) break;
        candidate = parent;
    }
    return {{}, "could not locate RBL Studio root in executable ancestors; set RBL_STUDIO_ROOT"};
}

} // namespace rbl::wxui
