from __future__ import annotations

import json
import os
import queue
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUEST_COUNT = 10_000


def main() -> None:
    suffix = ".exe" if os.name == "nt" else ""
    worker = Path(os.environ.get("RBL_STUDIO_WORKER", ROOT / "target" / "release" / f"rbl-studio-worker{suffix}"))
    if not worker.is_absolute():
        worker = ROOT / worker
    if not worker.is_file():
        raise SystemExit(f"worker executable not found: {worker}")

    process = subprocess.Popen(
        [str(worker)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )

    def receive_one(timeout: float = 10.0) -> dict[str, object]:
        assert process.stdout is not None
        result: queue.Queue[str] = queue.Queue()

        def read_line() -> None:
            result.put(process.stdout.readline())

        threading.Thread(target=read_line, daemon=True).start()
        try:
            line = result.get(timeout=timeout)
        except queue.Empty as error:
            raise TimeoutError("worker startup event timeout") from error
        if not line:
            raise RuntimeError("worker closed stdout during startup")
        return json.loads(line)

    assert receive_one()["event"] == "worker_started"
    assert process.stdin is not None
    process.stdin.write(json.dumps({"command": "initialize", "root": str(ROOT)}) + "\n")
    process.stdin.flush()
    assert receive_one()["event"] == "ready"

    # A burst of protocol errors is intentionally sent while stdout is not
    # consumed. This fills the bounded output channel and reaches the coordinator
    # high-water mark. New request intake must pause rather than consume the
    # reserved control slots and clear already accepted events.
    payload = "{invalid-json}\n" * REQUEST_COUNT + json.dumps({"command": "shutdown"}) + "\n"
    writer_error: list[BaseException] = []

    def flood_stdin() -> None:
        try:
            assert process.stdin is not None
            process.stdin.write(payload)
            process.stdin.flush()
            process.stdin.close()
        except BaseException as error:
            writer_error.append(error)

    writer = threading.Thread(target=flood_stdin, daemon=True)
    writer.start()
    time.sleep(2.5)

    lines: queue.Queue[dict[str, object] | None] = queue.Queue()

    def drain_stdout() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(json.loads(line))
        lines.put(None)

    reader = threading.Thread(target=drain_stdout, daemon=True)
    reader.start()
    events: list[dict[str, object]] = []
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        try:
            event = lines.get(timeout=1)
        except queue.Empty:
            if process.poll() is not None and not reader.is_alive():
                break
            continue
        if event is None:
            break
        events.append(event)

    writer.join(timeout=5)
    if writer.is_alive():
        process.kill()
        raise RuntimeError("stdin flood did not finish after stdout resumed")
    if writer_error:
        process.kill()
        raise RuntimeError(f"stdin flood failed: {writer_error[0]}")
    try:
        exit_code = process.wait(timeout=10)
    except subprocess.TimeoutExpired as error:
        process.kill()
        raise RuntimeError("worker did not exit after queued shutdown") from error

    names = [str(event.get("event")) for event in events]
    protocol_errors = names.count("protocol_error")
    if protocol_errors != REQUEST_COUNT:
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(
            f"control responses were lost under backpressure: expected={REQUEST_COUNT}, "
            f"received={protocol_errors}, tail={names[-20:]}, stderr={stderr}"
        )
    if not names or names[-1] != "bye":
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(f"bye was not the final protocol event: tail={names[-20:]}; stderr={stderr}")
    if exit_code != 0:
        stderr = process.stderr.read() if process.stderr else ""
        raise RuntimeError(f"worker exited {exit_code}: {stderr}")
    print(
        f"worker control reserve under stdout backpressure: PASS "
        f"({protocol_errors} protocol_error events retained, bye last, exit {exit_code})"
    )


if __name__ == "__main__":
    main()
