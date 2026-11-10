import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import ctypes
import psutil
import win32con
import win32gui
import win32process
import win32api
import win32clipboard

ROOT = pathlib.Path.cwd()
GUI = pathlib.Path(os.environ.get("RBL_STUDIO_GUI", ROOT / "build/wx-ui-win/rbl-studio-wx.exe"))
WORKER = pathlib.Path(os.environ.get("RBL_STUDIO_WORKER", ROOT / "build/wx-ui-win/rbl-studio-worker.exe"))
FIXTURE = ROOT / "build/wx-ui-win/search-result-stress"
QUERY = "SEARCH_RESULT_SENTINEL"


def frame_for(pid):
    found = []
    win32gui.EnumWindows(
        lambda h, _: found.append(h)
        if win32gui.IsWindowVisible(h)
        and win32process.GetWindowThreadProcessId(h)[1] == pid
        and win32gui.GetWindowText(h) == "RBL Studio"
        else None,
        None,
    )
    if not found:
        raise RuntimeError("main frame missing")
    return found[0]


def submit_search(pid, frame):
    win32gui.PostMessage(frame, win32con.WM_COMMAND, 6025, 0)
    dialog = 0
    for _ in range(100):
        time.sleep(.05)
        found = []
        win32gui.EnumWindows(
            lambda h, _: found.append(h)
            if win32gui.IsWindowVisible(h)
            and win32process.GetWindowThreadProcessId(h)[1] == pid
            and win32gui.GetWindowText(h) == "Find in Files"
            else None,
            None,
        )
        if found:
            dialog = found[0]
            break
    if not dialog:
        raise RuntimeError("Find in Files dialog did not appear")
    edits = []
    win32gui.EnumChildWindows(
        dialog,
        lambda h, _: edits.append(h) if win32gui.GetClassName(h) == "Edit" else None,
        None,
    )
    if not edits:
        raise RuntimeError("search text field missing")
    user32 = ctypes.windll.user32
    current = ctypes.windll.kernel32.GetCurrentThreadId()
    target = win32process.GetWindowThreadProcessId(dialog)[0]
    user32.AttachThreadInput(current, target, True)
    try:
        win32api.keybd_event(win32con.VK_MENU, 0, 0, 0)
        time.sleep(.03)
        try:
            win32gui.SetForegroundWindow(dialog)
        except Exception:
            pass
        win32api.keybd_event(win32con.VK_MENU, 0, win32con.KEYEVENTF_KEYUP, 0)
        win32gui.SetFocus(edits[0])
    finally:
        user32.AttachThreadInput(current, target, False)
    win32clipboard.OpenClipboard()
    try:
        win32clipboard.EmptyClipboard()
        win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT, QUERY)
    finally:
        win32clipboard.CloseClipboard()

    def key(vk, flags=0):
        win32api.keybd_event(vk, 0, flags, 0)

    def chord(vk):
        key(win32con.VK_CONTROL)
        key(vk)
        time.sleep(.04)
        key(vk, win32con.KEYEVENTF_KEYUP)
        key(win32con.VK_CONTROL, win32con.KEYEVENTF_KEYUP)

    chord(ord("A"))
    chord(ord("V"))
    time.sleep(.1)
    key(win32con.VK_RETURN)
    key(win32con.VK_RETURN, win32con.KEYEVENTF_KEYUP)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if not win32gui.IsWindow(dialog):
            return
        time.sleep(.05)
    raise RuntimeError("Find in Files dialog did not close")


def process_tree_memory(pid):
    try:
        root = psutil.Process(pid)
        procs = [root] + root.children(recursive=True)
        rss = uss = 0
        for proc in procs:
            try:
                rss += proc.memory_info().rss
                uss += proc.memory_full_info().uss
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        return rss, uss
    except psutil.NoSuchProcess:
        return 0, 0


def main():
    FIXTURE.mkdir(parents=True, exist_ok=True)
    try:
        line = (QUERY + " alpha beta gamma delta epsilon zeta eta theta\n").encode()
        for index in range(120):
            (FIXTURE / f"matches-{index:03d}.txt").write_bytes(line * 100)
        profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-search-profile-"))
        (profile / "ui.ini").write_text(
            "[Workspace]\nRoot=" + FIXTURE.as_posix() + "\n", encoding="utf-8"
        )
        env = os.environ.copy()
        env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
        env["RBL_STUDIO_WORKER"] = str(WORKER)
        env["RBL_PYTHON"] = __import__("sys").executable
        process = subprocess.Popen([str(GUI)], cwd=ROOT, env=env)
        try:
            time.sleep(4)
            frame = frame_for(process.pid)
            lists = []
            win32gui.EnumChildWindows(
                frame,
                lambda h, _: lists.append(h)
                if win32gui.GetClassName(h) == "SysListView32"
                and win32gui.GetDlgCtrlID(h) == 6026
                else None,
                None,
            )
            if not lists:
                raise RuntimeError("Search Results list missing")
            results = lists[0]
            submit_search(process.pid, frame)
            user32 = ctypes.windll.user32
            user32.SendMessageTimeoutW.argtypes = [
                ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t,
                ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t)
            ]
            user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
            deadline = time.monotonic() + 30
            peak_rss = peak_uss = 0
            max_ui_ping_ms = 0.0
            count = 0
            while time.monotonic() < deadline:
                rss, uss = process_tree_memory(process.pid)
                peak_rss = max(peak_rss, rss)
                peak_uss = max(peak_uss, uss)
                started = time.perf_counter()
                result = ctypes.c_size_t()
                ok = user32.SendMessageTimeoutW(
                    frame, win32con.WM_NULL, 0, 0, 0x0002, 1000, ctypes.byref(result)
                )
                latency = (time.perf_counter() - started) * 1000
                max_ui_ping_ms = max(max_ui_ping_ms, latency)
                if not ok:
                    raise RuntimeError("GUI stopped responding during result-list population")
                count = win32gui.SendMessage(results, 0x1004, 0, 0)
                if count == 10000:
                    break
                time.sleep(.02)
            elapsed = time.monotonic() - (deadline - 30)
            print("results", count, "elapsed_s", round(elapsed, 2), flush=True)
            print(
                "process_tree_peak_rss_mib", round(peak_rss / 1048576, 1),
                "peak_uss_mib", round(peak_uss / 1048576, 1),
                "max_ui_ping_ms", round(max_ui_ping_ms, 2),
                flush=True,
            )
            if count != 10000:
                raise RuntimeError(f"expected 10,000 capped results, got {count}")
            if peak_rss > 512 * 1048576:
                raise RuntimeError("search result workload exceeded 512 MiB RSS")
            if max_ui_ping_ms > 500:
                raise RuntimeError("GUI responsiveness exceeded 500 ms")
            print("Find in Files 10,000-result stress: PASS", flush=True)
        finally:
            if process.poll() is None:
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
                )
            shutil.rmtree(profile, ignore_errors=True)
    finally:
        shutil.rmtree(FIXTURE, ignore_errors=True)


if __name__ == "__main__":
    main()
