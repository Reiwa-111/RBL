#!/usr/bin/env python3
"""Bounded Windows wx frontend resource acceptance workload.

Requires an interactive desktop, pywin32 and psutil. Builds 31 × 2 MiB text files
plus a small RBL task fixture, opens all 32 documents in the actual wx GUI,
performs Scintilla edit/undo/redo operations,
starts the IDE's project test task, samples the complete process tree, closes
all tabs and samples the post-close footprint.

Run from the repository root after `tools/build_wx_ide.ps1`:

    python benchmarks/wx_resource_acceptance.py --duration 180

Fixtures and CSV output are kept under build/wx-ui-win/resource-acceptance/.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
import pathlib
import subprocess
import sys
import time
import ctypes
import tempfile
import shutil
import traceback

try:
    import psutil
    import win32con
    import win32gui
    import win32process
except ImportError as exc:  # pragma: no cover - Windows desktop dependency
    raise SystemExit("Install psutil and pywin32 to run this benchmark.") from exc


ROOT = pathlib.Path(__file__).resolve().parents[1]
GUI = pathlib.Path(os.environ.get(
    "RBL_STUDIO_GUI", ROOT / "build" / "wx-ui-win" / "rbl-studio-wx.exe"
)).resolve()
WORKER = pathlib.Path(os.environ.get(
    "RBL_STUDIO_WORKER", GUI.with_name("rbl-studio-worker.exe")
)).resolve()
ID_RUN_TESTS = 6020
ID_CLOSE_ALL_TABS = 6036
ID_RUN = 6005
ID_CHECK = 6006
ID_BUILD = 6007
ID_STOP = 6008
SCI_APPENDTEXT = 2282
SCI_UNDO = 2176
SCI_REDO = 2011
SCI_SETSAVEPOINT = 2014
SCI_GETLENGTH = 2006


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def write_fixture(path: pathlib.Path, size: int, prefix: bytes) -> None:
    block = (prefix + b"\n") * (64 * 1024 // (len(prefix) + 1))
    with path.open("wb") as stream:
        remaining = size
        while remaining:
            chunk = block[: min(len(block), remaining)]
            stream.write(chunk)
            remaining -= len(chunk)


def main_frame(pid: int) -> int:
    windows: list[int] = []
    win32gui.EnumWindows(
        lambda hwnd, _: windows.append(hwnd)
        if win32gui.IsWindowVisible(hwnd)
        and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
        and win32gui.GetWindowText(hwnd) == "RBL Studio"
        else None,
        None,
    )
    if not windows:
        raise RuntimeError("RBL Studio main frame was not found")
    return windows[0]


def editor_hwnds(frame: int) -> list[int]:
    editors: list[int] = []
    win32gui.EnumChildWindows(
        frame,
        lambda hwnd, _: editors.append(hwnd)
        if win32gui.GetClassName(hwnd) == "Scintilla"
        else None,
        None,
    )
    return editors


def status_text(pid: int, frame: int, part: int = 2) -> str:
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
    kernel32.VirtualFreeEx.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint
    ]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    user32.SendMessageTimeoutW.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t,
        ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t)
    ]
    user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
    bars: list[int] = []
    win32gui.EnumChildWindows(
        frame,
        lambda hwnd, _: bars.append(hwnd)
        if win32gui.GetClassName(hwnd) == "msctls_statusbar32"
        else None,
        None,
    )
    if not bars:
        raise RuntimeError("wx status bar was not found")
    process = kernel32.OpenProcess(0x0438, False, pid)
    if not process:
        raise RuntimeError("could not open GUI process to read task status")
    remote = kernel32.VirtualAllocEx(process, None, 512, 0x3000, 0x04)
    if not remote:
        kernel32.CloseHandle(process)
        raise RuntimeError("could not allocate status text buffer")
    try:
        result = ctypes.c_size_t()
        if not user32.SendMessageTimeoutW(
            bars[0], 0x040D, part, remote, 0x0002, 1000, ctypes.byref(result)
        ):
            raise RuntimeError("timed out reading wx status text")
        buffer = ctypes.create_string_buffer(512)
        copied = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(
            process, remote, buffer, 512, ctypes.byref(copied)
        ):
            raise RuntimeError("could not read wx status text")
        return buffer.raw.decode("utf-16le", errors="replace").split("\0", 1)[0]
    finally:
        kernel32.VirtualFreeEx(process, remote, 0, 0x8000)
        kernel32.CloseHandle(process)


def dispatch_task_command(pid: int, frame: int, command_id: int,
                          timeout_seconds: float = 15.0) -> tuple[str, str, bool]:
    """Synchronously deliver a task command, then observe its status transition.

    PostMessage plus a 10 ms polling delay can miss a short task that changes
    Finished -> Starting -> Finished between samples (notably an incremental
    no-op Build). SendMessageTimeout makes the command handler run before the
    first status read, while still bounding a stuck UI thread.
    """
    before = status_text(pid, frame)
    user32 = ctypes.windll.user32
    user32.SendMessageTimeoutW.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t,
        ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t)
    ]
    user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
    result = ctypes.c_size_t()
    delivered = user32.SendMessageTimeoutW(
        frame, win32con.WM_COMMAND, command_id, 0, 0x0002, 5000, ctypes.byref(result)
    )
    if not delivered:
        return before, before, False

    after = status_text(pid, frame)
    deadline = time.monotonic() + timeout_seconds
    while after == before and time.monotonic() < deadline:
        time.sleep(0.001)
        after = status_text(pid, frame)
    return before, after, after != before


def gui_resource_counts(pid: int) -> tuple[int, int]:
    """Return process GDI/USER object counts; zero when the process is inaccessible."""
    kernel32 = ctypes.windll.kernel32
    user32 = ctypes.windll.user32
    kernel32.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    user32.GetGuiResources.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    user32.GetGuiResources.restype = ctypes.c_uint
    handle = kernel32.OpenProcess(0x0400, False, pid)  # PROCESS_QUERY_INFORMATION
    if not handle:
        return 0, 0
    try:
        return (
            int(user32.GetGuiResources(handle, 0)),  # GR_GDIOBJECTS
            int(user32.GetGuiResources(handle, 1)),  # GR_USEROBJECTS
        )
    finally:
        kernel32.CloseHandle(handle)


def remote_scintilla_message(pid: int, editor: int, message: int, data: bytes = b"") -> int:
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    user32.SendMessageA.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t
    ]
    user32.SendMessageA.restype = ctypes.c_ssize_t
    kernel32.OpenProcess.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.VirtualAllocEx.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint
    ]
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.WriteProcessMemory.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
    ]
    kernel32.WriteProcessMemory.restype = ctypes.c_int
    kernel32.VirtualFreeEx.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint
    ]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]

    process = kernel32.OpenProcess(0x0438, False, pid)
    if not process:
        raise RuntimeError("could not open GUI process for Scintilla workload")
    remote = None
    try:
        if data:
            buffer = ctypes.create_string_buffer(data)
            remote = kernel32.VirtualAllocEx(process, None, len(data), 0x3000, 0x04)
            if not remote:
                raise RuntimeError("could not allocate Scintilla workload buffer")
            written = ctypes.c_size_t()
            if not kernel32.WriteProcessMemory(
                process, remote, buffer, len(data), ctypes.byref(written)
            ) or written.value != len(data):
                raise RuntimeError("could not copy Scintilla workload buffer")
        if message == SCI_APPENDTEXT:
            result = user32.SendMessageA(editor, message, len(data), remote)
        else:
            result = user32.SendMessageA(editor, message, 0, 0)
        return int(result)
    finally:
        if remote:
            kernel32.VirtualFreeEx(process, remote, 0, 0x8000)
        kernel32.CloseHandle(process)


def process_tree_sample(
    pid: int, frame: int, active_task: str, task_cycle: int, ping_timeout_ms: int
) -> dict[str, float | int | str]:
    try:
        root = psutil.Process(pid)
        processes = [root, *root.children(recursive=True)]
    except psutil.Error as exc:
        raise RuntimeError(f"GUI process tree disappeared during sampling (pid {pid}): {exc}") from exc
    working_set = private_bytes = cpu_seconds = 0
    gui_working_set = gui_private = 0
    handle_count = gdi_objects = user_objects = 0
    alive = 0
    process_names: list[str] = []
    process_commands: list[str] = []
    for process in processes:
        try:
            info = process.memory_info()
            cpu = process.cpu_times()
            ws = int(info.rss)
            private = int(getattr(info, "private", ws))
            working_set += ws
            private_bytes += private
            cpu_seconds += cpu.user + cpu.system
            try:
                handle_count += int(process.num_handles())
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                pass
            gdi, user = gui_resource_counts(process.pid)
            gdi_objects += gdi
            user_objects += user
            alive += 1
            process_names.append(f"{process.pid}:{process.name()}")
            process_commands.append(f"{process.pid}:{' '.join(process.cmdline())}")
            if process.pid == pid:
                gui_working_set = ws
                gui_private = private
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    mib = 1024 * 1024
    ping_started = time.perf_counter()
    result = ctypes.c_size_t()
    user32 = ctypes.windll.user32
    user32.SendMessageTimeoutW.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t,
        ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t)
    ]
    user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
    ping_ok = user32.SendMessageTimeoutW(
        frame, win32con.WM_NULL, 0, 0, 0x0002, ping_timeout_ms, ctypes.byref(result)
    )
    ping_ms = (time.perf_counter() - ping_started) * 1000
    return {
        "timestamp": time.time(),
        "process_count": alive,
        "gui_working_set_mib": gui_working_set / mib,
        "gui_private_mib": gui_private / mib,
        "tree_working_set_mib": working_set / mib,
        "tree_private_mib": private_bytes / mib,
        "tree_cpu_seconds": cpu_seconds,
        "tree_handle_count": handle_count,
        "tree_gdi_objects": gdi_objects,
        "tree_user_objects": user_objects,
        "ui_ping_ms": ping_ms if ping_ok else max(ping_ms, float(ping_timeout_ms)),
        "active_task": active_task,
        "task_cycle": task_cycle,
        "process_names": "; ".join(process_names),
        "process_commands": " | ".join(process_commands),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=180, choices=range(60, 7201))
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--cycle-interval", type=int, default=60)
    parser.add_argument("--max-ui-ping-ms", type=float, default=500.0)
    args = parser.parse_args()
    if args.interval <= 0 or args.cycle_interval <= 0 or args.max_ui_ping_ms <= 0:
        parser.error("interval, cycle-interval and max-ui-ping-ms must be positive")
    if not GUI.is_file() or not WORKER.is_file():
        raise SystemExit("Build the Windows wx GUI and worker first.")
    ping_timeout_ms = max(1000, int(args.max_ui_ping_ms + 100))

    output_dir = GUI.parent / "resource-acceptance"
    output_dir.mkdir(parents=True, exist_ok=True)
    paths: list[pathlib.Path] = []
    for index in range(31):
        path = output_dir / f"resource-tab-{index:02d}.txt"
        write_fixture(path, 2 * 1024 * 1024, f"TAB{index:02d}_RBL_RESOURCE".encode())
        paths.append(path)
    source = output_dir / "resource-task.rbl"
    source.write_text('func main() (\n    print("resource task")\n)\n', encoding="utf-8")
    paths.append(source)

    env = os.environ.copy()
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = sys.executable
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-resource-profile-"))
    (profile / "ui.ini").write_text(
        "[Workspace]\nRoot=" + ROOT.as_posix() + "\n"
        # Keep background auto-check from racing the deterministic manual
        # Check/Build/Run/Tests schedule below. Auto-check is validated
        # separately; this soak measures the explicit task lifecycle.
        "[Editor]\nAutoCheck=0\nAutoSave=0\n",
        encoding="utf-8",
    )
    env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
    process = subprocess.Popen([str(GUI), *(str(path) for path in paths)], cwd=ROOT, env=env)
    csv_path = output_dir / f"wx-resource-soak-{time.strftime('%Y%m%d-%H%M%S')}.csv"
    samples: list[dict[str, float | int]] = []
    active_task = ""
    task_cycle = 0
    completed_tasks = 0
    completed_task_names: list[str] = []
    failure_reason: str | None = None
    gui_exit_code: int | None = None
    worker_shutdown_ok = False
    worker_snapshot: list[tuple[int, float, str]] = []
    pending_workers: list[tuple[int, float, str]] = []
    sample_started: float | None = None
    sampled_duration = 0.0
    edit_cycle = 0
    task_sequence = [("Check", ID_CHECK), ("Build", ID_BUILD), ("Run", ID_RUN), ("Tests", ID_RUN_TESTS)]

    def task_finished(status: str) -> bool:
        return status == "Finished" or status.startswith("Exit code ")

    def task_succeeded(status: str) -> bool:
        return status == "Finished" or status == "Exit code 0"

    try:
        time.sleep(4)
        frame = main_frame(process.pid)
        editors = editor_hwnds(frame)
        if len(editors) != len(paths):
            raise RuntimeError(
                f"expected {len(paths)} open Scintilla documents, found {len(editors)}"
            )
        document_lengths = [
            remote_scintilla_message(process.pid, hwnd, SCI_GETLENGTH) for hwnd in editors
        ]
        for path, actual_length in zip(paths, document_lengths):
            source_bytes = path.read_bytes()
            expected_length = len(source_bytes)
            normalized_eol_length = len(source_bytes.replace(b"\r\n", b"\n"))
            if actual_length not in (expected_length, normalized_eol_length):
                raise RuntimeError(
                    f"open document length mismatch for {path.name}: "
                    f"expected {expected_length} (or EOL-normalized {normalized_eol_length}), "
                    f"got {actual_length}"
                )
        aggregate_length = sum(document_lengths)
        expected_aggregate = sum(path.stat().st_size for path in paths)
        if aggregate_length != expected_aggregate:
            raise RuntimeError(
                f"open document byte count mismatch: expected {expected_aggregate}, got {aggregate_length}"
            )
        if aggregate_length > 64 * 1024 * 1024:
            raise RuntimeError(f"open document aggregate exceeds 64 MiB: {aggregate_length}")
        editor = editors[0]
        print(
            f"Open document count: {len(editors)}; aggregate editor bytes: {aggregate_length}"
        )

        # Exercise actual Scintilla editing and its undo/redo history.
        block = b"// resource-edit-undo-redo\n" * 2048
        base_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
        for _ in range(8):
            remote_scintilla_message(process.pid, editor, SCI_APPENDTEXT, block)
        appended_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
        if appended_length != base_length + len(block) * 8:
            raise RuntimeError(
                f"Scintilla append verification failed: {base_length} -> {appended_length}"
            )
        for _ in range(8):
            remote_scintilla_message(process.pid, editor, SCI_UNDO)
        undone_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
        if undone_length != base_length:
            raise RuntimeError(
                f"Scintilla undo verification failed: expected {base_length}, got {undone_length}"
            )
        for _ in range(8):
            remote_scintilla_message(process.pid, editor, SCI_REDO)
        redone_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
        if redone_length != appended_length:
            raise RuntimeError(
                f"Scintilla redo verification failed: expected {appended_length}, got {redone_length}"
            )
        for _ in range(8):
            remote_scintilla_message(process.pid, editor, SCI_UNDO)
        restored_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
        if restored_length != base_length:
            raise RuntimeError(
                f"Scintilla final undo verification failed: expected {base_length}, got {restored_length}"
            )
        remote_scintilla_message(process.pid, editor, SCI_SETSAVEPOINT)

        sample_started = time.monotonic()
        samples.append(process_tree_sample(process.pid, frame, "", 0, ping_timeout_ms))

        # Start the project's own test task while sampling the worker and all
        # descendants (Python/toolchain/compiler processes included).
        status_before, status_after, dispatched = dispatch_task_command(
            process.pid, frame, ID_RUN_TESTS
        )
        if not dispatched:
            failure_reason = "Run Tests command did not change the IDE task status"
            print(f"DISPATCH_FAIL: {failure_reason} ({status_before!r})", flush=True)
        else:
            print(f"Run Tests task dispatch: {status_before!r} -> {status_after!r}")
            active_task = "Tests"
            task_cycle = 1
        deadline = sample_started + args.duration
        next_task_at = time.monotonic() + args.cycle_interval
        next_edit_at = time.monotonic() + 20
        edit_cycle = 0
        while failure_reason is None and time.monotonic() < deadline and process.poll() is None:
            current_status = status_text(process.pid, frame)
            if active_task and task_finished(current_status):
                if not task_succeeded(current_status):
                    failure_reason = f"{active_task} task completed unsuccessfully: {current_status}"
                    print(f"TASK_FAIL: {failure_reason}", flush=True)
                    break
                completed_tasks += 1
                completed_task_names.append(active_task)
                active_task = ""

            now = time.monotonic()
            if not active_task and now >= next_task_at:
                task_name, command_id = task_sequence[(task_cycle - 1) % len(task_sequence)]
                before, after, dispatched = dispatch_task_command(
                    process.pid, frame, command_id
                )
                if not dispatched:
                    failure_reason = (
                        f"{task_name} command {command_id} did not change task status "
                        f"(status remained {before!r})"
                    )
                    print(f"DISPATCH_FAIL: {failure_reason}", flush=True)
                    break
                task_cycle += 1
                active_task = task_name
                next_task_at = time.monotonic() + args.cycle_interval
                print(f"soak task {task_cycle}: command_id={command_id} task={task_name} "
                      f"status={before!r}->{after!r}")

            if now >= next_edit_at:
                edit_cycle += 1
                edit_block = f"// soak edit/undo/redo cycle {edit_cycle}\n".encode("ascii")
                before_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
                remote_scintilla_message(process.pid, editor, SCI_APPENDTEXT, edit_block)
                appended_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
                remote_scintilla_message(process.pid, editor, SCI_UNDO)
                undone_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
                remote_scintilla_message(process.pid, editor, SCI_REDO)
                redone_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
                remote_scintilla_message(process.pid, editor, SCI_UNDO)
                restored_length = remote_scintilla_message(process.pid, editor, SCI_GETLENGTH)
                if (
                    appended_length != before_length + len(edit_block)
                    or undone_length != before_length
                    or redone_length != appended_length
                    or restored_length != before_length
                ):
                    failure_reason = f"Scintilla edit/undo/redo verification failed in cycle {edit_cycle}"
                    print(f"EDIT_FAIL: {failure_reason}", flush=True)
                    break
                remote_scintilla_message(process.pid, editor, SCI_SETSAVEPOINT)
                next_edit_at = now + 20

            samples.append(process_tree_sample(process.pid, frame, active_task, task_cycle, ping_timeout_ms))
            time.sleep(args.interval)

        sampled_duration = max(0.0, time.monotonic() - sample_started)
        if failure_reason is None and process.poll() is not None and sampled_duration < args.duration:
            failure_reason = f"GUI process exited after {sampled_duration:.1f}s before requested {args.duration}s"
        if failure_reason is None and sampled_duration + args.interval < args.duration:
            failure_reason = (
                f"sampling ended after {sampled_duration:.1f}s; requested {args.duration}s"
            )

        # Let the final compiler/test descendant finish before closing tabs.
        settle_deadline = time.monotonic() + 120
        while failure_reason is None and active_task and process.poll() is None and time.monotonic() < settle_deadline:
            current_status = status_text(process.pid, frame)
            if task_finished(current_status):
                if not task_succeeded(current_status):
                    failure_reason = f"{active_task} task completed unsuccessfully during settle: {current_status}"
                    print(f"TASK_FAIL: {failure_reason}", flush=True)
                    break
                completed_tasks += 1
                completed_task_names.append(active_task)
                active_task = ""
                break
            samples.append(process_tree_sample(process.pid, frame, active_task, task_cycle, ping_timeout_ms))
            time.sleep(args.interval)

        if failure_reason is None and active_task:
            failure_reason = f"{active_task} task did not finish within the 120 s post-soak settle window"
            print(f"TASK_FAIL: {failure_reason}", flush=True)

        if failure_reason is None:
            required_tasks = {"Check", "Build", "Run", "Tests"}
            missing_tasks = sorted(required_tasks.difference(completed_task_names))
            if missing_tasks:
                failure_reason = "tasks did not all complete successfully: " + ", ".join(missing_tasks)
                print(f"TASK_COVERAGE_FAIL: {failure_reason}", flush=True)

        # Close all source tabs and record the post-close footprint for 20 s.
        if failure_reason is None and process.poll() is None:
            win32gui.PostMessage(frame, win32con.WM_COMMAND, ID_CLOSE_ALL_TABS, 0)
            time.sleep(1)
            remaining_editors = editor_hwnds(frame)
            if len(remaining_editors) != 1 or remote_scintilla_message(
                process.pid, remaining_editors[0], SCI_GETLENGTH
            ) != 0:
                failure_reason = (
                    "Close All Tabs did not leave exactly one empty new document; "
                    f"remaining Scintilla controls: {len(remaining_editors)}"
                )
                print(f"CLOSE_FAIL: {failure_reason}", flush=True)
            for _ in range(20):
                if process.poll() is not None:
                    break
                samples.append(process_tree_sample(process.pid, frame, "post-close", task_cycle, ping_timeout_ms))
                time.sleep(args.interval)

        # Verify a normal GUI shutdown and that the worker exits with it. The
        # force-kill in finally is emergency cleanup only, never a PASS path.
        if failure_reason is None and process.poll() is None:
            try:
                gui_process = psutil.Process(process.pid)
                for child in gui_process.children(recursive=True):
                    try:
                        if child.name().lower() == WORKER.name.lower():
                            worker_snapshot.append((child.pid, child.create_time(), child.name()))
                    except psutil.Error:
                        continue
            except psutil.Error as exc:
                failure_reason = f"could not inspect worker before GUI shutdown: {exc}"
            if not worker_snapshot:
                if failure_reason is None:
                    failure_reason = "worker process was not present before graceful GUI shutdown"
            elif failure_reason is None:
                try:
                    win32gui.PostMessage(frame, win32con.WM_CLOSE, 0, 0)
                    gui_exit_code = process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    failure_reason = "GUI did not exit within 20 s after WM_CLOSE"
                except Exception as exc:
                    failure_reason = f"could not request or await graceful GUI shutdown: {exc}"
                if failure_reason is None and gui_exit_code != 0:
                    failure_reason = f"GUI exited with non-zero code {gui_exit_code}"

                if failure_reason is None:
                    worker_deadline = time.monotonic() + 15
                    pending_workers = worker_snapshot
                    while pending_workers and time.monotonic() < worker_deadline:
                        still_alive: list[tuple[int, float, str]] = []
                        for worker_pid, worker_created, worker_name in pending_workers:
                            try:
                                candidate = psutil.Process(worker_pid)
                                if (abs(candidate.create_time() - worker_created) < 0.01
                                        and candidate.is_running()
                                        and candidate.status() != psutil.STATUS_ZOMBIE):
                                    still_alive.append((worker_pid, worker_created, worker_name))
                            except psutil.NoSuchProcess:
                                continue
                            except psutil.Error:
                                still_alive.append((worker_pid, worker_created, worker_name))
                        pending_workers = still_alive
                        if pending_workers:
                            time.sleep(0.1)
                    if pending_workers:
                        failure_reason = "worker did not exit after GUI shutdown: " + ", ".join(
                            f"{name} (pid {pid})" for pid, _, name in pending_workers
                        )
                    else:
                        worker_shutdown_ok = True
        elif process.poll() is not None and failure_reason is None:
            gui_exit_code = process.returncode
            failure_reason = f"GUI exited before graceful close was requested (code {gui_exit_code})"
    except Exception as exc:
        failure_reason = f"unhandled {type(exc).__name__}: {exc}"
        print(f"UNHANDLED_EXCEPTION: {failure_reason}", flush=True)
        traceback.print_exc(file=sys.stdout)
        if sample_started is not None:
            sampled_duration = max(0.0, time.monotonic() - sample_started)
    finally:
        if process.poll() is None:
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        for worker_pid, worker_created, _ in pending_workers:
            try:
                candidate = psutil.Process(worker_pid)
                if abs(candidate.create_time() - worker_created) < 0.01:
                    subprocess.run(
                        ["taskkill", "/PID", str(worker_pid), "/T", "/F"],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        check=False,
                    )
            except psutil.Error:
                pass
        shutil.rmtree(profile, ignore_errors=True)

    if not samples:
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow([
                "timestamp", "process_count", "gui_working_set_mib", "gui_private_mib",
                "tree_working_set_mib", "tree_private_mib", "tree_cpu_seconds",
                "tree_handle_count", "tree_gdi_objects", "tree_user_objects",
                "ui_ping_ms", "active_task", "task_cycle", "process_names",
                "process_commands",
            ])
        print(f"Samples: 0; CSV: {csv_path}")
        print(f"ACCEPTANCE=FAIL ({failure_reason or 'resource sampler captured no samples'})")
        return 1
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(samples[0]))
        writer.writeheader()
        writer.writerows(samples)

    peak_ws = max(float(row["tree_working_set_mib"]) for row in samples)
    peak_private = max(float(row["tree_private_mib"]) for row in samples)
    peak_handles = max(int(row["tree_handle_count"]) for row in samples)
    peak_gdi = max(int(row["tree_gdi_objects"]) for row in samples)
    peak_user = max(int(row["tree_user_objects"]) for row in samples)
    ping_values = sorted(float(row["ui_ping_ms"]) for row in samples)
    percentile = lambda fraction: ping_values[min(
        len(ping_values) - 1, int((len(ping_values) - 1) * fraction)
    )]
    p95_ping = percentile(0.95)
    p99_ping = percentile(0.99)
    max_ping = ping_values[-1]
    last = samples[-1]
    print(f"GUI SHA-256: {sha256(GUI)}")
    print(f"Worker SHA-256: {sha256(WORKER)}")
    print(f"Samples: {len(samples)}; CSV: {csv_path}")
    print(f"Sampled duration: {sampled_duration:.1f}s; requested: {args.duration}s")
    print(f"Completed task cycles: {completed_tasks} ({', '.join(completed_task_names)}); "
          f"editor edit/undo/redo cycles: {edit_cycle}")
    print(f"GUI graceful exit code: {gui_exit_code}")
    print(f"Worker graceful shutdown: {'PASS' if worker_shutdown_ok else 'FAIL'}")
    print(f"Peak process-tree working set: {peak_ws:.1f} MiB")
    print(f"Peak process-tree private bytes: {peak_private:.1f} MiB")
    print(f"Peak process-tree handles: {peak_handles}")
    print(f"Peak process-tree GDI/USER objects: {peak_gdi}/{peak_user}")
    print(f"Last sampled process count: {last['process_count']}")
    print(f"Last sampled process-tree working set: {last['tree_working_set_mib']:.1f} MiB")
    print(f"Last sampled process-tree private bytes: {last['tree_private_mib']:.1f} MiB")
    print(f"UI ping p95/p99/max: {p95_ping:.1f}/{p99_ping:.1f}/{max_ping:.1f} ms")
    duration_ok = sampled_duration + args.interval >= args.duration
    if duration_ok:
        print("DURATION=PASS")
    else:
        print(f"DURATION=FAIL ({failure_reason or 'requested interval not completed'})")
    if failure_reason:
        print(f"ACCEPTANCE=FAIL ({failure_reason})")
        return 1
    if gui_exit_code != 0 or not worker_shutdown_ok:
        print("NORMAL_TERMINATION=FAIL")
        return 1
    print("NORMAL_TERMINATION=PASS")
    if not duration_ok:
        return 1
    if peak_ws > 512 or peak_private > 512:
        print("RESOURCE_LIMIT=FAIL (>512 MiB)")
        return 1
    print("RESOURCE_LIMIT=PASS (sampled peaks <=512 MiB)")
    if max_ping > args.max_ui_ping_ms:
        print(f"UI_RESPONSIVENESS=FAIL (max ping > {args.max_ui_ping_ms:.1f} ms)")
        return 1
    print(f"UI_RESPONSIVENESS=PASS (max ping <= {args.max_ui_ping_ms:.1f} ms)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
