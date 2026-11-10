#!/usr/bin/env python3
"""Small XTest keyboard driver for reproducible WSLg IDE resource workloads."""
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import sys
import time


class X11Keyboard:
    def __init__(self, target_pid: int) -> None:
        x11_name = ctypes.util.find_library("X11")
        xtst_name = ctypes.util.find_library("Xtst")
        if not x11_name or not xtst_name:
            raise RuntimeError("X11 and Xtst libraries are required for WSLg automation")
        self.x11 = ctypes.CDLL(x11_name)
        self.xtst = ctypes.CDLL(xtst_name)
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("cannot connect to the active X11/WSLg display")
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XQueryTree.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.x11.XFetchName.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_char_p),
        ]
        self.x11.XFree.argtypes = [ctypes.c_void_p]
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XGetWindowProperty.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_long,
            ctypes.c_long,
            ctypes.c_int,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
        ]
        self.x11.XGetWindowProperty.restype = ctypes.c_int
        self.x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XRaiseWindow.restype = ctypes.c_int
        self.x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        self.x11.XSetInputFocus.restype = ctypes.c_int
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XCloseDisplay.restype = ctypes.c_int
        self.xtst.XTestFakeKeyEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_int,
            ctypes.c_ulong,
        ]
        self.xtst.XTestFakeKeyEvent.restype = ctypes.c_int
        self.window = self._find_window("RBL Studio", target_pid)
        if not self.window:
            raise RuntimeError(f"RBL Studio window for process {target_pid} was not found")
        if not self.x11.XRaiseWindow(self.display, self.window):
            raise RuntimeError("XRaiseWindow failed")
        if not self.x11.XSetInputFocus(self.display, self.window, 2, 0):
            raise RuntimeError("XSetInputFocus failed")
        self.x11.XFlush(self.display)
        self.x11.XSync(self.display, 0)
        time.sleep(0.25)

    def _window_pid(self, window: int) -> int:
        property_atom = self.x11.XInternAtom(self.display, b"_NET_WM_PID", 0)
        actual_type = ctypes.c_ulong()
        actual_format = ctypes.c_int()
        item_count = ctypes.c_ulong()
        bytes_after = ctypes.c_ulong()
        property_data = ctypes.POINTER(ctypes.c_ubyte)()
        status = self.x11.XGetWindowProperty(
            self.display,
            window,
            property_atom,
            0,
            1,
            0,
            0,
            ctypes.byref(actual_type),
            ctypes.byref(actual_format),
            ctypes.byref(item_count),
            ctypes.byref(bytes_after),
            ctypes.byref(property_data),
        )
        try:
            if status == 0 and actual_format.value == 32 and item_count.value and property_data:
                return int(ctypes.cast(property_data, ctypes.POINTER(ctypes.c_ulong))[0])
            return 0
        finally:
            if property_data:
                self.x11.XFree(property_data)

    def _find_window(self, needle: str, target_pid: int) -> int:
        root = self.x11.XDefaultRootWindow(self.display)
        root_return = ctypes.c_ulong()
        parent_return = ctypes.c_ulong()
        children_return = ctypes.POINTER(ctypes.c_ulong)()
        count_return = ctypes.c_uint()
        ok = self.x11.XQueryTree(
            self.display,
            root,
            ctypes.byref(root_return),
            ctypes.byref(parent_return),
            ctypes.byref(children_return),
            ctypes.byref(count_return),
        )
        if not ok:
            return 0
        matches = []
        try:
            for index in range(count_return.value):
                window = children_return[index]
                title = ctypes.c_char_p()
                if self.x11.XFetchName(self.display, window, ctypes.byref(title)) and title:
                    try:
                        name = ctypes.string_at(title).decode("utf-8", "replace")
                    finally:
                        self.x11.XFree(title)
                    if needle.casefold() in name.casefold():
                        window_pid = self._window_pid(int(window))
                        if window_pid == target_pid:
                            return int(window)
                        matches.append((int(window), window_pid, name))
        finally:
            if children_return:
                self.x11.XFree(children_return)
        if len(matches) == 1 and matches[0][1] == 0:
            return matches[0][0]
        if matches:
            raise RuntimeError(f"RBL Studio windows are ambiguous for PID {target_pid}: {matches}")
        return 0

    def keycode(self, keysym: int) -> int:
        code = int(self.x11.XKeysymToKeycode(self.display, keysym))
        if not code:
            raise RuntimeError(f"X11 has no keycode for keysym 0x{keysym:x}")
        return code

    def key(self, keysym: int, pressed: bool) -> None:
        if not self.xtst.XTestFakeKeyEvent(self.display, self.keycode(keysym), int(pressed), 0):
            raise RuntimeError("XTestFakeKeyEvent failed")

    def hotkey(self, keysym: int, *, ctrl: bool = False, shift: bool = False) -> None:
        modifiers = []
        if ctrl:
            modifiers.append(0xFFE3)  # XK_Control_L
        if shift:
            modifiers.append(0xFFE1)  # XK_Shift_L
        for modifier in modifiers:
            self.key(modifier, True)
        self.key(keysym, True)
        self.key(keysym, False)
        for modifier in reversed(modifiers):
            self.key(modifier, False)
        self.x11.XFlush(self.display)

    def type_ascii(self, value: str) -> None:
        for char in value:
            if char.isupper():
                self.key(0xFFE1, True)
            self.key(ord(char), True)
            self.key(ord(char), False)
            if char.isupper():
                self.key(0xFFE1, False)
        self.x11.XFlush(self.display)

    def close(self) -> None:
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None


