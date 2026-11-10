#!/usr/bin/env python3
"""Windows GUI acceptance for mouse-wheel routing to the hovered project tree.

The test deliberately sends WM_MOUSEWHEEL to the main frame while the pointer
is over the project tree. The app must route the wheel to the tree.
Requires an interactive Windows desktop and pywin32.
"""

from __future__ import annotations

import ctypes
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

if os.name != "nt":
    raise SystemExit("This GUI acceptance test requires Windows.")

try:
    import win32con
    import win32gui
    import win32process
except ImportError as exc:  # pragma: no cover - depends on Windows desktop setup
    raise SystemExit("Install pywin32 to run this GUI acceptance test.") from exc


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "wx-ui-win-rc"
GUI = pathlib.Path(os.environ.get("RBL_STUDIO_GUI", BUILD / "rbl-studio-wx.exe"))
WORKER = pathlib.Path(os.environ.get("RBL_STUDIO_WORKER", BUILD / "rbl-studio-worker.exe"))

TVM_GETNEXTITEM = 0x110A
TVGN_ROOT = 0
TVGN_FIRSTVISIBLE = 5
TVM_GETCOUNT = 0x1105
TVM_EXPAND = 0x1102
TVE_EXPAND = 0x0002
WM_MOUSEWHEEL = 0x020A


def wait_for_frame(pid: int, timeout: float = 15.0) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames: list[int] = []
        win32gui.EnumWindows(
            lambda hwnd, _: frames.append(hwnd)
            if win32gui.IsWindowVisible(hwnd)
            and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
            and "RBL Studio" in win32gui.GetWindowText(hwnd)
            else None,
            None,
        )
        if frames:
            return frames[0]
        time.sleep(0.1)
    raise RuntimeError("RBL Studio main frame did not appear")


def child_by_class(frame: int, class_name: str) -> int:
    found: list[int] = []
    win32gui.EnumChildWindows(
        frame,
        lambda hwnd, _: found.append(hwnd)
        if win32gui.GetClassName(hwnd) == class_name else None,
        None,
    )
    if not found:
        raise RuntimeError(f"child window class {class_name!r} not found")
    return found[0]


def first_visible_tree_item(tree: int) -> int:
    return int(win32gui.SendMessage(tree, TVM_GETNEXTITEM, TVGN_FIRSTVISIBLE, 0))


def main() -> int:
    if not GUI.is_file() or not WORKER.is_file():
        raise SystemExit(f"Windows GUI candidate or worker missing: {GUI} / {WORKER}")

    fixture = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wheel-project-"))
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wheel-profile-"))
    process: subprocess.Popen[bytes] | None = None
    frame = 0
    old_cursor = win32gui.GetCursorPos()
    try:
        source = fixture / "main.rbl"
        source.write_text('func main() {\n    print("wheel test")\n}\n', encoding="utf-8")
        for index in range(1600):
            (fixture / f"entry-{index:04d}.txt").touch()
        (profile / "ui.ini").write_text(
            "[Workspace]\nRoot=" + fixture.as_posix() + "\n", encoding="utf-8"
        )

        env = os.environ.copy()
        env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
        env["RBL_STUDIO_WORKER"] = str(WORKER)
        env["RBL_PYTHON"] = sys.executable
        process = subprocess.Popen(
            [str(GUI), str(source)], cwd=ROOT, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

        frame = wait_for_frame(process.pid)
        tree = child_by_class(frame, "SysTreeView32")
        root_item = int(win32gui.SendMessage(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0))
        if not root_item:
            raise RuntimeError("project tree root item is missing")
        win32gui.SendMessage(tree, TVM_EXPAND, TVE_EXPAND, root_item)
        deadline = time.monotonic() + 10.0
        count = 0
        while time.monotonic() < deadline:
            count = int(win32gui.SendMessage(tree, TVM_GETCOUNT, 0, 0))
            if count >= 1000:
                break
            time.sleep(0.1)
        if count < 1000:
            raise RuntimeError(f"project tree did not populate enough rows for a scroll test: {count}")

        rectangle = win32gui.GetWindowRect(tree)
        x = (rectangle[0] + rectangle[2]) // 2
        y = (rectangle[1] + rectangle[3]) // 2
        ctypes.windll.user32.SetCursorPos(x, y)
        try:
            win32gui.SetForegroundWindow(frame)
        except Exception:
            pass

        before = first_visible_tree_item(tree)
        if not before:
            raise RuntimeError("project tree has no visible item")

        lparam = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
        wparam = ((-120 & 0xFFFF) << 16)  # one wheel notch downward
        for _ in range(5):
            win32gui.SendMessage(frame, WM_MOUSEWHEEL, wparam, lparam)
            time.sleep(0.08)

        deadline = time.monotonic() + 2.0
        after = before
        while time.monotonic() < deadline:
            after = first_visible_tree_item(tree)
            if after != before:
                break
            time.sleep(0.05)

        if after == before:
            raise RuntimeError(
                "project tree did not scroll when WM_MOUSEWHEEL was sent to the editor"
            )
        print("hover wheel routing: PASS (main frame -> project tree)", flush=True)
        return 0
    finally:
        ctypes.windll.user32.SetCursorPos(*old_cursor)
        if process is not None and process.poll() is None:
            if frame and win32gui.IsWindow(frame):
                win32gui.PostMessage(frame, win32con.WM_CLOSE, 0, 0)
                try:
                    process.wait(timeout=4.0)
                except subprocess.TimeoutExpired:
                    pass
            if process.poll() is None:
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                )
        shutil.rmtree(fixture, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
