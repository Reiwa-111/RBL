"""Interactive smoke test for the persistent RBL Studio process worker.

Run after building `rbl-studio-worker`, optionally setting
RBL_STUDIO_WORKER to the exact worker executable to test.
"""

from __future__ import annotations

import json
import os
import queue
import subprocess
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    suffix = ".exe" if os.name == "nt" else ""
    worker = Path(os.environ.get(
        "RBL_STUDIO_WORKER",
        ROOT / "target" / "debug" / f"rbl-studio-worker{suffix}",
    ))
    if not worker.is_absolute():
        worker = ROOT / worker
    if not worker.is_file():
        raise SystemExit(f"worker executable not found: {worker}")

    source = ROOT / "examples" / "utilities" / "unit_converter.rbl"
    snapshot_dir = ROOT / "build" / "worker-smoke"
    snapshot_dir.mkdir(parents=True, exist_ok=True)
    snapshot = snapshot_dir / "unit_converter_snapshot.rbl"
    source_bytes = source.read_bytes()
    padding = b"#" + b"x" * 1022 + b"\n"
    with snapshot.open("wb") as snapshot_file:
        snapshot_file.write(source_bytes)
        while snapshot_file.tell() + len(padding) <= 2 * 1024 * 1024:
            snapshot_file.write(padding)
        snapshot_file.write(b" " * (2 * 1024 * 1024 - snapshot_file.tell()))
    assert snapshot.stat().st_size == 2 * 1024 * 1024
    process = subprocess.Popen(
        [str(worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1,
    )
    lines: queue.Queue[str | None] = queue.Queue()

    def read_events() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=read_events, daemon=True).start()

    def send(message: dict[str, object]) -> None:
        assert process.stdin is not None
        process.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
        process.stdin.flush()

    def receive(timeout: float = 60.0) -> dict[str, object]:
        try:
            line = lines.get(timeout=timeout)
        except queue.Empty as error:
            raise TimeoutError("worker event timeout") from error
        if line is None:
            stderr = process.stderr.read() if process.stderr else ""
            raise RuntimeError(f"worker closed stdout unexpectedly: {stderr}")
        return json.loads(line)

    assert receive()["event"] == "worker_started"
    send({"command": "initialize", "root": str(ROOT)})
    assert receive()["event"] == "ready"
    send({
        "command": "start", "request_id": "interactive-smoke", "task": "run",
        "workspace_root": str(ROOT),
        "source_path": str(source), "snapshot_file": str(snapshot),
        "is_rbl": True, "snapshot": None,
    })

    output = ""
    sent_unit = False
    sent_value = False
    sent_invalid_stdin = False
    saw_invalid_stdin_error = False
    accepted_inputs = 0
    exit_code: object = None
    while exit_code is None:
        event = receive()
        if event.get("event") == "started" and not sent_invalid_stdin:
            send({"command": "stdin_line", "request_id": "interactive-smoke"})
            sent_invalid_stdin = True
        elif event.get("event") == "output":
            output += str(event.get("text", ""))
            if not sent_unit and "Choose 1, 2 or 3: " in output:
                send({"command": "stdin_line", "request_id": "interactive-smoke", "line": "1"})
                sent_unit = True
            if not sent_value and "Value to convert: " in output:
                send({"command": "stdin_line", "request_id": "interactive-smoke", "line": "10"})
                sent_value = True
        elif event.get("event") == "finished":
            exit_code = event.get("exit_code")
        elif event.get("event") == "stdin_accepted":
            assert event.get("request_id") == "interactive-smoke", event
            accepted_inputs += 1
        elif event.get("event") == "error":
            if "stdin_line requires a line field" in str(event.get("message", "")):
                saw_invalid_stdin_error = True
            else:
                raise RuntimeError(str(event))

    assert sent_unit and sent_value and sent_invalid_stdin and saw_invalid_stdin_error, output
    assert accepted_inputs == 2, f"expected two stdin acknowledgements, got {accepted_inputs}"
    assert exit_code == 0, (exit_code, output)
    assert "10.000 km = 6.214 mi" in output, output
    assert not snapshot.exists(), "worker must remove the GUI-created snapshot after completion"
    send({"command": "stdin_line", "request_id": "interactive-smoke", "line": "late input"})
    inactive = receive()
    assert inactive.get("event") == "error" and "no active task" in str(inactive.get("message")), inactive

    # Exercise the protocol's explicit EOF path end-to-end. The first input is
    # delivered, then stdin is closed while the RBL program waits for a second
    # line. The runtime's expected EOF diagnostic proves the child observed the
    # closed pipe rather than merely receiving an acknowledgement from worker.
    eof_source = snapshot_dir / "stdin_eof.rbl"
    eof_source.write_text(
        "func main() (\n"
        "    set first = input()\n"
        "    set second = input()\n"
        "    print(first)\n"
        ")\n",
        encoding="utf-8",
        newline="\n",
    )
    send({
        "command": "start", "request_id": "stdin-eof-smoke", "task": "run",
        "workspace_root": str(ROOT),
        "source_path": None, "is_rbl": True, "snapshot": eof_source.read_text(encoding="utf-8"),
    })
    eof_output = ""
    sent_eof_input = False
    saw_eof_line_ack = False
    eof_requested = False
    saw_eof_ack = False
    saw_eof_runtime_error = False
    eof_exit_code: object = None
    while eof_exit_code is None:
        event = receive()
        if event.get("event") == "started" and not sent_eof_input:
            send({"command": "stdin_line", "request_id": "stdin-eof-smoke", "line": "first-line"})
            sent_eof_input = True
        elif event.get("event") == "stdin_accepted":
            assert event.get("request_id") == "stdin-eof-smoke", event
            saw_eof_line_ack = True
        elif event.get("event") == "stdin_eof_accepted":
            assert event.get("request_id") == "stdin-eof-smoke", event
            saw_eof_ack = True
        elif event.get("event") == "output":
            eof_output += str(event.get("text", ""))
            if "input() failed to read stdin" in eof_output:
                saw_eof_runtime_error = True
        elif event.get("event") == "finished":
            assert event.get("request_id") == "stdin-eof-smoke", event
            eof_exit_code = event.get("exit_code")
        elif event.get("event") == "error":
            raise RuntimeError(f"unexpected EOF protocol error: {event}")
        if saw_eof_line_ack and not eof_requested:
            # Send EOF exactly once, after the line request has been accepted.
            send({"command": "stdin_eof", "request_id": "stdin-eof-smoke"})
            eof_requested = True

    assert sent_eof_input and saw_eof_line_ack and saw_eof_ack, (saw_eof_line_ack, saw_eof_ack, eof_output)
    assert saw_eof_runtime_error, eof_output
    assert eof_exit_code == 1, (eof_exit_code, eof_output)
    send({"command": "shutdown"})
    assert receive()["event"] == "bye"
    assert process.wait(timeout=10) == 0
    print("RBL Studio worker interactive stdin/EOF integration: PASS")


if __name__ == "__main__":
    main()
