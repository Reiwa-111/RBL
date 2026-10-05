#!/usr/bin/env python3
"""WSLg/X11-safe RBL Studio launcher.

The UI is constructed after Tk enters its event loop. This avoids a class of
WSLg/Tk startup races where the Tcl interpreter is alive but the first window
never gets mapped.
"""
from __future__ import annotations

import os
import signal
import sys
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def main() -> int:
    debug = os.environ.get("RBLSTUDIO_DEBUG") == "1" or "--debug" in sys.argv
    safe_mode = "--safe" in sys.argv
    debug_log = ROOT / "ide_startup.log"

    def log(msg: str) -> None:
        line = msg
        if debug:
            with debug_log.open("a", encoding="utf-8") as fh:
                fh.write(line + "\n")
            print(line, flush=True)

    try:
        log(f"startup root={ROOT}")
        log(f"DISPLAY={os.environ.get('DISPLAY', '<unset>')}")
        log(f"WAYLAND_DISPLAY={os.environ.get('WAYLAND_DISPLAY', '<unset>')}")

        import tkinter as tk
        from ide.rbl_studio import RBLStudio

        # Keep construction minimal until Tk's event loop is alive. This is
        # substantially more reliable under WSLg/XWayland than creating every
        # widget before the first event is processed.
        root = tk.Tk()
        root.title("RBL Studio")
        root.minsize(900, 600)
        root.geometry("1280x800+60+60")
        root.resizable(True, True)
        try:
            root.overrideredirect(False)
        except tk.TclError:
            pass

        app_state = {"app": None, "shown": False}

        def report_callback_exception(exc, val, tb):
            text = "".join(traceback.format_exception(exc, val, tb))
            debug_log.write_text(text, encoding="utf-8")
            print(text, file=sys.stderr, end="")

        root.report_callback_exception = report_callback_exception

        # Ctrl+C should terminate the GUI instead of leaving the shell looking
        # hung inside Tk's event loop.
        def sigint(_signum, _frame):
            try:
                root.after(0, root.destroy)
            except Exception:
                os._exit(130)

        try:
            signal.signal(signal.SIGINT, sigint)
        except Exception:
            pass

        def bring_to_front() -> None:
            try:
                root.deiconify()
                root.state("normal")
                root.update_idletasks()
                root.lift()
                root.focus_force()
                root.attributes("-alpha", 1.0)
                # WSLg/XWayland can ignore one of lift/focus calls on the first
                # map; the temporary topmost flag makes the initial map reliable.
                root.attributes("-topmost", True)
                root.after(1200, lambda: root.attributes("-topmost", False))
                try:
                    root.eval("tk::PlaceWindow . center")
                except tk.TclError:
                    pass
                root.update_idletasks()
                app_state["shown"] = True
                log(
                    "window mapped: "
                    f"state={root.state()} viewable={root.winfo_viewable()} "
                    f"mapped={root.winfo_ismapped()} geometry={root.winfo_geometry()}"
                )
            except tk.TclError as exc:
                log(f"show warning: {exc!r}")

        def start_ui() -> None:
            try:
                root.withdraw()
                root.update_idletasks()
                if safe_mode:
                    # Safe mode is deliberately tiny: it proves the GUI stack
                    # works without touching the compiler/project UI.
                    frame = tk.Frame(root, padx=28, pady=28)
                    frame.pack(fill="both", expand=True)
                    tk.Label(
                        frame,
                        text="RBL Studio — GUI OK",
                        font=("DejaVu Sans", 22, "bold"),
                    ).pack(pady=(10, 8))
                    tk.Label(
                        frame,
                        text="WSLg/Tk window creation is working.\nClose this window or use normal mode to start the IDE.",
                        justify="center",
                    ).pack(pady=8)
                    tk.Button(frame, text="Close", command=root.destroy, width=14).pack(pady=12)
                else:
                    initial = next((arg for arg in sys.argv[1:] if arg not in ("--debug", "--safe")), None)
                    app_state["app"] = RBLStudio(root, initial)
                root.update_idletasks()
                log("RBL Studio UI initialized")
                root.after(20, bring_to_front)
            except Exception:
                debug_log.write_text(traceback.format_exc(), encoding="utf-8")
                print(f"RBL Studio failed to initialize. Full traceback: {debug_log}", file=sys.stderr)
                root.destroy()
                raise

        root.after(10, start_ui)
        # Enter the event loop immediately; start_ui builds the heavy UI from
        # inside Tk's own event processing.
        root.after(100, lambda: log("Tk event loop alive"))
        log("entering mainloop")
        root.mainloop()
        log("mainloop exited")
        return 0
    except Exception:
        debug_log.write_text(traceback.format_exc(), encoding="utf-8")
        print(f"RBL Studio failed to start. Full traceback: {debug_log}", file=sys.stderr)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
