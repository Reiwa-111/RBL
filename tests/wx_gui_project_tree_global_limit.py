import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import psutil
import win32gui
import win32process

ROOT = pathlib.Path.cwd()
GUI = ROOT / "build/wx-ui-win/rbl-studio-wx.exe"
WORKER = ROOT / "build/wx-ui-win/rbl-studio-worker.exe"
FIXTURE = ROOT / "build/wx-ui-win/tree-directory-stress"


def main():
    FIXTURE.mkdir(parents=True, exist_ok=True)
    profile = pathlib.Path(tempfile.mkdtemp(prefix="rbl-wx-tree-profile-"))
    try:
        for index in range(35):
            parent = FIXTURE / f"parent-{index:02d}"
            parent.mkdir()
            for child in range(1500):
                (parent / f"child-{child:04d}").mkdir()
        (profile / "ui.ini").write_text(
            "[Workspace]\nRoot=" + FIXTURE.as_posix() + "\n", encoding="utf-8"
        )
        env = os.environ.copy()
        env["RBL_STUDIO_CONFIG_DIR"] = str(profile)
        env["RBL_STUDIO_WORKER"] = str(WORKER)
        env["RBL_PYTHON"] = __import__("sys").executable
        process = subprocess.Popen([str(GUI)], cwd=ROOT, env=env)
        try:
            time.sleep(5)
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
                raise RuntimeError("IDE frame missing")
            trees = []
            win32gui.EnumChildWindows(
                frames[0],
                lambda h, _: trees.append(h)
                if win32gui.GetClassName(h) == "SysTreeView32"
                else None,
                None,
            )
            if not trees:
                raise RuntimeError("project tree control missing")
            tree = trees[0]
            root_item = win32gui.SendMessage(tree, 0x110A, 0, 0)
            child = win32gui.SendMessage(tree, 0x110A, 4, root_item)
            expanded = 0
            peak_rss = 0
            while child and expanded < 35:
                win32gui.SendMessage(tree, 0x1102, 2, child)
                expanded += 1
                count = win32gui.SendMessage(tree, 0x1105, 0, 0)
                try:
                    procs = [psutil.Process(process.pid)] + psutil.Process(process.pid).children(recursive=True)
                    peak_rss = max(
                        peak_rss,
                        sum(proc.memory_info().rss for proc in procs if proc.is_running()),
                    )
                except psutil.Error:
                    pass
                if count >= 50000:
                    break
                child = win32gui.SendMessage(tree, 0x110A, 1, child)
            count = win32gui.SendMessage(tree, 0x1105, 0, 0)
            print("directory-heavy expansion count", count, "expanded", expanded,
                  "peak_process_tree_rss_mib", round(peak_rss / 1048576, 1), flush=True)
            if count > 50000 or count < 49900:
                raise RuntimeError(f"global tree node cap not exercised safely, got {count}")
            print("directory-heavy 50,000-node global cap: PASS", flush=True)
        finally:
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
    finally:
        shutil.rmtree(FIXTURE, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
