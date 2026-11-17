#!/usr/bin/env python3
"""Guard the installed-copy repair payload and wizard preflight contract."""
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    linux = (ROOT / "installer/install_linux.sh").read_text(encoding="utf-8")
    windows = (ROOT / "installer/install_windows.ps1").read_text(encoding="utf-8")
    wizard = (ROOT / "rbl-ide/wx-ui/src/main_frame.cpp").read_text(encoding="utf-8")

    linux_stage = linux.split('cp -- "$BUILD_DIR/rbl-studio-wx"', 1)[1].split("PREVIOUS_DEFAULT=", 1)[0]
    require('"$ROOT/installer/install_linux.sh"' in linux_stage,
            "Linux install script is not staged for installed-copy repair")
    require('"$ROOT/installer/uninstall_linux.sh"' in linux_stage,
            "Linux uninstaller is not staged under installer/")
    require('cp -- "$PYTHON_RUNTIME_ARCHIVE" "$STAGE/runtime/python/python-runtime.tar.gz"' in linux_stage,
            "Linux pinned CPython runtime archive is not preserved in installed payload")
    require("runtime/python/python-runtime.tar.gz" in linux,
            "Linux installer marker checks omit the CPython archive")
    require("installer/install_linux.sh" in linux and "installer/uninstall_linux.sh" in linux,
            "Linux installer marker checks omit self-repair scripts")
    linux_uninstaller = (ROOT / "installer/uninstall_linux.sh").read_text(encoding="utf-8")
    require('"$SCRIPT_DIR/../.rblstudio-install-files"' in linux_uninstaller,
            "Linux installed installer-directory uninstaller does not resolve its parent install root")

    windows_stage = windows.split("if (-not $Portable) {", 1)[1].split("\n    }", 1)[0]
    for relative in ("install_windows.ps1", "uninstall_windows.ps1", "windows_registry_policy.ps1"):
        require(relative in windows_stage,
                f"Windows installed-copy repair payload omits {relative}")
        require(f"installer/{relative}" in windows,
                f"Windows staged/committed marker checks omit installer/{relative}")
    windows_uninstaller = (ROOT / "installer/uninstall_windows.ps1").read_text(encoding="utf-8")
    require("$PSScriptRoot '..\\.rblstudio-install.json'" in windows_uninstaller,
            "Windows installed installer-directory uninstaller does not resolve its parent install root")

    for token in ("runtime/python/python-runtime.tar.gz", "installer/uninstall_linux.sh",
                  "installer/uninstall_windows.ps1", "installer/windows_registry_policy.ps1"):
        require(token in wizard, f"Wizard installer preflight omits {token}")
    require('"RBLStudio.sh"' in wizard,
            "Linux installed-copy launch must use the launcher that configures runtime library paths")

    print("PASS: Linux and Windows installed-copy repair payloads are staged and checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
