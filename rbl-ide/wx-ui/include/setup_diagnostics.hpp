#pragma once

#include "app_root.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace rbl::wxui {

struct SetupDiagnostic {
    std::string name;
    bool passed{};
    std::string detail;
};

inline bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
}

inline std::vector<SetupDiagnostic> InspectSetup(
    const std::filesystem::path& root,
    const std::filesystem::path& worker,
    const std::filesystem::path& python) {
    std::vector<SetupDiagnostic> checks;
    const bool valid_root = IsApplicationRoot(root);
    checks.push_back({"Application root", valid_root,
        valid_root ? root.string() : "Application directory is missing required RBL Studio files"});

    const bool valid_worker = IsRegularFile(worker);
    checks.push_back({"Backend worker", valid_worker,
        valid_worker ? worker.string() : "Worker executable not found; set RBL_STUDIO_WORKER or select its path"});

    const bool valid_python = IsRegularFile(python);
    checks.push_back({"Python runtime", valid_python,
        valid_python ? python.string() : "Bundled Python not found; set RBL_PYTHON or provide the runtime"});

#ifdef _WIN32
    const auto toolchain = root / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64";
    const std::array required{
        toolchain / "bin" / "gcc.exe",
        toolchain / "bin" / "as.exe",
        toolchain / "x86_64-w64-mingw32" / "bin" / "ld.exe"};
#else
    const auto toolchain = root / "toolchain" / "linux-x86_64";
    const std::array required{toolchain / "bin" / "clang", toolchain / "bin" / "ld.lld"};
#endif
    bool toolchain_ok = true;
    std::string missing;
    for (const auto& file : required) {
        if (!IsRegularFile(file)) {
            toolchain_ok = false;
            if (!missing.empty()) missing += ", ";
            missing += file.string();
        }
    }
    checks.push_back({"Compiler toolchain", toolchain_ok,
        toolchain_ok ? toolchain.string() : "Missing toolchain files: " + missing});
    return checks;
}

} // namespace rbl::wxui
