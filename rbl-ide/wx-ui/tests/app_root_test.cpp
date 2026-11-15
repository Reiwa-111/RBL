#include "app_root.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void make_root(const fs::path& root) {
    fs::create_directories(root / "tools");
    fs::create_directories(root / "tests");
    fs::create_directories(root / "benchmarks");
    fs::create_directories(root / "compiler");
    fs::create_directories(root / "runtime");
    fs::create_directories(root / "toolchain");
    std::ofstream(root / "VERSION") << "0.8.0\n";
    std::ofstream(root / "rbl.py") << "# test\n";
    std::ofstream(root / "tools" / "rbltool.py") << "# test\n";
    std::ofstream(root / "tests" / "run_tests.py") << "# test\n";
    std::ofstream(root / "benchmarks" / "run_benchmarks.py") << "# test\n";
}

int main() {
    const auto base = fs::temp_directory_path() / "rbl-studio-app-root-test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const auto install = base / "install";
    const auto other = base / "other";
    const auto executable = install / "build" / "wx-ui" / "rbl-studio";
    fs::create_directories(executable.parent_path());
    std::ofstream(executable) << "test executable\n";
    make_root(install);
    make_root(other);

    const auto override_result = rbl::wxui::ResolveApplicationRoot(other, executable);
    require(override_result && override_result.root == fs::canonical(other), "valid override must win");

    const auto invalid_override = rbl::wxui::ResolveApplicationRoot(base / "missing", executable);
    require(!invalid_override, "invalid override must not fall back to executable ancestors");
    require(invalid_override.error.find("RBL_STUDIO_ROOT") != std::string::npos, "invalid override error must name the variable");

    const auto incomplete = base / "incomplete";
    fs::create_directories(incomplete / "tools");
    std::ofstream(incomplete / "VERSION") << "0.8.0\n";
    std::ofstream(incomplete / "tools" / "rbltool.py") << "# incomplete\n";
    const auto incomplete_override = rbl::wxui::ResolveApplicationRoot(incomplete, executable);
    require(!incomplete_override, "tool-only directory without compiler/runtime payload must be rejected");

    const auto discovered = rbl::wxui::ResolveApplicationRoot(std::nullopt, executable);
    require(discovered && discovered.root == fs::canonical(install), "must discover root in executable ancestors");

#ifndef _WIN32
    const auto symlink_executable = base / "launcher" / "bin" / "rbl-studio";
    fs::create_directories(symlink_executable.parent_path());
    std::error_code symlink_error;
    fs::create_symlink(executable, symlink_executable, symlink_error);
    if (!symlink_error) {
        const auto discovered_through_symlink =
            rbl::wxui::ResolveApplicationRoot(std::nullopt, symlink_executable);
        require(discovered_through_symlink && discovered_through_symlink.root == fs::canonical(install),
            "must discover application root through executable symlink target ancestors");
    } else if (symlink_error != std::errc::operation_not_permitted &&
               symlink_error != std::errc::permission_denied &&
               symlink_error != std::errc::function_not_supported) {
        throw std::system_error(symlink_error, "create executable symlink regression fixture");
    }
#endif

    const auto old_cwd = fs::current_path();
    fs::current_path(base);
    const auto independent_of_cwd = rbl::wxui::ResolveApplicationRoot(std::nullopt, executable);
    fs::current_path(old_cwd);
    require(independent_of_cwd && independent_of_cwd.root == fs::canonical(install), "absolute executable discovery must ignore cwd");

    const auto missing = rbl::wxui::ResolveApplicationRoot(std::nullopt, base / "unrelated" / "ide");
    require(!missing, "missing root must be reported");
    fs::remove_all(base, ignored);
}
