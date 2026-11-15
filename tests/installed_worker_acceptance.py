#!/usr/bin/env python3
"""Exercise the installed Rust worker and its app-local Python backend without GUI startup."""
from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from typing import Any, Dict, Optional


def _read_lines(stream: Any, output: "queue.Queue[Optional[str]]") -> None:
    try:
        for line in stream:
            output.put(line)
    finally:
        output.put(None)


def _next_event(events: "queue.Queue[Optional[str]]", timeout: float) -> Dict[str, Any]:
    try:
        line = events.get(timeout=timeout)
    except queue.Empty as error:
        raise AssertionError(f"worker protocol timed out after {timeout}s") from error
    if line is None:
        raise AssertionError("worker closed stdout before completing the protocol")
    try:
        value = json.loads(line)
    except json.JSONDecodeError as error:
        raise AssertionError(f"worker emitted invalid NDJSON: {line!r}") from error
    if not isinstance(value, dict):
        raise AssertionError(f"worker emitted a non-object frame: {value!r}")
    if value.get("event") in ("protocol_error", "error"):
        raise AssertionError(f"worker protocol/backend error: {value}")
    return value


def run_worker(root: Path, environment: Dict[str, str], timeout: float = 120.0) -> None:
    worker_name = "rbl-studio-worker.exe" if os.name == "nt" else "rbl-studio-worker"
    worker = root / worker_name
    if not worker.is_file():
        raise AssertionError(f"installed worker is missing: {worker}")

    env = environment.copy()
    env.pop("RBL_PYTHON", None)
    env["RBL_TOOLCHAIN"] = "native"
    env["RBL_NO_BUILD_CACHE"] = "1"
    with tempfile.TemporaryDirectory(prefix="rbl-worker-settings-") as config_root:
        if os.name == "nt":
            settings_root = Path(config_root) / "RBLStudio"
            env["APPDATA"] = config_root
        else:
            settings_root = Path(config_root) / "RBLStudio"
            env["XDG_CONFIG_HOME"] = config_root
        settings_root.mkdir(parents=True, exist_ok=True)
        (settings_root / "settings.json").write_text(
            json.dumps({"wsl_mode": "native", "wsl_distro": "Ubuntu-24.04"}),
            encoding="utf-8",
        )

        process = subprocess.Popen(
            [str(worker)],
            cwd=str(root),
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        assert process.stdin is not None
        assert process.stdout is not None
        events: "queue.Queue[Optional[str]]" = queue.Queue()
        reader = threading.Thread(target=_read_lines, args=(process.stdout, events), daemon=True)
        reader.start()

        def send(value: Dict[str, Any]) -> None:
            assert process.stdin is not None
            process.stdin.write(json.dumps(value, ensure_ascii=False) + "\n")
            process.stdin.flush()

        try:
            started = _next_event(events, timeout)
            if started.get("event") != "worker_started" or started.get("protocol") != 1:
                raise AssertionError(f"unexpected worker startup response: {started}")
            send({"command": "initialize", "root": str(root)})
            ready = _next_event(events, timeout)
            if ready.get("event") != "ready" or ready.get("protocol") != 1:
                raise AssertionError(f"unexpected worker initialization response: {ready}")

            request_id = "app-local-python-check"
            send({
                "command": "start",
                "request_id": request_id,
                "task": "run",
                "workspace_root": str(root),
                "source_path": str(root / "examples" / "test.rbl"),
                "is_rbl": True,
            })
            output_text = []
            finished = None
            while finished is None:
                event = _next_event(events, timeout)
                if event.get("request_id") not in (None, request_id):
                    continue
                if event.get("event") == "output":
                    output_text.append(str(event.get("text", "")))
                elif event.get("event") == "finished":
                    finished = event
            if finished.get("exit_code") != 0:
                raise AssertionError(f"worker backend run failed: {finished}; output={''.join(output_text)!r}")
            combined = "".join(output_text)
            for expected in ("10", "change number", "20", "big", "5"):
                if expected not in combined:
                    raise AssertionError(f"worker backend output missing {expected!r}: {combined!r}")

            send({"command": "shutdown"})
            bye = _next_event(events, timeout)
            if bye.get("event") != "bye":
                raise AssertionError(f"unexpected worker shutdown response: {bye}")
            process.stdin.close()
            exit_code = process.wait(timeout=15)
            if exit_code != 0:
                raise AssertionError(f"worker exited with code {exit_code}")
        except Exception:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            raise
        finally:
            try:
                process.stdin.close()
            except Exception:
                pass
            try:
                process.stdout.close()
            except Exception:
                pass
            reader.join(timeout=2)

    print("Installed worker acceptance: PASS (initialize, app-local Python backend run, shutdown)")
