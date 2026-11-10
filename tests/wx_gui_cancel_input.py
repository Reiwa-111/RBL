import os
import pathlib
import subprocess
import tempfile
import time
import shutil
import ctypes
import psutil
import win32con
import win32gui
import win32process

ROOT = pathlib.Path.cwd()
GUI = ROOT / "build/wx-ui-win/rbl-studio-wx.exe"
WORKER = ROOT / "build/wx-ui-win/rbl-studio-worker.exe"
SOURCE = ROOT / "build/wx-ui-win/gui-acceptance/cancel-loop.rbl"
RUN_COMMAND = 6005
STOP_COMMAND = 6008
STDIN_ID = 6009
SEND_ID = 6010
EOF_ID = 6011


def status(pid, frame):
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    kernel32.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.VirtualAllocEx.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint
    ]
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.ReadProcessMemory.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
    ]
    kernel32.ReadProcessMemory.restype = ctypes.c_int
    kernel32.VirtualFreeEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    bars = []
    win32gui.EnumChildWindows(
        frame, lambda h, _: bars.append(h) if win32gui.GetClassName(h) == "msctls_statusbar32" else None, None
    )
    handle = kernel32.OpenProcess(0x0438, False, pid)
    remote = kernel32.VirtualAllocEx(handle, None, 512, 0x3000, 4)
    try:
        values = []
        for part in range(4):
            user32.SendMessageW(bars[0], 0x040D, part, remote)
            buf = ctypes.create_string_buffer(512)
            copied = ctypes.c_size_t()
            kernel32.ReadProcessMemory(handle, remote, buf, 512, ctypes.byref(copied))
            values.append(buf.raw.decode("utf-16le", errors="replace").split("\0")[0])
        return values
    finally:
        kernel32.VirtualFreeEx(handle, remote, 0, 0x8000)
        kernel32.CloseHandle(handle)


def main():
    if not SOURCE.is_file():
        raise RuntimeError(f"loop fixture missing: {SOURCE}")
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-cancel-profile-"))
    env = os.environ.copy()
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = __import__("sys").executable
    process = subprocess.Popen([str(GUI), str(SOURCE)], cwd=ROOT, env=env)
    try:
        time.sleep(4)
        frames = []
        win32gui.EnumWindows(
            lambda h, _: frames.append(h)
            if win32gui.IsWindowVisible(h)
            and win32process.GetWindowThreadProcessId(h)[1] == process.pid
            and win32gui.GetWindowText(h) == "RBL Studio"
            else None,
            None,
        )
        if not frames:
            raise RuntimeError("main frame missing")
        frame = frames[0]
        controls = {}
        win32gui.EnumChildWindows(
            frame,
            lambda h, _: controls.setdefault(win32gui.GetDlgCtrlID(h), h),
            None,
        )
        for control_id in (STDIN_ID, SEND_ID, EOF_ID):
            if control_id not in controls:
                raise RuntimeError(f"stdin control {control_id} missing")

        def run_and_cancel(iteration):
            before = status(process.pid, frame)[2]
            win32gui.PostMessage(frame, win32con.WM_COMMAND, RUN_COMMAND, 0)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                current = status(process.pid, frame)[2]
                if current != before and current in ("Running", "Starting run"):
                    break
                time.sleep(.05)
            else:
                raise RuntimeError(f"Run did not start, status={status(process.pid, frame)}")
            if not all(win32gui.IsWindowEnabled(controls[key]) for key in (STDIN_ID, SEND_ID, EOF_ID)):
                raise RuntimeError("stdin controls were not enabled for Run")
            win32gui.PostMessage(frame, win32con.WM_COMMAND, STOP_COMMAND, 0)
            time.sleep(.1)
            enabled = {key: win32gui.IsWindowEnabled(controls[key]) for key in (STDIN_ID, SEND_ID, EOF_ID)}
            if any(enabled.values()):
                raise RuntimeError(f"stdin controls remained enabled after Stop: {enabled}")
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                current = status(process.pid, frame)[2]
                if current.startswith("Exit code 130"):
                    print(f"Run {iteration}: cancelled; stdin disabled; {current}", flush=True)
                    return
                time.sleep(.05)
            raise RuntimeError(f"cancel completion timed out, status={status(process.pid, frame)}")

        run_and_cancel(1)
        worker_pids = [child.pid for child in psutil.Process(process.pid).children(recursive=True)
                       if child.name().lower() == "rbl-studio-worker.exe"]
        run_and_cancel(2)
        worker_pids_after = [child.pid for child in psutil.Process(process.pid).children(recursive=True)
                             if child.name().lower() == "rbl-studio-worker.exe"]
        if worker_pids != worker_pids_after or len(worker_pids_after) != 1:
            raise RuntimeError(f"worker did not remain stable across cancellation: {worker_pids} -> {worker_pids_after}")
        print("GUI Stop/input gating and repeated cancellation: 2/2 PASS", flush=True)
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
