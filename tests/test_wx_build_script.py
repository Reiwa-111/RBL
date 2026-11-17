#!/usr/bin/env python3
"""Guard wx worker build handling and keep diagnostic stub outputs in build/."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    script = (ROOT / "tools" / "build_wx_ide.ps1").read_text(encoding="utf-8")
    linux_script = (ROOT / "tools" / "build_wx_ide.sh").read_text(encoding="utf-8")
    windows_acceptance = (ROOT / "tests" / "accept_windows_registered_installer.ps1").read_text(encoding="utf-8")
    linux_acceptance = (ROOT / "tests" / "installer_linux_acceptance.sh").read_text(encoding="utf-8")
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
    for obsolete in (
        "verify_rust_source_bundle.py",
        "Rust-worker-source.tar.zst",
        "Windows-toolchain-source.tar.zst",
        "source/wxWidgets-3.2.4.tar.bz2",
    ):
        if obsolete in linux_script or obsolete in script:
            raise AssertionError(f"Build scripts must not require a separate source archive: {obsolete}")
    obsolete_payloads = (
        "Rust-worker-source.tar.zst",
        "Windows-toolchain-source.tar.zst",
        "Python-3.14.10.tar.xz",
        "python-build-standalone-20261110.tar.gz",
    )
    for payload in obsolete_payloads:
        if payload in windows_acceptance or payload in linux_acceptance:
            raise AssertionError(f"Installer acceptance must not require corresponding-source payload: {payload}")
    if 'cp -a "$ROOT/rbl-ide/wx-ui/licenses/Rust/." "$BUILD_DIR/licenses/Rust/"' not in linux_script:
        raise AssertionError("Linux build must retain Rust third-party license texts and notices")
    if '"$BUILD_DIR/licenses/Rust/THIRD_PARTY_NOTICES.md"' not in linux_script:
        raise AssertionError("Linux build must retain the Rust dependency notice inventory")
    if diagnostics_scale.count("cwd=WORK, capture_output=True, text=True") != 2:
        raise AssertionError("diagnostics-scale worker stub compilers must run in the isolated build work directory")
    print("wx worker build handling, source-only release policy and diagnostics-stub isolation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
