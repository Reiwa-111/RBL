import os
import pathlib
import subprocess
import tempfile
import shutil
import time
import ctypes
import win32con
import win32gui
import win32process

ROOT = pathlib.Path.cwd()
GUI = ROOT / "build/wx-ui-win/rbl-studio-wx.exe"
WORKER = ROOT / "build/wx-ui-win/rbl-studio-worker.exe"
FIXTURE = ROOT / "build/wx-ui-win/gui-acceptance/gui-stdin-eof.rbl"
OUTPUT = ROOT / "build/wx-ui-win/gui-acceptance/gui-stdin-eof-output.txt"
STDIN_ID, SEND_ID, EOF_ID = 6009, 6010, 6011


def status(pid, frame):
    k = ctypes.windll.kernel32
    u = ctypes.windll.user32
    k.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    k.OpenProcess.restype = ctypes.c_void_p
    k.VirtualAllocEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
    k.VirtualAllocEx.restype = ctypes.c_void_p
    k.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    k.ReadProcessMemory.restype = ctypes.c_int
    k.VirtualFreeEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint]
    k.CloseHandle.argtypes = [ctypes.c_void_p]
    u.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    u.SendMessageW.restype = ctypes.c_ssize_t
    bars = []
    win32gui.EnumChildWindows(frame, lambda h, _: bars.append(h) if win32gui.GetClassName(h) == "msctls_statusbar32" else None, None)
    process = k.OpenProcess(0x0438, False, pid)
    remote = k.VirtualAllocEx(process, None, 512, 0x3000, 4)
    try:
        parts = []
        for part in range(4):
            u.SendMessageW(bars[0], 0x040D, part, remote)
            buf = ctypes.create_string_buffer(512)
            copied = ctypes.c_size_t()
            k.ReadProcessMemory(process, remote, buf, 512, ctypes.byref(copied))
            parts.append(buf.raw.decode("utf-16le", errors="replace").split("\0", 1)[0])
        return parts
    finally:
        k.VirtualFreeEx(process, remote, 0, 0x8000)
        k.CloseHandle(process)


def main():
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.unlink(missing_ok=True)
    FIXTURE.write_text(
        "func main() (\n"
        "    set first = input()\n"
        f"    write_file(\"{OUTPUT.as_posix()}\", first)\n"
        "    set second = input()\n"
        "    print(first)\n"
        ")\n",
        encoding="utf-8",
    )
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-stdin-profile-"))
    env = os.environ.copy()
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = __import__("sys").executable
    process = subprocess.Popen([str(GUI), str(FIXTURE)], cwd=ROOT, env=env)
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
        def collect_control(hwnd, _):
            controls.setdefault(win32gui.GetDlgCtrlID(hwnd), hwnd)
        win32gui.EnumChildWindows(
            frame, collect_control, None
        )
        for control_id in (STDIN_ID, SEND_ID, EOF_ID):
            if control_id not in controls:
                raise RuntimeError(f"stdin control {control_id} missing")
        win32gui.PostMessage(frame, win32con.WM_COMMAND, 6005, 0)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if all(win32gui.IsWindowEnabled(controls[key]) for key in (STDIN_ID, SEND_ID, EOF_ID)):
                break
            time.sleep(.05)
        else:
            raise RuntimeError("stdin controls did not become enabled for Run")
        win32gui.SendMessage(controls[STDIN_ID], win32con.WM_SETTEXT, 0, "ordered-line")
        win32gui.SendMessage(controls[EOF_ID], win32con.BM_CLICK, 0, 0)
        deadline = time.monotonic() + 20
        final_status = ""
        while time.monotonic() < deadline:
            final_status = status(process.pid, frame)[2]
            if final_status.startswith("Exit code 1"):
                break
            time.sleep(.05)
        if final_status != "Exit code 1":
            raise RuntimeError(f"child did not finish after EOF: {final_status}")
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != "ordered-line":
            raise RuntimeError("queued stdin line was not delivered before EOF")
        if any(win32gui.IsWindowEnabled(controls[key]) for key in (STDIN_ID, SEND_ID, EOF_ID)):
            raise RuntimeError("stdin controls remained enabled after EOF")
        print("wx GUI stdin line + immediate EOF ordering: PASS", flush=True)
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        shutil.rmtree(profile, ignore_errors=True)
        OUTPUT.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
