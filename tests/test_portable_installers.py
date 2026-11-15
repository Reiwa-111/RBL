#!/usr/bin/env python3
"""Ensure portable staging never writes install state or user-specific files."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    windows = (ROOT / "installer" / "install_windows.ps1").read_text(encoding="utf-8")
    linux = (ROOT / "installer" / "install_linux.sh").read_text(encoding="utf-8")

    if "[switch]$Portable" not in windows:
        raise AssertionError("Windows installer has no -Portable switch")
    if "if ($Portable) { $NoRegister = $true }" not in windows:
        raise AssertionError("Windows -Portable does not force no-registration")
    if "if (-not $Portable) {" not in windows:
        raise AssertionError("Windows installer does not guard portable-only omissions")
    if windows.count("if (-not $Portable) {") < 2:
        raise AssertionError("Windows installer does not guard both the uninstaller and the manifest")

    copy_guard = windows.split("if (-not $Portable) {", 1)[1].split("\n    }", 1)[0]
    if "Uninstall-RBLStudio.ps1" not in copy_guard:
        raise AssertionError("Windows uninstaller copy is not inside the portable guard")
    manifest_guard = windows.split("if (-not $Portable) {", 2)[2]
    if "$Manifest | ConvertTo-Json" not in manifest_guard:
        raise AssertionError("Windows install manifest is written outside the portable guard")

    if "--portable" not in linux or "PORTABLE=1; NO_REGISTER=1" not in linux:
        raise AssertionError("Linux installer has no --portable mode")
    copy_guard = linux.split("if ((PORTABLE == 0)); then", 1)[1].split("\nfi", 1)[0]
    if "Uninstall-RBLStudio.sh" not in copy_guard:
        raise AssertionError("Linux uninstaller copy is not inside the portable guard")
    if "if ((PORTABLE == 0)); then\n  FILES_LIST=" not in linux:
        raise AssertionError("Linux install state is not guarded by the portable mode")

    for script in (windows, linux):
        if "Portable staging: no install manifest" not in script:
            raise AssertionError("portable staging message is missing")

    print("PASS: portable staging skips install manifest, uninstaller and registration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
