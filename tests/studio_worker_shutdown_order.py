from __future__ import annotations

import json
import os
import queue
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    suffix = ".exe" if os.name == "nt" else ""
    worker = Path(os.environ.get("RBL_STUDIO_WORKER", ROOT / "target" / "release" / f"rbl-studio-worker{suffix}"))
    if not worker.is_absolute():
        worker = ROOT / worker
    if not worker.is_file():
        raise SystemExit(f"worker executable not found: {worker}")

    fixture_dir = ROOT / "build" / "worker-shutdown-order"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    source = fixture_dir / "output_burst.rbl"
    source.write_text(
        "func main() (\n"
        "    for (i in 0..100000) (\n"
        "        print(\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\")\n"
        "    )\n"
        ")\n",
        encoding="utf-8",
    )

    process = subprocess.Popen(
        [str(worker)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )

    def send(message: dict[str, object]) -> None:
        assert process.stdin is not None
        process.stdin.write(json.dumps(message) + "\n")
        process.stdin.flush()

    def receive_one(timeout: float = 10.0) -> dict[str, object]:
        assert process.stdout is not None
        line_queue: queue.Queue[str | None] = queue.Queue()

        def read_line() -> None:
            line_queue.put(process.stdout.readline())

        threading.Thread(target=read_line, daemon=True).start()
        try:
            line = line_queue.get(timeout=timeout)
        except queue.Empty as error:
            raise TimeoutError("worker startup event timeout") from error
        if not line:
            raise RuntimeError("worker closed stdout during startup")
        return json.loads(line)

    assert receive_one()["event"] == "worker_started"
    send({"command": "initialize", "root": str(ROOT)})
    assert receive_one()["event"] == "ready"
    send({
        "command": "start",
        "request_id": "shutdown-order",
        "task": "run",
        "workspace_root": str(ROOT),
        "source_path": str(source),
        "snapshot_file": None,
        "is_rbl": True,
        "snapshot": None,
    })
    started = False
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        event = receive_one()
        if event.get("event") == "started":
            started = True
            break
        if event.get("event") == "error":
            raise RuntimeError(f"task start failed: {event}")
    if not started:
        raise RuntimeError("task did not start")

    # Stop consuming stdout while the child emits a multi-megabyte burst. This
    # fills the writer pipe and bounded queues, creating pending coordinator
    # events when shutdown is requested.
    # Exceed the worker's normal two-second output backpressure deadline. A
    # shutdown must retain terminal control events even if queued output text
    # has to be discarded, then deliver Finished before bye once draining
    # resumes.
    time.sleep(3.0)
    send({"command": "shutdown"})

    lines: queue.Queue[str | None] = queue.Queue()

    def drain_stdout() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=drain_stdout, daemon=True).start()
    events: list[dict[str, object]] = []
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            line = lines.get(timeout=1)
        except queue.Empty:
            if process.poll() is not None:
                break
            continue
        if line is None:
            break
        events.append(json.loads(line))

    try:
        exit_code = process.wait(timeout=10)
    except subprocess.TimeoutExpired as error:
        process.kill()
        raise RuntimeError("worker did not exit after shutdown") from error
    if exit_code != 0:
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(f"worker exited {exit_code}: {stderr}")

    names = [str(event.get("event")) for event in events]
    if "finished" not in names:
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(f"shutdown discarded Finished; events={names[-20:]}; count={len(names)}; stderr={stderr}")
    if "bye" not in names:
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(f"shutdown did not emit bye; events={names[-20:]}; count={len(names)}; stderr={stderr}")
    if names.index("finished") > names.index("bye"):
        raise RuntimeError("bye overtook the task's Finished event")
    output_count = sum(event.get("event") == "output" for event in events)
    if output_count == 0:
        raise RuntimeError("stdout burst did not deliver any output before terminal events")
    print(
        f"worker shutdown drains pending output: PASS "
        f"({output_count} output events, Finished before bye, exit {exit_code})"
    )


if __name__ == "__main__":
    main()
