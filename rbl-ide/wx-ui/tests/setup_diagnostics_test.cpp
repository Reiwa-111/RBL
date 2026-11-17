#include "setup_diagnostics.hpp"

#include <cassert>
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
    std::ofstream(root / "tools/rbltool.py") << "# test\n";
    std::ofstream(root / "tests/run_tests.py") << "# test\n";
    std::ofstream(root / "benchmarks/run_benchmarks.py") << "# test\n";
#ifdef _WIN32
    const auto toolchain = root / "toolchain/windows-x86_64/msys64/ucrt64";
    fs::create_directories(toolchain / "bin");
    fs::create_directories(toolchain / "x86_64-w64-mingw32/bin");
    std::ofstream(toolchain / "bin/gcc.exe") << "test";
    std::ofstream(toolchain / "bin/as.exe") << "test";
    std::ofstream(toolchain / "x86_64-w64-mingw32/bin/ld.exe") << "test";
#else
    const auto toolchain = root / "toolchain/linux-x86_64/bin";
    fs::create_directories(toolchain);
    std::ofstream(toolchain / "clang") << "test";
    std::ofstream(toolchain / "ld.lld") << "test";
#endif
}

int main() {
    const auto base = fs::temp_directory_path() / "rbl-studio-setup-diagnostics-test";
    std::error_code ignored;
    fs::remove_all(base, ignored);
    const auto root = base / "install";
    const auto worker = base / "rbl-studio-worker";
    const auto python = base / "python";
    fs::create_directories(base);
    make_root(root);
    std::ofstream(worker) << "test";
    std::ofstream(python) << "test";

    const auto complete = rbl::wxui::InspectSetup(root, worker, python);
    require(complete.size() == 4, "setup must report four diagnostics");
    for (const auto& check : complete) require(check.passed, "complete setup must pass every diagnostic");

    const auto incomplete = rbl::wxui::InspectSetup(root, base / "missing-worker", python);
    require(!incomplete[1].passed, "missing worker must be reported");
    require(incomplete[1].detail.find("RBL_STUDIO_WORKER") != std::string::npos,
        "worker diagnostic must provide an actionable environment variable");

    const auto missing_python = rbl::wxui::InspectSetup(root, worker, base / "missing-python");
    require(!missing_python[2].passed, "missing Python must be reported");
    require(missing_python[2].detail.find("RBL_PYTHON") != std::string::npos,
        "Python diagnostic must provide an actionable environment variable");

#ifdef _WIN32
    const auto missing_tool = root / "toolchain/windows-x86_64/msys64/ucrt64/bin/as.exe";
#else
    const auto missing_tool = root / "toolchain/linux-x86_64/bin/clang";
#endif
    fs::remove(missing_tool);
    const auto missing_toolchain = rbl::wxui::InspectSetup(root, worker, python);
    require(!missing_toolchain[3].passed, "missing compiler tool must be reported");

    fs::remove_all(base, ignored);
}
