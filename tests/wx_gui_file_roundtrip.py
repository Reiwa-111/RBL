#!/usr/bin/env python3
"""Real Windows wx GUI byte-roundtrip smoke test (requires pywin32).

Launches the actual wx frontend with each fixture, invokes its File > Save
command through the frame's native event handler, and compares the resulting
file bytes. Run from the repository root after building the Windows wx target:

    python tests/wx_gui_file_roundtrip.py

This is intentionally separate from the headless test suite. It must run in an
interactive Windows desktop session; it does not fake SaveDocument or call the
file serializer directly.
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import time
import ctypes

if os.name != "nt":
    raise SystemExit("This GUI acceptance test requires Windows.")

try:
    import win32con
    import win32gui
    import win32process
except ImportError as exc:  # pragma: no cover - depends on Windows desktop setup
    raise SystemExit("Install pywin32 to run this GUI acceptance test.") from exc


ROOT = pathlib.Path(__file__).resolve().parents[1]
GUI = pathlib.Path(os.environ.get(
    "RBL_STUDIO_GUI", ROOT / "build" / "wx-ui-win" / "rbl-studio-wx.exe"
))
WORKER = pathlib.Path(os.environ.get(
    "RBL_STUDIO_WORKER", ROOT / "build" / "wx-ui-win" / "rbl-studio-worker.exe"
))
SAVE_COMMAND_ID = 6002  # ID_Save = wxID_HIGHEST + 3 in main_frame.cpp
TEXT = 'func main() {\n    print("Космос 🚀")\n}\n'


class _MouseInput(ctypes.Structure):
    _fields_ = [
        ("dx", ctypes.c_long), ("dy", ctypes.c_long),
        ("mouse_data", ctypes.c_ulong), ("flags", ctypes.c_ulong),
        ("time", ctypes.c_ulong), ("extra_info", ctypes.c_size_t),
    ]


class _KeyboardInput(ctypes.Structure):
    _fields_ = [
        ("virtual_key", ctypes.c_ushort), ("scan", ctypes.c_ushort),
        ("flags", ctypes.c_ulong), ("time", ctypes.c_ulong),
        ("extra_info", ctypes.c_size_t),
    ]


class _HardwareInput(ctypes.Structure):
    _fields_ = [
        ("message", ctypes.c_ulong), ("param_low", ctypes.c_ushort),
        ("param_high", ctypes.c_ushort),
    ]


class _InputUnion(ctypes.Union):
    _fields_ = [("mouse", _MouseInput), ("keyboard", _KeyboardInput), ("hardware", _HardwareInput)]


class _Input(ctypes.Structure):
    _fields_ = [("type", ctypes.c_ulong), ("value", _InputUnion)]


def send_keyboard_input(events: list[tuple[int, int, int]]) -> None:
    """Send actual keyboard input to the foreground native dialog."""
    inputs = (_Input * len(events))()
    for index, (virtual_key, scan, flags) in enumerate(events):
        inputs[index].type = 1  # INPUT_KEYBOARD
        inputs[index].value.keyboard = _KeyboardInput(virtual_key, scan, flags, 0, 0)
    user32 = ctypes.windll.user32
    user32.SendInput.argtypes = [ctypes.c_uint, ctypes.POINTER(_Input), ctypes.c_int]
    user32.SendInput.restype = ctypes.c_uint
    sent = user32.SendInput(len(inputs), inputs, ctypes.sizeof(_Input))
    if sent != len(inputs):
        raise OSError(ctypes.get_last_error(), f"SendInput accepted {sent}/{len(inputs)} keyboard events")


def click_control(hwnd: int) -> None:
    """Click a native control through real mouse input, then restore the cursor."""
    rectangle = win32gui.GetWindowRect(hwnd)
    old_position = win32gui.GetCursorPos()
    user32 = ctypes.windll.user32
    user32.SetCursorPos((rectangle[0] + rectangle[2]) // 2,
                        (rectangle[1] + rectangle[3]) // 2)
    inputs = (_Input * 2)()
    inputs[0].type = 0  # INPUT_MOUSE
    inputs[0].value.mouse = _MouseInput(0, 0, 0, 0x0002, 0, 0)  # LEFTDOWN
    inputs[1].type = 0
    inputs[1].value.mouse = _MouseInput(0, 0, 0, 0x0004, 0, 0)  # LEFTUP
    user32.SendInput.argtypes = [ctypes.c_uint, ctypes.POINTER(_Input), ctypes.c_int]
    user32.SendInput.restype = ctypes.c_uint
    sent = user32.SendInput(2, inputs, ctypes.sizeof(_Input))
    time.sleep(0.15)
    user32.SetCursorPos(*old_position)
    if sent != 2:
        raise OSError(ctypes.get_last_error(), f"SendInput accepted {sent}/2 mouse events")


def focus_native_dialog(dialog: int, control: int) -> None:
    """Bring a native dialog forward despite the test runner's foreground lock."""
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    current_thread = kernel32.GetCurrentThreadId()
    dialog_thread = win32process.GetWindowThreadProcessId(dialog)[0]
    foreground = user32.GetForegroundWindow()
    foreground_thread = win32process.GetWindowThreadProcessId(foreground)[0] if foreground else 0
    attached: list[int] = []
    for thread_id in {dialog_thread, foreground_thread}:
        if thread_id and thread_id != current_thread and user32.AttachThreadInput(current_thread, thread_id, True):
            attached.append(thread_id)
    try:
        win32gui.BringWindowToTop(dialog)
        if not user32.SetForegroundWindow(dialog):
            raise RuntimeError("Windows refused to foreground the native Save As dialog")
        user32.SetActiveWindow(dialog)
        user32.SetFocus(control)
        if user32.GetForegroundWindow() != dialog or user32.GetFocus() != control:
            raise RuntimeError("native dialog focus could not be verified")
    finally:
        for thread_id in reversed(attached):
            user32.AttachThreadInput(current_thread, thread_id, False)


