import os
import pathlib
import shutil
import subprocess
import tempfile
import time

import win32con
import win32gui
import win32process

ROOT = pathlib.Path.cwd()
GUI = ROOT / "build/wx-ui-win/rbl-studio-wx.exe"
WORKER = ROOT / "build/wx-ui-win/rbl-studio-worker.exe"
LIMIT = 16 * 1024 * 1024


def write_ascii_file(path: pathlib.Path, size: int) -> None:
    line = b"x" * 79 + b"\n"
    chunk_size = 1024 * 1024
    chunk = line * (chunk_size // len(line)) + b"x" * (chunk_size % len(line))
    with path.open("wb") as stream:
        remaining = size
        while remaining:
            block = chunk[:min(len(chunk), remaining)]
            stream.write(block)
            remaining -= len(block)


def find_frame(pid: int, timeout: float = 10.0) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames = []
        win32gui.EnumWindows(
            lambda hwnd, _: frames.append(hwnd)
            if win32gui.IsWindowVisible(hwnd)
            and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
            and win32gui.GetWindowText(hwnd) == "RBL Studio"
            else None,
            None,
        )
        if frames:
            return frames[0]
        time.sleep(0.1)
    raise RuntimeError("wx main frame did not appear")


def find_editor(frame: int) -> int:
    editors = []
    win32gui.EnumChildWindows(
        frame,
        lambda hwnd, _: editors.append(hwnd)
        if win32gui.GetClassName(hwnd) == "Scintilla"
        else None,
        None,
    )
    if not editors:
        raise RuntimeError("Scintilla editor was not found")
    return editors[0]


def wait_length(frame: int, expected: int, timeout: float = 60.0) -> None:
    user32 = __import__("ctypes").windll.user32
    user32.SendMessageA.argtypes = [
        __import__("ctypes").c_void_p,
        __import__("ctypes").c_uint,
        __import__("ctypes").c_size_t,
        __import__("ctypes").c_ssize_t,
    ]
    user32.SendMessageA.restype = __import__("ctypes").c_ssize_t
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        editors = []
        win32gui.EnumChildWindows(
            frame,
            lambda hwnd, _: editors.append(hwnd)
            if win32gui.GetClassName(hwnd) == "Scintilla"
            else None,
            None,
        )
        for editor in editors:
            if user32.SendMessageA(editor, 2183, 0, 0) == expected:
                return
        time.sleep(0.05)
    actual = [user32.SendMessageA(editor, 2183, 0, 0) for editor in editors]
    raise RuntimeError(f"expected Scintilla length {expected}, got {actual}")


def kill_tree(process: subprocess.Popen) -> None:
    if process.poll() is None:
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    process.wait(timeout=10)


def run_open(path: pathlib.Path, profile: pathlib.Path):
    env = os.environ.copy()
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = __import__("sys").executable
    process = subprocess.Popen([str(GUI), str(path)], cwd=ROOT, env=env)
    return process


def main() -> None:
    fixture = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-file-limit-", dir=ROOT / "build"))
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-file-limit-profile-"))
    try:
        exact = fixture / "exact-16MiB.rbl"
        oversized = fixture / "over-16MiB.rbl"
        write_ascii_file(exact, LIMIT)
        write_ascii_file(oversized, LIMIT + 1)

        p = run_open(exact, profile)
        try:
            frame = find_frame(p.pid)
            print("exact file size", exact.stat().st_size, flush=True)
            wait_length(frame, LIMIT, timeout=60.0)
            print("exact 16 MiB file: PASS", flush=True)
        finally:
            kill_tree(p)

        p = run_open(oversized, profile)
        try:
            frame = find_frame(p.pid)
            deadline = time.monotonic() + 10
            dialog = 0
            while time.monotonic() < deadline:
                windows = []
                win32gui.EnumWindows(
                    lambda hwnd, _: windows.append(hwnd)
                    if win32gui.IsWindowVisible(hwnd)
                    and win32process.GetWindowThreadProcessId(hwnd)[1] == p.pid
                    and win32gui.GetWindowText(hwnd) == "File too large"
                    else None,
                    None,
                )
                if windows:
                    dialog = windows[0]
                    break
                time.sleep(0.1)
            if not dialog:
                raise RuntimeError("oversized file did not show the File too large dialog")
            children = []
            win32gui.EnumChildWindows(dialog, lambda hwnd, _: children.append(hwnd), None)
            buttons = [hwnd for hwnd in children if win32gui.GetClassName(hwnd) == "Button"]
            if not buttons:
                raise RuntimeError("File too large dialog has no acknowledgement button")
            button_id = win32gui.GetDlgCtrlID(buttons[0])
            win32gui.SendMessage(
                dialog,
                win32con.WM_COMMAND,
                button_id,
                buttons[0],
            )
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and win32gui.IsWindow(dialog):
                time.sleep(0.05)
            if win32gui.IsWindow(dialog):
                raise RuntimeError("File too large dialog did not close after OK")
            time.sleep(0.3)
            if p.poll() is not None or not win32gui.IsWindow(frame):
                raise RuntimeError("rejecting the oversized file closed the IDE")
            editors = []
            win32gui.EnumChildWindows(
                frame,
                lambda hwnd, _: editors.append(hwnd)
                if win32gui.GetClassName(hwnd) == "Scintilla"
                else None,
                None,
            )
            lengths = [win32gui.SendMessage(hwnd, 2183, 0, 0) for hwnd in editors]
            if any(length != 0 for length in lengths):
                raise RuntimeError(f"oversized file changed editor buffers: {lengths}")
            print("16 MiB + 1 byte rejection: PASS", flush=True)
        finally:
            kill_tree(p)
        print("file size boundary: 2/2 PASS")
    finally:
        shutil.rmtree(fixture, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
