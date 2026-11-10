"""End-to-end check that worker app scripts and task workspaces are independent."""

from __future__ import annotations

import json
import os
import queue
import subprocess
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    if os.name == "nt":
        raise SystemExit("studio_worker_root_split.py is a POSIX worker integration test")
    worker = Path(os.environ.get("RBL_STUDIO_WORKER", ROOT / "target" / "debug" / "rbl-studio-worker"))
    if not worker.is_absolute():
        worker = ROOT / worker
    if not worker.is_file():
        raise SystemExit(f"worker executable not found: {worker}")

    with tempfile.TemporaryDirectory(prefix="rbl-worker-root-split-") as temporary:
        base = Path(temporary)
        application_root = base / "installation"
        for directory in ("tools", "tests", "benchmarks"):
            (application_root / directory).mkdir(parents=True, exist_ok=True)
        for script in (
            "tools/rbltool.py",
            "tests/run_tests.py",
            "benchmarks/run_benchmarks.py",
        ):
            (application_root / script).write_text("# fixture\n", encoding="utf-8")

        workspaces = [base / "project-a", base / "project-b"]
        for workspace in workspaces:
            workspace.mkdir()

        fake_python = base / "python-probe"
        fake_python.write_text(
            "#!/bin/sh\n"
            "printf 'cwd=%s\\nscript=%s\\n' \"$PWD\" \"$1\"\n",
            encoding="utf-8",
        )
        fake_python.chmod(0o755)

        environment = os.environ.copy()
        environment["RBL_PYTHON"] = str(fake_python)
        process = subprocess.Popen(
            [str(worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1,
            env=environment,
        )
        assert process.stdin is not None and process.stdout is not None
        events: queue.Queue[dict[str, object] | None] = queue.Queue()

        def read_events() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                events.put(json.loads(line))
            events.put(None)

        threading.Thread(target=read_events, daemon=True).start()

        def send(message: dict[str, object]) -> None:
            assert process.stdin is not None
            process.stdin.write(json.dumps(message) + "\n")
            process.stdin.flush()

        def receive() -> dict[str, object]:
            try:
                event = events.get(timeout=10)
            except queue.Empty as error:
                raise TimeoutError("worker event timeout") from error
            if event is None:
                stderr = process.stderr.read() if process.stderr else ""
                raise RuntimeError(f"worker exited unexpectedly: {stderr}")
            return event

        try:
            assert receive().get("event") == "worker_started"
            send({"command": "initialize", "root": str(application_root)})
            assert receive().get("event") == "ready"

            for index, workspace in enumerate(workspaces):
                request_id = f"root-split-{index}"
                send({
                    "command": "start",
                    "request_id": request_id,
                    "task": "doctor",
                    "workspace_root": str(workspace),
                    "is_rbl": False,
                })
                output = ""
                while True:
                    event = receive()
                    if event.get("event") == "output":
                        output += str(event.get("text", ""))
                    elif event.get("event") == "error":
                        raise RuntimeError(str(event))
                    elif event.get("event") == "finished":
                        assert event.get("request_id") == request_id, event
                        assert event.get("exit_code") == 0, (event, output)
                        break
                assert f"cwd={workspace}" in output, output
                assert f"script={application_root / 'tools' / 'rbltool.py'}" in output, output

            send({"command": "shutdown"})
            assert receive().get("event") == "bye"
            assert process.wait(timeout=10) == 0
            print("RBL Studio worker app-root/workspace-root split: PASS")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