def type_text(text: str) -> None:
    key_up = 0x0002
    unicode_key = 0x0004
    events = [
        (win32con.VK_CONTROL, 0, 0), (ord("A"), 0, 0),
        (ord("A"), 0, key_up), (win32con.VK_CONTROL, 0, key_up),
    ]
    encoded = text.encode("utf-16le")
    for offset in range(0, len(encoded), 2):
        code_unit = int.from_bytes(encoded[offset:offset + 2], "little")
        events.extend([(0, code_unit, unicode_key), (0, code_unit, unicode_key | key_up)])
    send_keyboard_input(events)


def terminate_tree(pid: int) -> None:
    subprocess.run(
        ["taskkill", "/PID", str(pid), "/T", "/F"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def main_frame(pid: int, timeout: float = 10.0) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames: list[int] = []
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
    raise RuntimeError("wx main frame did not appear within 10 seconds")


def set_editor_text(pid: int, editor: int, data: bytes) -> None:
    """Set text through Scintilla using memory allocated in the GUI process."""
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
        raise RuntimeError("could not open GUI process for editor test")
    remote = None
    try:
        buffer = ctypes.create_string_buffer(data)
        remote = kernel32.VirtualAllocEx(process, None, len(data), 0x3000, 0x04)
        if not remote:
            raise RuntimeError("could not allocate remote editor buffer")
        written = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(
            process, remote, buffer, len(data), ctypes.byref(written)
        ) or written.value != len(data):
            raise RuntimeError("could not copy editor fixture into GUI process")
        user32.SendMessageA(editor, 2181, 0, remote)  # SCI_SETTEXT
        # SCI_SETTEXT resets Scintilla's modification flag. The test helper is
        # simulating a user edit, so explicitly mark the document dirty before
        # exercising Save/Save As and cancel/retry behavior.
        user32.SendMessageA(editor, 2158, 1, 0)  # SCI_SETMODIFY
        if not user32.SendMessageA(editor, 2159, 0, 0):  # SCI_GETMODIFY
            raise RuntimeError("Scintilla did not retain the simulated dirty state")
    finally:
        if remote:
            kernel32.VirtualFreeEx(process, remote, 0, 0x8000)
        kernel32.CloseHandle(process)


def editor_hwnd(frame: int) -> int:
    editors: list[int] = []
    win32gui.EnumChildWindows(
        frame,
        lambda hwnd, _: editors.append(hwnd)
        if win32gui.GetClassName(hwnd) == "Scintilla"
        else None,
        None,
    )
    if not editors:
        raise RuntimeError("Scintilla editor HWND not found")
    return editors[0]


def wait_editor_length(frame: int, expected: int, timeout: float = 12.0) -> int:
    """Wait for the current Scintilla child to contain the deferred startup file.

    Opening the first command-line file can replace the constructor's initial
    empty editor. Reacquire the child HWND while waiting instead of polling a
    handle that may already have been destroyed.
    """
    user32 = ctypes.windll.user32
    user32.SendMessageA.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t
    ]
    user32.SendMessageA.restype = ctypes.c_ssize_t
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        editors: list[int] = []
        win32gui.EnumChildWindows(
            frame,
            lambda hwnd, _: editors.append(hwnd)
            if win32gui.GetClassName(hwnd) == "Scintilla"
            else None,
            None,
        )
        for editor in editors:
            if user32.SendMessageA(editor, 2183, 0, 0) == expected:  # SCI_GETTEXTLENGTH
                return editor
        time.sleep(0.05)
    actual = [user32.SendMessageA(editor, 2183, 0, 0) for editor in editors]
    raise RuntimeError(
        f"Scintilla did not finish loading startup file: expected {expected} bytes, got {actual}"
    )


def editor_is_modified(editor: int) -> bool:
    return bool(ctypes.windll.user32.SendMessageA(editor, 2159, 0, 0))  # SCI_GETMODIFY


def wait_editor_nonempty(editor: int, timeout: float = 8.0) -> None:
    """Wait until deferred startup-file loading has populated Scintilla."""
    user32 = ctypes.windll.user32
    user32.SendMessageA.argtypes = [
        ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t
    ]
    user32.SendMessageA.restype = ctypes.c_ssize_t
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if user32.SendMessageA(editor, 2183, 0, 0) > 0:  # SCI_GETTEXTLENGTH
            return
        time.sleep(0.05)
    raise RuntimeError("Scintilla did not finish loading the startup file")


def find_dialog(pid: int, title: str) -> int:
    dialogs: list[int] = []
    win32gui.EnumWindows(
        lambda hwnd, _: dialogs.append(hwnd)
        if win32gui.IsWindowVisible(hwnd)
        and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
        and win32gui.GetWindowText(hwnd) == title
        else None,
        None,
    )
    return dialogs[0] if dialogs else 0


def answer_dialog(pid: int, title: str, command_id: int) -> None:
    dialog = 0
    for _ in range(50):
        time.sleep(0.1)
        dialog = find_dialog(pid, title)
        if dialog:
            break
    if not dialog:
        visible_windows: list[tuple[str, str]] = []
        win32gui.EnumWindows(
            lambda hwnd, _: visible_windows.append(
                (win32gui.GetWindowText(hwnd), win32gui.GetClassName(hwnd))
            )
            if win32gui.IsWindowVisible(hwnd)
            and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
            else None,
            None,
        )
        raise RuntimeError(
            f"expected '{title}' confirmation did not appear; "
            f"visible windows for pid {pid}: {visible_windows}"
        )
    buttons: list[int] = []
    button_details: list[tuple[int, int, str]] = []
    def collect_button(hwnd: int, _: object) -> None:
        if win32gui.GetClassName(hwnd) != "Button":
            return
        control_id = win32gui.GetDlgCtrlID(hwnd)
        caption = win32gui.GetWindowText(hwnd)
        button_details.append((hwnd, control_id, caption))
        if control_id == command_id:
            buttons.append(hwnd)

    win32gui.EnumChildWindows(
        dialog, collect_button, None
    )
    # wxMessageBox button IDs are not consistently the Win32 IDYES/IDNO values
    # across wxMSW implementations. Prefer the visible label for Yes/No so a
    # requested decline can never silently click the first (Yes) button.
    normalize_caption = lambda caption: caption.strip().lower().replace("&", "")
    if command_id in (2, 6, 7):
        wanted = {
            2: ("cancel", "отмена"),
            6: ("yes", "да"),
            7: ("no", "нет"),
        }[command_id]
        buttons = [item[0] for item in button_details if normalize_caption(item[2]) in wanted]
    if not buttons:
        buttons = [item[0] for item in button_details if item[1] == command_id]
    if not buttons and command_id not in (2, 6, 7):
        buttons = [item[0] for item in button_details
                   if normalize_caption(item[2]) in ("ok", "ок")]
    if not buttons and button_details and command_id not in (2, 6, 7):
        buttons = [button_details[0][0]]
    if buttons:
        selected = next((item for item in button_details if item[0] == buttons[0]), None)
        if selected:
            print(f"{title}: selected button {selected[2]!r} (id={selected[1]})", flush=True)
        focus_native_dialog(dialog, buttons[0])
        click_control(buttons[0])
    elif command_id not in (2, 6, 7):
        win32gui.PostMessage(dialog, win32con.WM_COMMAND, command_id, 0)
    else:
        raise RuntimeError(f"could not identify the requested Yes/No/Cancel button for '{title}': {button_details}")
    if title == "Save failed":
        time.sleep(0.15)
        if win32gui.IsWindowVisible(dialog):
            win32gui.PostMessage(dialog, win32con.WM_CLOSE, 0, 0)
    for _ in range(30):
        time.sleep(0.1)
        if not win32gui.IsWindowVisible(dialog):
            return
    raise RuntimeError(f"'{title}' confirmation did not close; buttons={button_details}")


def wait_window_closed(hwnd: int) -> None:
    for _ in range(50):
        time.sleep(0.1)
        if not win32gui.IsWindow(hwnd):
            return
    raise RuntimeError("expected wx frame to close")


def save_as_to_path(
    pid: int, frame: int, destination: pathlib.Path, *, allow_overwrite: bool = False
) -> None:
    """Drive the real native Save As dialog through verified input events."""
    existed_before = destination.exists()
    if existed_before and not allow_overwrite:
        raise RuntimeError(f"refusing to automate Save As over an existing target: {destination}")
    win32gui.PostMessage(frame, win32con.WM_COMMAND, 6003, 0)  # ID_SaveAs
    dialog = 0
    for _ in range(80):
        time.sleep(0.1)
        dialogs: list[int] = []
        win32gui.EnumWindows(
            lambda hwnd, _: dialogs.append(hwnd)
            if win32gui.IsWindowVisible(hwnd)
            and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
            and win32gui.GetClassName(hwnd) == "#32770"
            else None,
            None,
        )
        dialog = dialogs[0] if dialogs else 0
        if dialog:
            break
    if not dialog:
        raise RuntimeError("native Save As dialog did not appear")

    # The modern Windows common dialog is still a #32770 top-level window,
    # but its filename control uses ID 0x3E9. Setting its text with WM_SETTEXT
    # does not update IFileDialog's internal selection; send real Unicode
    # keyboard input after verifying foreground and focus instead.
    filename_controls: list[int] = []
    win32gui.EnumChildWindows(
        dialog,
        lambda hwnd, _: filename_controls.append(hwnd)
        if win32gui.GetDlgCtrlID(hwnd) == 0x3E9
        and win32gui.GetClassName(hwnd) == "Edit"
        else None,
        None,
    )
    filename_edit = filename_controls[0] if filename_controls else 0
    if not filename_edit:
        controls: list[tuple[int, str, int, str]] = []
        win32gui.EnumChildWindows(
            dialog,
            lambda hwnd, _: controls.append((hwnd, win32gui.GetClassName(hwnd),
                                              win32gui.GetDlgCtrlID(hwnd),
                                              win32gui.GetWindowText(hwnd))),
            None,
        )
        raise RuntimeError(f"native Save As filename control edt1 was not found: {controls}")
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    current_thread = kernel32.GetCurrentThreadId()
    dialog_thread = win32process.GetWindowThreadProcessId(dialog)[0]
    foreground = user32.GetForegroundWindow()
    foreground_thread = win32process.GetWindowThreadProcessId(foreground)[0] if foreground else 0
    attached: list[int] = []
    for thread_id in {dialog_thread, foreground_thread}:
        if thread_id and thread_id != current_thread and user32.AttachThreadInput(current_thread, thread_id, True):
            attached.append(thread_id)
    try:
        win32gui.BringWindowToTop(dialog)
        if not user32.SetForegroundWindow(dialog):
            raise RuntimeError("Windows refused to foreground the native Save As dialog")
        user32.SetActiveWindow(dialog)
        user32.SetFocus(filename_edit)
        if user32.GetForegroundWindow() != dialog or user32.GetFocus() != filename_edit:
            raise RuntimeError("native Save As filename field did not receive keyboard focus")
        type_text(str(destination))
        save_buttons: list[int] = []
        win32gui.EnumChildWindows(
            dialog,
            lambda hwnd, _: save_buttons.append(hwnd)
            if win32gui.GetDlgCtrlID(hwnd) == 1
            and win32gui.GetClassName(hwnd) == "Button"
            else None,
            None,
        )
        if not save_buttons:
            raise RuntimeError("native Save As Save button was not found")
        # Do not steal focus from the filename edit before the physical click:
        # the shell dialog commits its IFileDialog filename state on focus loss.
        click_control(save_buttons[0])
    finally:
        for thread_id in reversed(attached):
            user32.AttachThreadInput(current_thread, thread_id, False)

    # Windows may show a separate "Confirm Save As" prompt (localized by the
    # host OS) after the Save button is clicked. This is distinct from the
    # IDE's later external-modification prompt. Accept it only for a target
    # that was absent at entry or when the test explicitly permits overwrite.
    confirmation = 0
    for _ in range(50):
        candidates: list[int] = []
        win32gui.EnumWindows(
            lambda hwnd, _: candidates.append(hwnd)
            if win32gui.IsWindowVisible(hwnd)
            and win32process.GetWindowThreadProcessId(hwnd)[1] == pid
            and win32gui.GetClassName(hwnd) == "#32770"
            and hwnd != dialog
            else None,
            None,
        )
        for candidate in candidates:
            title = win32gui.GetWindowText(candidate).strip().lower()
            if ("confirm save as" in title or
                    ("подтверд" in title and "сохран" in title)):
                confirmation = candidate
                break
        if confirmation or not win32gui.IsWindowVisible(dialog):
            break
        time.sleep(0.1)
    if confirmation:
        if existed_before and not allow_overwrite:
            raise RuntimeError(f"unexpected overwrite confirmation for existing target: {destination}")
        yes_buttons: list[int] = []
        confirmation_controls: list[tuple[int, str, str, int]] = []
        def collect_confirmation_control(hwnd: int, _: object) -> None:
            caption = win32gui.GetWindowText(hwnd)
            confirmation_controls.append((hwnd, win32gui.GetClassName(hwnd), caption,
                                          win32gui.GetDlgCtrlID(hwnd)))
            if caption.strip().lower().replace("&", "") in ("yes", "да"):
                yes_buttons.insert(0, hwnd)
            elif win32gui.GetClassName(hwnd) == "Button" and win32gui.IsWindowEnabled(hwnd):
                yes_buttons.append(hwnd)
        win32gui.EnumChildWindows(confirmation, collect_confirmation_control, None)
        if not yes_buttons:
            raise RuntimeError(f"Save As confirmation has no actionable button: {confirmation_controls}")
        focus_native_dialog(confirmation, yes_buttons[0])
        click_control(yes_buttons[0])
        for _ in range(30):
            time.sleep(0.1)
            if not win32gui.IsWindowVisible(confirmation):
                break

    for _ in range(80):
        time.sleep(0.1)
        if not win32gui.IsWindowVisible(dialog):
            break
    else:
        windows: list[tuple[str, str, int]] = []
        win32gui.EnumWindows(
            lambda hwnd, _: windows.append((win32gui.GetWindowText(hwnd), win32gui.GetClassName(hwnd),
                win32process.GetWindowThreadProcessId(hwnd)[1]))
            if win32gui.IsWindowVisible(hwnd) else None,
            None,
        )
        controls: list[tuple[int, str, int, str, bool, bool]] = []
        win32gui.EnumChildWindows(
            dialog,
            lambda hwnd, _: controls.append((
                hwnd, win32gui.GetClassName(hwnd), win32gui.GetDlgCtrlID(hwnd),
                win32gui.GetWindowText(hwnd), bool(win32gui.IsWindowVisible(hwnd)),
                bool(win32gui.IsWindowEnabled(hwnd)))),
            None,
        )
        raise RuntimeError(
            f"native Save As dialog did not close after submitting the path; "
            f"focus={user32.GetFocus()}, controls={controls}, visible windows={windows}"
        )
    for _ in range(50):
        time.sleep(0.1)
        if destination.is_file():
            return
    raise RuntimeError("Save As did not create the requested destination")


def main() -> int:
    if not GUI.is_file() or not WORKER.is_file():
        raise SystemExit(f"Build the wx GUI and worker first: {GUI}")

    env = os.environ.copy()
    env["RBL_STUDIO_WORKER"] = str(WORKER)
    env["RBL_PYTHON"] = sys.executable
    fixture_dir = pathlib.Path(os.environ.get(
        "RBL_STUDIO_GUI_FIXTURE_DIR",
        str(ROOT / "build" / "wx-ui-win" / "gui-acceptance"),
    ))
    fixture_dir.mkdir(parents=True, exist_ok=True)

    cases = {
        "utf8-lf": TEXT.encode("utf-8"),
        "utf8-crlf": TEXT.replace("\n", "\r\n").encode("utf-8"),
        "utf8-cr": TEXT.replace("\n", "\r").encode("utf-8"),
        "utf8-bom-crlf": b"\xef\xbb\xbf" + TEXT.replace("\n", "\r\n").encode("utf-8"),
        "utf16le-crlf": b"\xff\xfe" + TEXT.replace("\n", "\r\n").encode("utf-16le"),
        "utf16be-lf": b"\xfe\xff" + TEXT.encode("utf-16be"),
        "mixed-eol": 'func main() {\r\n    print("Космос 🚀")\n}\r'.encode("utf-8"),
    }

    failures: list[str] = []
    for name, expected in cases.items():
        path = fixture_dir / f"{name}-тест.rbl"
        path.write_bytes(expected)
        process = subprocess.Popen([str(GUI), str(path)], cwd=ROOT, env=env)
        try:
            time.sleep(1.5)
            frame = main_frame(process.pid)
            wait_editor_nonempty(editor_hwnd(frame))
            win32gui.PostMessage(
                frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0
            )
            time.sleep(0.6)
            actual = path.read_bytes()
            if actual != expected:
                failures.append(name)
                print(
                    f"{name}: FAIL (expected {len(expected)} bytes, "
                    f"got {len(actual)} bytes)",
                    flush=True,
                )
            else:
                print(f"{name}: PASS ({len(actual)} exact bytes)", flush=True)
        finally:
            if process.poll() is None:
                terminate_tree(process.pid)

    # Exercise the actual modified-buffer Save path through Scintilla and the
    # frame's File > Save event. Remote memory is allocated in the GUI process;
    # no foreign-process pointer is passed to Scintilla.
    edited_path = fixture_dir / "edited-save-тест.rbl"
    original = b'func main() {\n    print("before")\n}\n'
    edited = 'func main() {\n    print("after Космос 🚀")\n}\n'.encode("utf-8")
    external = b'func main() {\n    print("external writer")\n}\n'
    edited_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(edited_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        time.sleep(0.6)
        if edited_path.read_bytes() != edited:
            failures.append("edited-buffer-save")
            print("edited-buffer-save: FAIL", flush=True)
        else:
            print("edited-buffer-save: PASS (exact UTF-8 bytes)", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Atomic replacement of one name in a hard-link set intentionally splits
    # that name from the other links. The GUI must explain this before Save and
    # default to No; accepting it must leave the other name's bytes untouched.
    hardlink_path = fixture_dir / "hardlink-save-тест.rbl"
    hardlink_alias = fixture_dir / "hardlink-save-alias-тест.rbl"
    hardlink_path.write_bytes(original)
    hardlink_alias.unlink(missing_ok=True)
    os.link(hardlink_path, hardlink_alias)
    process = subprocess.Popen([str(GUI), str(hardlink_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "Hard-linked destination", 7)  # No
        if (hardlink_path.read_bytes() != original or hardlink_alias.read_bytes() != original
                or not editor_is_modified(editor)):
            failures.append("hardlink-save-cancel")
            print("hardlink-save-cancel: FAIL", flush=True)
        else:
            print("hardlink-save-cancel: PASS", flush=True)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "Hard-linked destination", 6)  # Yes
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and editor_is_modified(editor):
            time.sleep(0.05)
        if (hardlink_path.read_bytes() != edited or hardlink_alias.read_bytes() != original
                or os.path.samefile(hardlink_path, hardlink_alias) or editor_is_modified(editor)):
            failures.append("hardlink-save-confirm")
            print("hardlink-save-confirm: FAIL", flush=True)
        else:
            print("hardlink-save-confirm: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # A failed replacement must not mark the editor clean. Clearing the
    # read-only attribute and retrying must then save the same dirty buffer.
    readonly_path = fixture_dir / "readonly-save-recovery-тест.rbl"
    readonly_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(readonly_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        if not ctypes.windll.kernel32.SetFileAttributesW(str(readonly_path), 0x1):
            raise RuntimeError("could not set read-only attribute for save-failure test")
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "Save failed", 1)
        if readonly_path.read_bytes() != original or not editor_is_modified(editor):
            failures.append("readonly-save-dirty-state")
            print("readonly-save-dirty-state: FAIL", flush=True)
        else:
            print("readonly-save-dirty-state: PASS", flush=True)
        if not ctypes.windll.kernel32.SetFileAttributesW(str(readonly_path), 0x80):
            raise RuntimeError("could not clear read-only attribute for save-retry test")
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and editor_is_modified(editor):
            time.sleep(0.05)
        if readonly_path.read_bytes() != edited or editor_is_modified(editor):
            failures.append("readonly-save-retry")
            print("readonly-save-retry: FAIL", flush=True)
        else:
            print("readonly-save-retry: PASS", flush=True)
    finally:
        ctypes.windll.kernel32.SetFileAttributesW(str(readonly_path), 0x80)
        if process.poll() is None:
            terminate_tree(process.pid)

    # Save As from a new document and from an existing document. The native
    # common dialog is driven with actual keyboard input, not by calling the
    # serializer or fabricating a wxFileDialog return value.
    new_save_as_path = fixture_dir / "save-as-new-космос.rbl"
    new_save_as_path.unlink(missing_ok=True)
    process = subprocess.Popen([str(GUI)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        set_editor_text(process.pid, editor_hwnd(frame), edited)
        save_as_to_path(process.pid, frame, new_save_as_path)
        if new_save_as_path.read_bytes() != edited:
            failures.append("save-as-new-document")
            print("save-as-new-document: FAIL", flush=True)
        else:
            print("save-as-new-document: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Save As to the same canonical path must retain the external-change guard.
    # The native overwrite confirmation is not a substitute for the IDE's
    # separate external-modification decision.
    save_as_conflict_path = fixture_dir / "external-conflict-save-as-тест.rbl"
    save_as_conflict_path.write_bytes(original)
    process = subprocess.Popen(
        [str(GUI), str(save_as_conflict_path)], cwd=ROOT, env=env
    )
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        wait_editor_length(frame, len(original))
        save_as_conflict_path.write_bytes(external)
        save_as_to_path(
            process.pid, frame, save_as_conflict_path, allow_overwrite=True
        )
        answer_dialog(process.pid, "External modification", 7)  # preserve external bytes
        if save_as_conflict_path.read_bytes() != external:
            failures.append("external-conflict-save-as-decline")
            print("external-conflict-save-as-decline: FAIL", flush=True)
        else:
            print("external-conflict-save-as-decline: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    existing_save_as_source = fixture_dir / "save-as-existing-source.rbl"
    existing_save_as_target = fixture_dir / "save-as-existing-target.rbl"
    existing_save_as_source.write_bytes(original)
    existing_save_as_target.unlink(missing_ok=True)
    process = subprocess.Popen(
        [str(GUI), str(existing_save_as_source)], cwd=ROOT, env=env
    )
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        save_as_to_path(process.pid, frame, existing_save_as_target)
        if (
            existing_save_as_target.read_bytes() != edited
            or existing_save_as_source.read_bytes() != original
        ):
            failures.append("save-as-existing-document")
            print("save-as-existing-document: FAIL", flush=True)
        else:
            print("save-as-existing-document: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # A deleted open file is not silently recreated. The user must explicitly
    # choose No (keep it deleted) or Yes (recreate from the editor buffer).
    deleted_path = fixture_dir / "deleted-file-тест.rbl"
    deleted_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(deleted_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        wait_editor_length(frame, len(original))
        deleted_path.unlink()
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "File deleted", 7)  # No / keep deleted
        if deleted_path.exists():
            failures.append("deleted-file-decline")
            print("deleted-file-decline: FAIL", flush=True)
        else:
            print("deleted-file-decline: PASS", flush=True)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "File deleted", 6)  # Yes / recreate
        if not deleted_path.is_file() or deleted_path.read_bytes() != original:
            failures.append("deleted-file-recreate")
            print("deleted-file-recreate: FAIL", flush=True)
        else:
            print("deleted-file-recreate: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Verify both choices in the external-modification prompt. Declining must
    # preserve the external writer's bytes; accepting must save the editor's
    # in-memory version.
    conflict_path = fixture_dir / "external-conflict-тест.rbl"
    conflict_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(conflict_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        wait_editor_length(frame, len(original))
        conflict_path.write_bytes(external)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "External modification", 7)  # IDNO
        if conflict_path.read_bytes() != external:
            failures.append("external-conflict-decline")
            print("external-conflict-decline: FAIL", flush=True)
        else:
            print("external-conflict-decline: PASS", flush=True)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        answer_dialog(process.pid, "External modification", 6)  # IDYES
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                if conflict_path.read_bytes() == original:
                    break
            except OSError:
                pass
            time.sleep(0.05)
        if conflict_path.read_bytes() != original:
            failures.append("external-conflict-confirm")
            print("external-conflict-confirm: FAIL", flush=True)
        else:
            print("external-conflict-confirm: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Repeated external writes while each modal prompt is open must never be
    # silently overwritten. The first Yes authorizes the first observed
    # external version; a second write during that prompt triggers another
    # confirmation; a third write during the second prompt must be detected by
    # the final post-modal revalidation and left untouched.
    repeat_conflict_path = fixture_dir / "external-repeat-conflict-тест.rbl"
    external_again = b'func main() {\n    print("external writer two")\n}\n'
    external_third = b'func main() {\n    print("external writer three")\n}\n'
    repeat_conflict_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(repeat_conflict_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        repeat_conflict_path.write_bytes(external)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, SAVE_COMMAND_ID, 0)
        first_prompt = 0
        for _ in range(50):
            first_prompt = find_dialog(process.pid, "External modification")
            if first_prompt:
                break
            time.sleep(0.1)
        if not first_prompt:
            raise RuntimeError("first repeated-write conflict prompt did not appear")
        repeat_conflict_path.write_bytes(external_again)
        answer_dialog(process.pid, "External modification", 6)
        second_prompt = 0
        for _ in range(50):
            second_prompt = find_dialog(process.pid, "External modification")
            if second_prompt:
                break
            time.sleep(0.1)
        if not second_prompt:
            raise RuntimeError("second repeated-write conflict prompt did not appear")
        repeat_conflict_path.write_bytes(external_third)
        answer_dialog(process.pid, "External modification", 6)
        answer_dialog(process.pid, "Save conflict check failed", 1)
        if repeat_conflict_path.read_bytes() != external_third or not editor_is_modified(editor):
            failures.append("external-repeat-conflict")
            print("external-repeat-conflict: FAIL", flush=True)
        else:
            print("external-repeat-conflict: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Dirty tab close: Cancel must retain the tab, while No discards the edit
    # without touching the original disk contents.
    dirty_path = fixture_dir / "dirty-tab-close-тест.rbl"
    dirty_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(dirty_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, 6017, 0)  # Close Tab
        answer_dialog(process.pid, "Unsaved changes", 2)  # Cancel
        if dirty_path.read_bytes() != original or not win32gui.IsWindow(frame):
            failures.append("dirty-tab-cancel")
            print("dirty-tab-cancel: FAIL", flush=True)
        else:
            print("dirty-tab-cancel: PASS", flush=True)
        win32gui.PostMessage(frame, win32con.WM_COMMAND, 6017, 0)
        answer_dialog(process.pid, "Unsaved changes", 7)  # No / discard
        if dirty_path.read_bytes() != original or not win32gui.IsWindow(frame):
            failures.append("dirty-tab-discard")
            print("dirty-tab-discard: FAIL", flush=True)
        else:
            print("dirty-tab-discard: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    # Closing the whole frame follows the same dirty-state policy. Verify both
    # Cancel and No, then verify Yes saves before the frame exits.
    close_path = fixture_dir / "dirty-window-close-тест.rbl"
    close_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(close_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        win32gui.PostMessage(frame, win32con.WM_CLOSE, 0, 0)
        answer_dialog(process.pid, "Unsaved changes", 2)
        if close_path.read_bytes() != original or not win32gui.IsWindow(frame):
            failures.append("dirty-window-cancel")
            print("dirty-window-cancel: FAIL", flush=True)
        else:
            print("dirty-window-cancel: PASS", flush=True)
        win32gui.PostMessage(frame, win32con.WM_CLOSE, 0, 0)
        answer_dialog(process.pid, "Unsaved changes", 7)
        wait_window_closed(frame)
        if close_path.read_bytes() != original:
            failures.append("dirty-window-discard")
            print("dirty-window-discard: FAIL", flush=True)
        else:
            print("dirty-window-discard: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    close_yes_path = fixture_dir / "dirty-window-save-тест.rbl"
    close_yes_path.write_bytes(original)
    process = subprocess.Popen([str(GUI), str(close_yes_path)], cwd=ROOT, env=env)
    try:
        time.sleep(1.5)
        frame = main_frame(process.pid)
        editor = wait_editor_length(frame, len(original))
        set_editor_text(process.pid, editor, edited)
        win32gui.PostMessage(frame, win32con.WM_CLOSE, 0, 0)
        answer_dialog(process.pid, "Unsaved changes", 6)  # Yes / save
        wait_window_closed(frame)
        if close_yes_path.read_bytes() != edited:
            failures.append("dirty-window-save")
            print("dirty-window-save: FAIL", flush=True)
        else:
            print("dirty-window-save: PASS", flush=True)
    finally:
        if process.poll() is None:
            terminate_tree(process.pid)

    malformed = {
        "utf16le-unpaired-high": b"\xff\xfe\x00\xd8",
        "utf16be-unpaired-low": b"\xfe\xff\xdc\x00",
    }
    for name, original in malformed.items():
        path = fixture_dir / f"{name}-тест.rbl"
        path.write_bytes(original)
        process = subprocess.Popen([str(GUI), str(path)], cwd=ROOT, env=env)
        try:
            # The second launch can overlap worker shutdown from the preceding
            # case on slower machines; allow the wx startup dialog to settle.
            time.sleep(2.0)
            dialogs: list[int] = []
            win32gui.EnumWindows(
                lambda hwnd, _: dialogs.append(hwnd)
                if win32gui.IsWindowVisible(hwnd)
                and win32process.GetWindowThreadProcessId(hwnd)[1] == process.pid
                and win32gui.GetClassName(hwnd) == "#32770"
                else None,
                None,
            )
            if path.read_bytes() != original or not dialogs:
                failures.append(name)
                print(f"{name}: FAIL (file changed or rejection dialog missing)", flush=True)
            else:
                print(f"{name}: PASS (rejected without modifying source)", flush=True)
        finally:
            if process.poll() is None:
                terminate_tree(process.pid)

    total = len(cases) + len(malformed) + 18
    print(f"wx GUI file round-trip: {total - len(failures)}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
