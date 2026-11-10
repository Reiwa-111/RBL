"""Exercise the worker's bounded event/output queues under a burst of stdout."""

from __future__ import annotations

import json
import os
import subprocess
import time
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

    source = ROOT / "build" / "worker_backpressure.rbl"
    source.parent.mkdir(parents=True, exist_ok=True)
    payload = "X" * 1024
    source_text = (
        "func main() (\n"
        "    for (i in 0..=5999) (\n"
        f'        print("{payload}")\n'
        "    )\n"
        ")\n"
    )
    source.write_text(source_text, encoding="utf-8", newline="\n")

    process = subprocess.Popen(
        [str(worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1,
    )

    def send(message: dict[str, object]) -> None:
        assert process.stdin is not None
        process.stdin.write(json.dumps(message) + "\n")
        process.stdin.flush()

    def receive() -> dict[str, object]:
        assert process.stdout is not None
        line = process.stdout.readline()
        if not line:
            stderr = process.stderr.read() if process.stderr else ""
            raise RuntimeError(f"worker closed stdout unexpectedly: {stderr}")
        return json.loads(line)

    assert receive().get("event") == "worker_started"
    send({"command": "initialize", "root": str(ROOT)})
    assert receive().get("event") == "ready"
    send({
        "command": "start", "request_id": "backpressure", "task": "run",
        "workspace_root": str(ROOT),
        "source_path": str(source), "is_rbl": True, "snapshot": source_text,
    })

    # Let the child fill the bounded queues and block on the worker's stdout
    # pipe. Resume consumption before the explicit 2-second broken-consumer
    # failsafe, then verify every emitted byte is delivered.
    time.sleep(0.5)
    output_bytes = 0
    exit_code: object = None
    while exit_code is None:
        event = receive()
        if event.get("event") == "output":
            output_bytes += len(str(event.get("text", "")).encode("utf-8"))
        elif event.get("event") == "finished":
            exit_code = event.get("exit_code")
        elif event.get("event") == "error":
            raise RuntimeError(str(event))

    expected_bytes = 6000 * (len(payload.encode("utf-8")) + 1)
    assert exit_code == 0, (exit_code, output_bytes)
    assert output_bytes >= expected_bytes, (output_bytes, expected_bytes)
    send({"command": "shutdown"})
    assert receive().get("event") == "bye"
    assert process.wait(timeout=10) == 0
    print(f"RBL Studio worker output backpressure: PASS ({output_bytes} bytes)")


if __name__ == "__main__":
    main()