def run_workload(workload: str, target_pid: int, documents_file: str) -> None:
    keyboard = X11Keyboard(target_pid)
    try:
        if workload == "EditUndoRedo":
            with open(documents_file, encoding="utf-8") as stream:
                documents = [line.rstrip("\r\n") for line in stream if line.strip()]
            if len(documents) != 4:
                raise RuntimeError("EditUndoRedo requires four document paths")
            original_sizes = [os.path.getsize(path) for path in documents]
            keyboard.hotkey(0xFF09, ctrl=True)  # move from initially selected last tab to first
            time.sleep(0.15)
            for tab_index in range(4):
                keyboard.hotkey(0xFF57, ctrl=True)  # End
                for _ in range(8):
                    keyboard.type_ascii("x" * 1024)
                    time.sleep(0.08)
                keyboard.hotkey(ord("s"), ctrl=True)
                time.sleep(0.15)
                if os.path.getsize(documents[tab_index]) <= original_sizes[tab_index]:
                    raise RuntimeError(f"paste phase did not persist on {documents[tab_index]}")
                for _ in range(8):
                    keyboard.hotkey(ord("z"), ctrl=True)
                    time.sleep(0.04)
                keyboard.hotkey(ord("s"), ctrl=True)
                time.sleep(0.15)
                if os.path.getsize(documents[tab_index]) != original_sizes[tab_index]:
                    raise RuntimeError(f"undo phase did not restore {documents[tab_index]}")
                for _ in range(8):
                    keyboard.hotkey(ord("y"), ctrl=True)
                    time.sleep(0.04)
                keyboard.hotkey(ord("s"), ctrl=True)
                time.sleep(0.15)
                if os.path.getsize(documents[tab_index]) <= original_sizes[tab_index]:
                    raise RuntimeError(f"redo phase did not restore edited content in {documents[tab_index]}")
                keyboard.hotkey(0xFF09, ctrl=True)  # Tab
        elif workload == "FindReplaceUndoRedo":
            with open(documents_file, encoding="utf-8") as stream:
                documents = [line.rstrip("\r\n") for line in stream if line.strip()]
            original_size = os.path.getsize(documents[0])
            keyboard.hotkey(ord("h"), ctrl=True)
            time.sleep(0.35)
            keyboard.type_ascii("needle")
            keyboard.hotkey(0xFF09)  # Tab to replacement
            keyboard.type_ascii("rbl")
            for _ in range(4):
                keyboard.hotkey(0xFF09)
            keyboard.hotkey(0xFF0D)  # Enter on Replace All
            time.sleep(1.5)
            keyboard.hotkey(0xFF1B)  # Escape closes the find bar
            for _ in range(4):
                keyboard.hotkey(ord("z"), ctrl=True)
                time.sleep(0.2)
                keyboard.hotkey(ord("y"), ctrl=True)
                time.sleep(0.2)
            keyboard.hotkey(ord("s"), ctrl=True)
            time.sleep(1.0)
            if os.path.getsize(documents[0]) >= original_size:
                raise RuntimeError("Find/Replace All did not reduce the saved fixture size")
        elif workload == "CompilerTask":
            keyboard.hotkey(0xFFC2)  # F5
        elif workload == "TestSuite":
            keyboard.hotkey(ord("t"), ctrl=True)
        else:
            raise ValueError(f"unsupported interactive workload: {workload}")
    finally:
        keyboard.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "scenario",
        choices=("EditUndoRedo", "FindReplaceUndoRedo", "CompilerTask", "TestSuite"),
    )
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--documents-file", default="")
    args = parser.parse_args()
    try:
        run_workload(args.scenario, args.pid, args.documents_file)
    except Exception as error:  # noqa: BLE001 - surface automation failures to the harness
        print(f"IDE workload automation failed: {error}", file=sys.stderr)
        return 1
    print(f"PASS: WSLg workload automation {args.scenario}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
