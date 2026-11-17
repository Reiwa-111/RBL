from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def normalize_app_root(path: str) -> str:
    """Mirror the launcher's trailing-separator rule for focused path cases."""
    if path.endswith("\\") and len(path) > 3:
        return path[:-1]
    return path


def test_drive_root_keeps_its_trailing_separator() -> None:
    assert normalize_app_root("C:\\") == "C:\\"


def test_normal_directory_path_loses_only_its_trailing_separator() -> None:
    assert normalize_app_root("C:\\Program Files\\RBL Studio\\") == (
        "C:\\Program Files\\RBL Studio"
    )


def test_batch_launcher_uses_the_drive_root_guard() -> None:
    source = (ROOT / "RBLStudio.cmd").read_text(encoding="utf-8")
    assert (
        'if not "%APP_ROOT:~3%"=="" if "%APP_ROOT:~-1%"=="\\" '
        'set "APP_ROOT=%APP_ROOT:~0,-1%"'
    ) in source


if __name__ == "__main__":
    test_drive_root_keeps_its_trailing_separator()
    test_normal_directory_path_loses_only_its_trailing_separator()
    test_batch_launcher_uses_the_drive_root_guard()
    print("Windows launcher root normalization: PASS")
