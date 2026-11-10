#!/usr/bin/env python3
"""Exercise large diagnostic bursts, sorted rows and UI responsiveness.

Builds a tiny test-only worker executable which speaks the existing line-based
worker protocol. The GUI itself, diagnostics parser, wxListCtrl and timers are
real; only the backend event source is synthetic.
"""

from __future__ import annotations

import ctypes
import os
import pathlib
import subprocess
import tempfile
import time

import psutil
import win32con
import win32gui
import win32process

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUI = pathlib.Path(os.environ.get("RBL_STUDIO_GUI", ROOT / "build/wx-ui-win/rbl-studio-wx.exe"))
GCC = ROOT / "toolchain/windows-x86_64/msys64/ucrt64/bin/gcc.exe"
WORK = ROOT / "build/wx-ui-win/diagnostics-scale"
STUB = WORK / "diagnostics-worker-stub.exe"
SOURCE = WORK / "diagnostics-scale.rbl"
CHECK_COMMAND = 6006
PROBLEMS_ID = 6023
EXPECTED_ROWS = 10000

STUB_C = r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static void json_escape(const char *s) {
    for (; *s; ++s) {
        switch (*s) {
        case '\\': fputs("\\\\", stdout); break;
        case '"': fputs("\\\"", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default: putchar(*s); break;
        }
    }
}

int main(void) {
    char input[65536];
    while (fgets(input, sizeof(input), stdin)) {
        if (strstr(input, "\"command\":\"initialize\"")) {
            puts("{\"event\":\"ready\"}");
            fflush(stdout);
        } else if (strstr(input, "\"command\":\"start\"")) {
            puts("{\"event\":\"started\",\"label\":\"Synthetic diagnostics\"}");
            if (getenv("RBL_DIAGNOSTICS_EXIT_WITHOUT_FINISHED")) {
                fputs("{\"event\":\"output\",\"stderr\":true,\"text\":\"C:\\\\synthetic\\\\crash.rbl:7:3: error: final line without newline\"}\n", stdout);
                fflush(stdout);
                return 23;
            }
            const char *stale_marker = getenv("RBL_DIAGNOSTICS_STALE_RACE");
            if (stale_marker) {
                FILE *marker = fopen(stale_marker, "wb");
                if (marker) {
                    fputs("worker accepted start request", marker);
                    fclose(marker);
                }
                Sleep(1500);
            }
            fputs("{\"event\":\"output\",\"stderr\":false,\"text\":\"C:\\\\synthetic\\\\stdout.rbl:99:1: error: stdout must not become a diagnostic\\n\"}\n", stdout);
            fputs("{\"event\":\"output\",\"stderr\":false,\"text\":\"", stdout);
            for (int i = 0; i < 70000; ++i) putchar('X');
            fputs("\\n\"}\n", stdout);
            for (int batch = 0; batch < 5; ++batch) {
                fputs("{\"event\":\"output\",\"stderr\":true,\"text\":\"", stdout);
                const int first = 10500 - batch * 2100;
                for (int i = first; i > first - 2100; --i) {
                    char diagnostic[192];
                    const char *path = i > 5250 ? "C:\\synthetic\\z.rbl" : "C:\\synthetic\\a.rbl";
                    snprintf(diagnostic, sizeof(diagnostic), "%s:%d:1: error: generated %d\n", path, i, i);
                    json_escape(diagnostic);
                }
                fputs("\"}\n", stdout);
            }
            puts("{\"event\":\"finished\",\"exit_code\":1}");
            fflush(stdout);
        } else if (strstr(input, "\"command\":\"shutdown\"")) {
            break;
        }
    }
    return 0;
}
'''


class LVITEM(ctypes.Structure):
    _fields_ = [
        ("mask", ctypes.c_uint), ("iItem", ctypes.c_int), ("iSubItem", ctypes.c_int),
        ("state", ctypes.c_uint), ("stateMask", ctypes.c_uint),
        ("pszText", ctypes.c_void_p), ("cchTextMax", ctypes.c_int),
        ("iImage", ctypes.c_int), ("lParam", ctypes.c_ssize_t),
        ("iIndent", ctypes.c_int), ("iGroupId", ctypes.c_int),
        ("cColumns", ctypes.c_uint), ("puColumns", ctypes.c_void_p),
        ("piColFmt", ctypes.c_void_p), ("iGroup", ctypes.c_int),
    ]


def main_frame(pid: int) -> int:
    found: list[int] = []
    win32gui.EnumWindows(
        lambda hwnd, _: found.append(hwnd)
        if win32gui.IsWindowVisible(hwnd)
        and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
        and win32gui.GetWindowText(hwnd) == "RBL Studio"
        else None,
        None,
    )
    if not found:
        raise RuntimeError("RBL Studio main frame was not found")
    return found[0]


def child_by_id(frame: int, control_id: int) -> int:
    found: list[int] = []
    win32gui.EnumChildWindows(
        frame, lambda hwnd, _: found.append(hwnd) if win32gui.GetDlgCtrlID(hwnd) == control_id else None, None
    )
    if not found:
        raise RuntimeError(f"control {control_id} was not found")
    return found[0]


def list_text(pid: int, hwnd: int, row: int, column: int) -> str:
    kernel32 = ctypes.windll.kernel32
    user32 = ctypes.windll.user32
    kernel32.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.VirtualAllocEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint]
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.WriteProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel32.VirtualFreeEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    process = kernel32.OpenProcess(0x0438, False, pid)
    remote_text = kernel32.VirtualAllocEx(process, None, 2048, 0x3000, 0x04)
    remote_item = kernel32.VirtualAllocEx(process, None, ctypes.sizeof(LVITEM), 0x3000, 0x04)
    try:
        item = LVITEM()
        item.iItem = row
        item.iSubItem = column
        item.pszText = remote_text
        item.cchTextMax = 512
        local = ctypes.create_string_buffer(bytes(item))
        written = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(process, remote_item, local, len(local), ctypes.byref(written)):
            raise RuntimeError("WriteProcessMemory failed for diagnostic list item")
        user32.SendMessageW(hwnd, 0x1073, row, remote_item)  # LVM_GETITEMTEXTW
        data = ctypes.create_string_buffer(2048)
        copied = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(process, remote_text, data, 2048, ctypes.byref(copied)):
            raise RuntimeError("ReadProcessMemory failed for diagnostic list text")
        return data.raw.decode("utf-16le", errors="replace").split("\0", 1)[0]
    finally:
        kernel32.VirtualFreeEx(process, remote_item, 0, 0x8000)
        kernel32.VirtualFreeEx(process, remote_text, 0, 0x8000)
        kernel32.CloseHandle(process)


def main() -> int:
    if os.name != "nt":
        raise SystemExit("Windows GUI test only")
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / "diagnostics-worker-stub.c").write_text(STUB_C, encoding="utf-8")
    SOURCE.write_text("func main() (\n    print(\"synthetic diagnostics\")\n)\n", encoding="utf-8")
    built = subprocess.run([str(GCC), "-O2", str(WORK / "diagnostics-worker-stub.c"), "-o", str(STUB)],
                           cwd=ROOT, capture_output=True, text=True)
    if built.returncode:
        vsdev = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat"
        if not vsdev.is_file():
            raise RuntimeError(f"could not build test worker stub with GCC: {built.stderr}")
        build_script = WORK / "build-stub.cmd"
        build_script.write_text(
            "@echo off\r\n"
            f'call "{vsdev}" -arch=x64 >nul\r\n'
            "if errorlevel 1 exit /b %errorlevel%\r\n"
            f'cl /nologo /O2 /Fe:"{STUB}" "{WORK / "diagnostics-worker-stub.c"}"\r\n',
            encoding="ascii",
        )
        built = subprocess.run(["cmd.exe", "/d", "/c", str(build_script)],
                               cwd=ROOT, capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f"could not build test worker stub with MSVC: {built.stdout}\n{built.stderr}")
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-diagnostics-scale-"))
    env = os.environ.copy()
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    env["RBL_STUDIO_WORKER"] = str(STUB)
    env["RBL_PYTHON"] = __import__("sys").executable
    process = subprocess.Popen([str(GUI), str(SOURCE)], cwd=ROOT, env=env)
    max_latency_ms = 0.0
    try:
        time.sleep(2.0)
        frame = main_frame(process.pid)
        problems = child_by_id(frame, PROBLEMS_ID)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, CHECK_COMMAND, 0)
        deadline = time.monotonic() + 90
        count = 0
        user32 = ctypes.windll.user32
        result = ctypes.c_size_t()
        while time.monotonic() < deadline:
            started = time.perf_counter()
            ok = user32.SendMessageTimeoutW(frame, win32con.WM_NULL, 0, 0, 0x0002, 200, ctypes.byref(result))
            latency = (time.perf_counter() - started) * 1000
            max_latency_ms = max(max_latency_ms, latency)
            if not ok:
                raise RuntimeError(f"GUI did not answer WM_NULL within 200 ms; last latency={latency:.1f} ms")
            count = user32.SendMessageW(problems, 0x1004, 0, 0)  # LVM_GETITEMCOUNT
            if count >= EXPECTED_ROWS:
                break
            time.sleep(0.02)
        if count != EXPECTED_ROWS:
            raise RuntimeError(f"expected capped {EXPECTED_ROWS} diagnostics, observed {count}")
        first_path = list_text(process.pid, problems, 0, 0)
        first_line = list_text(process.pid, problems, 0, 1)
        last_path = list_text(process.pid, problems, EXPECTED_ROWS - 1, 0)
        last_line = list_text(process.pid, problems, EXPECTED_ROWS - 1, 1)
        if not first_path.endswith("a.rbl") or first_line != "501":
            raise RuntimeError(f"diagnostic sort first row incorrect: {first_path}:{first_line}")
        if not last_path.endswith("z.rbl") or last_line != "10500":
            raise RuntimeError(f"diagnostic sort last row incorrect: {last_path}:{last_line}")
        tree = [psutil.Process(process.pid), *psutil.Process(process.pid).children(recursive=True)]
        tree_rss = sum(item.memory_info().rss for item in tree if item.is_running()) / (1024 * 1024)
        print(f"diagnostic rows: {count}/{EXPECTED_ROWS} PASS")
        print(f"sorted endpoints: {first_path}:{first_line} .. {last_path}:{last_line} PASS")
        print(f"max UI ping latency: {max_latency_ms:.1f} ms")
        print(f"process-tree RSS: {tree_rss:.1f} MiB")
        if max_latency_ms > 200 or tree_rss > 512:
            raise RuntimeError("diagnostic scale responsiveness/resource threshold exceeded")
        print("large diagnostics scale acceptance: PASS")
        stale_profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-diagnostics-stale-"))
        stale_env = env.copy()
        stale_env["RBL_STUDIO_CONFIG_DIR"] = str(stale_profile)
        stale_marker = stale_profile / "worker-started.marker"
        stale_env["RBL_DIAGNOSTICS_STALE_RACE"] = str(stale_marker)
        stale_process = subprocess.Popen([str(GUI), str(SOURCE)], cwd=ROOT, env=stale_env)
        try:
            time.sleep(2.0)
            stale_frame = main_frame(stale_process.pid)
            editors: list[int] = []
            win32gui.EnumChildWindows(
                stale_frame,
                lambda hwnd, _: editors.append(hwnd)
                if win32gui.GetClassName(hwnd) == "Scintilla" else None,
                None,
            )
            if not editors:
                raise RuntimeError("stale-race editor control was not found")
            stale_problems = child_by_id(stale_frame, PROBLEMS_ID)
            win32gui.PostMessage(stale_frame, win32con.WM_COMMAND, CHECK_COMMAND, 0)
            # Wait until the worker has actually received the start request.
            # Posting WM_COMMAND and immediately sending WM_CHAR can otherwise
            # edit the buffer before StartTask snapshots it, which is not the
            # intended edit-after-snapshot/before-first-output race.
            marker_deadline = time.monotonic() + 5
            while time.monotonic() < marker_deadline and not stale_marker.is_file():
                time.sleep(0.01)
            if not stale_marker.is_file():
                raise RuntimeError("stale-race worker did not acknowledge the start request")
            win32gui.SendMessage(editors[-1], win32con.WM_CHAR, ord("x"), 0)
            time.sleep(5.0)
            stale_count = ctypes.windll.user32.SendMessageW(stale_problems, 0x1004, 0, 0)
            if stale_count != 0:
                raise RuntimeError(
                    f"stale diagnostic race: expected zero rows after editing before first output, got {stale_count}"
                )
            print("edit-before-first-diagnostic invalidation: PASS")
        finally:
            if stale_process.poll() is None:
                subprocess.run(["taskkill", "/PID", str(stale_process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            import shutil
            shutil.rmtree(stale_profile, ignore_errors=True)

        crash_profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-diagnostics-worker-exit-"))
        crash_env = env.copy()
        crash_env["RBL_STUDIO_CONFIG_DIR"] = str(crash_profile)
        crash_env["RBL_DIAGNOSTICS_EXIT_WITHOUT_FINISHED"] = "1"
        crash_process = subprocess.Popen([str(GUI), str(SOURCE)], cwd=ROOT, env=crash_env)
        try:
            time.sleep(2.0)
            crash_frame = main_frame(crash_process.pid)
            crash_problems = child_by_id(crash_frame, PROBLEMS_ID)
            win32gui.PostMessage(crash_frame, win32con.WM_COMMAND, CHECK_COMMAND, 0)
            crash_deadline = time.monotonic() + 15
            crash_count = 0
            while time.monotonic() < crash_deadline:
                crash_count = user32.SendMessageW(crash_problems, 0x1004, 0, 0)
                if crash_count:
                    break
                time.sleep(0.05)
            if crash_count != 1:
                raise RuntimeError(
                    f"worker exit without finished event: expected 1 finalized diagnostic, got {crash_count}"
                )
            crash_path = list_text(crash_process.pid, crash_problems, 0, 0)
            crash_line = list_text(crash_process.pid, crash_problems, 0, 1)
            if not crash_path.endswith("crash.rbl") or crash_line != "7":
                raise RuntimeError(f"worker-exit diagnostic row incorrect: {crash_path}:{crash_line}")
            print("worker exit without finished: final unterminated stderr diagnostic finalized: PASS")
        finally:
            if crash_process.poll() is None:
                subprocess.run(["taskkill", "/PID", str(crash_process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            import shutil
            shutil.rmtree(crash_profile, ignore_errors=True)
        return 0
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        import shutil
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
