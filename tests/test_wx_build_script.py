#!/usr/bin/env python3
"""Guard wx worker build handling and keep diagnostic stub outputs in build/."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    script = (ROOT / "tools" / "build_wx_ide.ps1").read_text(encoding="utf-8")
    linux_script = (ROOT / "tools" / "build_wx_ide.sh").read_text(encoding="utf-8")
    diagnostics_scale = (ROOT / "tests" / "wx_gui_diagnostics_scale.py").read_text(encoding="utf-8")
    marker = "# Capture native streams outside PowerShell's error pipeline."
    start = script.index(marker)
    end_marker = "if ($cargoProcess.ExitCode -ne 0)"
    end = script.index(end_marker, start) + len(end_marker)
    cargo_block = script[start:end]
    required = (
        "$cargoArguments = 'build --locked --release --bin rbl-studio-worker --target x86_64-pc-windows-gnu --target-dir \"{0}\"' -f $workerTargetPath",
        "-ArgumentList $cargoArguments",
        "-RedirectStandardOutput $cargoStdout -RedirectStandardError $cargoStderr",
        "if ($cargoProcess.ExitCode -ne 0)",
    )
    for fragment in required:
        if fragment not in cargo_block:
            raise AssertionError(f"Windows worker build block is missing: {fragment}")
    linux_verify_lines = [line for line in linux_script.splitlines() if "verify_rust_source_bundle.py" in line]
    if len(linux_verify_lines) != 2 or any("--target " in line for line in linux_verify_lines):
        raise AssertionError("Linux build must verify the full two-target Rust source/license closure")
    if diagnostics_scale.count("cwd=WORK, capture_output=True, text=True") != 2:
        raise AssertionError("diagnostics-scale worker stub compilers must run in the isolated build work directory")
    print("wx worker build handling, source closure and diagnostics-stub isolation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
