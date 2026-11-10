import os
import pathlib
import subprocess
import tempfile
import shutil
import time
import psutil
import win32con
import win32gui
import win32process

ROOT = pathlib.Path.cwd()
GUI = ROOT / "build/wx-ui-win/rbl-studio-wx.exe"
WORKER = ROOT / "build/wx-ui-win/rbl-studio-worker.exe"
TESTS_COMMAND_ID = 6020


def status_parts(pid, frame):
    import ctypes
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    kernel32.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.VirtualAllocEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel32.ReadProcessMemory.restype = ctypes.c_int
    kernel32.VirtualFreeEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    bars = []
    win32gui.EnumChildWindows(frame, lambda h, _: bars.append(h) if win32gui.GetClassName(h) == "msctls_statusbar32" else None, None)
    handle = kernel32.OpenProcess(0x0438, False, pid)
    remote = kernel32.VirtualAllocEx(handle, None, 512, 0x3000, 4)
    try:
        parts = []
        for part in range(4):
            user32.SendMessageW(bars[0], 0x040D, part, remote)
            buf = ctypes.create_string_buffer(512)
            copied = ctypes.c_size_t()
            kernel32.ReadProcessMemory(handle, remote, buf, 512, ctypes.byref(copied))
            parts.append(buf.raw.decode("utf-16le", errors="replace").split("\0", 1)[0])
        return parts
    finally:
        kernel32.VirtualFreeEx(handle, remote, 0, 0x8000)
        kernel32.CloseHandle(handle)


def main():
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-tests-process-"))
    env = os.environ.copy()
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = __import__("sys").executable
    process = subprocess.Popen([str(GUI)], cwd=ROOT, env=env)
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
            raise RuntimeError("RBL Studio frame missing")
        frame = frames[0]
        before = status_parts(process.pid, frame)[2]
        win32gui.PostMessage(frame, win32con.WM_COMMAND, TESTS_COMMAND_ID, 0)
        deadline = time.monotonic() + 20
        launched = None
        observed = []
        while time.monotonic() < deadline:
            try:
                root = psutil.Process(process.pid)
                children = root.children(recursive=True)
                for child in children:
                    try:
                        cmdline = child.cmdline()
                        rendered = " ".join(cmdline)
                        observed.append((child.pid, child.name(), rendered))
                        if any(pathlib.Path(arg).name.lower() == "run_tests.py" for arg in cmdline):
                            launched = (child.pid, child.name(), rendered)
                            break
                    except (psutil.NoSuchProcess, psutil.AccessDenied):
                        continue
            except psutil.NoSuchProcess:
                break
            if launched:
                break
            time.sleep(.05)
        if not launched:
            raise RuntimeError(f"Tests command never launched tests/run_tests.py; observed={observed[-10:]}")
        print("actual Tests child launched:", launched, flush=True)
        final = None
        transitioned = False
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            current = status_parts(process.pid, frame)[2]
            if current != before:
                transitioned = True
            if transitioned and (current == "Finished" or current.startswith("Exit code ")):
                final = current
                break
            time.sleep(.1)
        if final != "Finished":
            raise RuntimeError(f"Tests process launched but did not finish successfully: {final}")
        print("GUI Tests command ID 6020: actual tests/run_tests.py child observed; Finished: PASS", flush=True)
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
