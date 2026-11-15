"""Exercise the worker's bounded event/output queues under a burst of stdout."""

from __future__ import annotations

import json
import os
import queue
import shutil
import signal
import subprocess
import tempfile
import threading
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_LINES = 60_000


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

    build_root = ROOT / "build"
    build_root.mkdir(parents=True, exist_ok=True)
    payload = "X" * 1024
    recovery_payload = "Y" * 1024

    def program(text: str) -> str:
        return (
            "func main() (\n"
            f"    for (i in 0..={OUTPUT_LINES - 1}) (\n"
            f'        print("{text}")\n'
            "    )\n"
            ")\n"
        )

    source_text = program(payload)
    recovery_text = program(recovery_payload)

    with tempfile.TemporaryDirectory(prefix="worker-backpressure-", dir=build_root) as temp:
        temp_root = Path(temp)
        source = Path(temp) / "worker_backpressure.rbl"
        recovery_source = Path(temp) / "worker_backpressure_recovery.rbl"
        source.write_text(source_text, encoding="utf-8", newline="\n")
        recovery_source.write_text(recovery_text, encoding="utf-8", newline="\n")

        compiler = shutil.which(os.environ.get("CC", "cc")) or shutil.which("gcc")
        if os.name == "nt" and not compiler:
            mingw = Path(os.environ.get("RBL_MINGW", r"C:\msys64\ucrt64\bin"))
            candidate = mingw / "gcc.exe"
            if candidate.is_file():
                compiler = str(candidate)
        if not compiler:
            raise SystemExit("C compiler not found; set CC or RBL_MINGW for the worker backpressure probe")
        stub = temp_root / ("worker-output-stub.exe" if os.name == "nt" else "worker-output-stub")
        compiler_env = os.environ.copy()
        compiler_env["PATH"] = str(Path(compiler).parent) + os.pathsep + compiler_env.get("PATH", "")
        compile_result = subprocess.run(
            [compiler, "-std=c11", "-O2", str(ROOT / "tests" / "worker_backpressure_stub.c"), "-o", str(stub)],
            capture_output=True,
            text=True,
            check=False,
            env=compiler_env,
        )
        if compile_result.returncode:
            raise RuntimeError(
                f"could not build deterministic worker output stub ({compile_result.returncode}): "
                f"{compile_result.stdout}\n{compile_result.stderr}"
            )

        stderr_log = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
        worker_env = os.environ.copy()
        worker_env["RBL_PYTHON"] = str(stub)
        worker_env["PATH"] = str(Path(compiler).parent) + os.pathsep + worker_env.get("PATH", "")
        process = subprocess.Popen(
            [str(worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=stderr_log, text=True, encoding="utf-8", bufsize=1,
            start_new_session=(os.name != "nt"),
            env=worker_env,
        )
        # A bounded handoff makes the first phase a real slow consumer: once
        # this queue fills, the reader blocks and backpressure reaches the
        # worker's stdout pipe instead of being hidden in an unbounded queue.
        lines: queue.Queue[str | None] = queue.Queue(maxsize=32)

        def read_stdout() -> None:
            assert process.stdout is not None
            try:
                for line in process.stdout:
                    lines.put(line)
            finally:
                try:
                    lines.put(None, timeout=1.0)
                except queue.Full:
                    pass

        reader = threading.Thread(target=read_stdout, name="worker-protocol-reader", daemon=True)
        reader.start()

        def stderr_tail() -> str:
            stderr_log.flush()
            stderr_log.seek(0, os.SEEK_END)
            size = stderr_log.tell()
            stderr_log.seek(max(0, size - 4096))
            return stderr_log.read()

        def send(message: dict[str, object]) -> None:
            if process.poll() is not None:
                raise RuntimeError(f"worker exited early ({process.returncode}): {stderr_tail()}")
            assert process.stdin is not None
            process.stdin.write(json.dumps(message) + "\n")
            process.stdin.flush()

        def receive(timeout: float = 30.0) -> dict[str, object]:
            try:
                line = lines.get(timeout=timeout)
            except queue.Empty as error:
                raise TimeoutError(
                    f"worker protocol timed out after {timeout}s; "
                    f"exit={process.poll()}; stderr={stderr_tail()}"
                ) from error
            if line is None:
                raise RuntimeError(f"worker closed stdout; exit={process.poll()}; stderr={stderr_tail()}")
            return json.loads(line)

        try:
            assert receive().get("event") == "worker_started"
            send({"command": "initialize", "root": str(ROOT)})
            assert receive().get("event") == "ready"

            def run_burst(request_id: str, source_path: Path, text: str,
                          marker: str, pause: float) -> tuple[object, int, int]:
                send({
                    "command": "start", "request_id": request_id, "task": "run",
                    "workspace_root": str(ROOT),
                    "source_path": str(source_path), "is_rbl": True, "snapshot": text,
                })
                time.sleep(pause)
                output_bytes = 0
                marker_count = 0
                exit_code: object = None
                while exit_code is None:
                    event = receive()
                    if event.get("event") == "output":
                        output_text = str(event.get("text", ""))
                        output_bytes += len(output_text.encode("utf-8"))
                        marker_count += output_text.count(marker)
                    elif event.get("event") == "finished":
                        exit_code = event.get("exit_code")
                    elif event.get("event") == "error":
                        raise RuntimeError(str(event))
                return exit_code, output_bytes, marker_count

            expected_bytes = OUTPUT_LINES * (len(payload.encode("utf-8")) + 1)
            expected_markers = OUTPUT_LINES * len(payload)

            # First task: stop consuming long enough to cover worker startup, fill the
            # bounded protocol/pipe queues, and activate the 2-second degraded mode.
            # The longer pause leaves margin for slower Windows process startup.
            degraded_exit, degraded_bytes, degraded_markers = run_burst(
                "backpressure", source, source_text, "X", 6.0
            )
            assert degraded_exit == 0, (degraded_exit, degraded_bytes)

            # Second task: pause for less than the degradation deadline, then consume.
            # If degraded mode is not reset after the first queue drains, this task loses
            # output as soon as the writer channel fills again.
            recovery_expected = OUTPUT_LINES * (len(recovery_payload.encode("utf-8")) + 1)
            recovery_exit, recovery_bytes, recovery_markers = run_burst(
                "backpressure-recovery", recovery_source, recovery_text, "Y", 0.5
            )
            assert recovery_exit == 0, (recovery_exit, recovery_bytes)
            assert recovery_bytes >= recovery_expected, (recovery_bytes, recovery_expected)
            assert recovery_markers == expected_markers, (recovery_markers, expected_markers)
            assert 0 < degraded_markers < expected_markers * 0.9, (
                "the slow-consumer phase did not discard a substantial amount of stub payload",
                degraded_markers,
                expected_markers,
            )
            send({"command": "shutdown"})
            assert receive().get("event") == "bye"
            if process.wait(timeout=10) != 0:
                raise RuntimeError(f"worker shutdown failed ({process.returncode}): {stderr_tail()}")
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    process.terminate()
                else:
                    os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        process.kill()
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            if process.stdin is not None:
                process.stdin.close()
            if process.stdout is not None:
                process.stdout.close()
            reader.join(timeout=2)
            stderr_log.close()
    print(
        "RBL Studio worker output backpressure/recovery: PASS "
        f"(degraded={degraded_markers}/{expected_markers}, recovery={recovery_markers}/{expected_markers} marker bytes; "
        f"aggregate={degraded_bytes}/{expected_bytes} vs {recovery_bytes}/{recovery_expected})"
    )


if __name__ == "__main__":
    main()
