#!/usr/bin/env python3
"""Verify that the IDE toolchain driver forwards child output incrementally."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


class _SignalWriter:
    def __init__(self, event: threading.Event, marker: str):
        self.event = event
        self.marker = marker
        self.parts: list[str] = []

    def write(self, text: str) -> int:
        self.parts.append(text)
        if self.marker in text:
            self.event.set()
        return len(text)

    def flush(self) -> None:
        pass


class _NullWriter:
    def write(self, text: str) -> int:
        return len(text)

    def flush(self) -> None:
        pass


def main() -> int:
    stdout_event = threading.Event()
    stderr_event = threading.Event()
    stdout = _SignalWriter(stdout_event, "STREAM_FIRST")
    stderr = _SignalWriter(stderr_event, "STREAM_ERROR")
    old_stdout, old_stderr = sys.stdout, sys.stderr
    old_setting = os.environ.get("RBL_STREAM_OUTPUT")
    result: dict[str, tuple[int, str, str]] = {}
    started = time.monotonic()

    def run() -> None:
        result["value"] = rbltool.run_native(
            [
                sys.executable,
                "-c",
                (
                    "import sys,time; "
                    "print('STREAM_FIRST', flush=True); "
                    "print('STREAM_ERROR', file=sys.stderr, flush=True); "
                    "time.sleep(1.2); "
                    "print('STREAM_SECOND', flush=True)"
                ),
            ]
        )

    try:
        os.environ["RBL_STREAM_OUTPUT"] = "1"
        sys.stdout, sys.stderr = stdout, stderr
        worker = threading.Thread(target=run)
        worker.start()
        if not stdout_event.wait(0.8):
            raise AssertionError("stdout was not forwarded before the child exited")
        first_output_latency = time.monotonic() - started
        if first_output_latency >= 1.0:
            raise AssertionError(f"first output was delayed by {first_output_latency:.3f}s")
        if not stderr_event.wait(0.8):
            raise AssertionError("stderr was not forwarded before the child exited")
        if not worker.is_alive():
            raise AssertionError("child unexpectedly completed before the streaming assertion")
        worker.join(5)
        if worker.is_alive():
            raise AssertionError("streaming subprocess did not finish")
        code, captured_out, captured_err = result["value"]
        if code != 0:
            raise AssertionError(f"streaming child exited with {code}: {captured_err}")
        if "STREAM_FIRST" not in captured_out or "STREAM_SECOND" not in captured_out:
            raise AssertionError(f"stdout capture incomplete: {captured_out!r}")
        if "STREAM_ERROR" not in captured_err:
            raise AssertionError(f"stderr capture incomplete: {captured_err!r}")
    finally:
        sys.stdout, sys.stderr = old_stdout, old_stderr
        if old_setting is None:
            os.environ.pop("RBL_STREAM_OUTPUT", None)
        else:
            os.environ["RBL_STREAM_OUTPUT"] = old_setting

    # Exercise the complete IDE run bridge: the native RBL Runtime must flush
    # print() before input(), and the driver's inherited stdin must carry more
    # than one line back to the running program.
    build_root = ROOT / "build"
    build_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ide-stdin-stream-", dir=build_root) as temp:
        source = Path(temp) / "interactive_input.rbl"
        source.write_text(
            'func main() (\n'
            '    print("READY")\n'
            '    set first = input()\n'
            '    set second = input("SECOND> ")\n'
            '    print(f"RESULT:{first}:{second}")\n'
            ')\n',
            encoding="utf-8",
        )
        output_dir = rbltool.build_output_dir(source, {})
        env = os.environ.copy()
        env["RBL_STREAM_OUTPUT"] = "1"
        process = subprocess.Popen(
            [sys.executable, str(ROOT / "tools" / "rbltool.py"), "run", str(source)],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=env,
        )
        output_parts: list[str] = []
        stderr_result: dict[str, str] = {}
        first_prompt = threading.Event()
        second_prompt = threading.Event()

        def read_interactive_output() -> None:
            assert process.stdout is not None
            while character := process.stdout.read(1):
                output_parts.append(character)
                output = "".join(output_parts)
                if "READY\n" in output:
                    first_prompt.set()
                if "SECOND> " in output:
                    second_prompt.set()

        def read_interactive_error() -> None:
            assert process.stderr is not None
            stderr_result["text"] = process.stderr.read()

        stdout_reader = threading.Thread(target=read_interactive_output, daemon=True)
        stderr_reader = threading.Thread(target=read_interactive_error, daemon=True)
        stdout_reader.start()
        stderr_reader.start()
        try:
            if not first_prompt.wait(30):
                raise AssertionError(
                    f"RBL print/input prompt was not streamed: {''.join(output_parts)!r}"
                )
            if process.stdin is None:
                raise AssertionError("interactive RBL process has no stdin pipe")
            process.stdin.write("alpha\n")
            process.stdin.flush()
            if not second_prompt.wait(10):
                raise AssertionError(
                    "second input() prompt was not reached: "
                    f"stdout={''.join(output_parts)!r} "
                    f"stderr={stderr_result.get('text', '')!r} "
                    f"exit={process.poll()}"
                )
            process.stdin.write("beta\n")
            process.stdin.flush()
            process.stdin.close()
            return_code = process.wait(timeout=30)
            stdout_reader.join(timeout=3)
            stderr_reader.join(timeout=3)
            output = "".join(output_parts)
            if return_code != 0 or "READY\n" not in output or "RESULT:alpha:beta" not in output:
                raise AssertionError(
                    f"interactive RBL stdin/stdout bridge failed ({return_code}): "
                    f"stdout={output!r} stderr={stderr_result.get('text', '')!r}"
                )
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            shutil.rmtree(output_dir, ignore_errors=True)

    old_timeout = rbltool.PROGRAM_TIMEOUT
    old_stdout, old_stderr = sys.stdout, sys.stderr
    os.environ["RBL_STREAM_OUTPUT"] = "1"
    sys.stdout, sys.stderr = _NullWriter(), _NullWriter()
    try:
        rbltool.PROGRAM_TIMEOUT = 0.25
        started = time.monotonic()
        code, _, _ = rbltool.run_native(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            input_text="x" * (8 * 1024 * 1024),
        )
        if code != rbltool.TIMEOUT_RC:
            raise AssertionError(f"blocked-stdin child did not time out: {code}")
        if time.monotonic() - started >= 3.0:
            raise AssertionError("blocked stdin delayed the subprocess timeout")

        rbltool.PROGRAM_TIMEOUT = 10.0
        descendant_script = (
            "import subprocess,sys; "
            "child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'], "
            "stdout=sys.stdout,stderr=sys.stderr); "
            "print(child.pid, flush=True)"
        )
        started = time.monotonic()
        code, out, err = rbltool.run_native([sys.executable, "-c", descendant_script])
        if code != 0:
            raise AssertionError(f"parent with inherited-pipe descendant failed: {code} {err}")
        if time.monotonic() - started >= 4.0:
            raise AssertionError("descendant-held pipe blocked the toolchain driver")
        descendant_pid = int(out.strip().splitlines()[0])
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/PID", str(descendant_pid), "/T", "/F"],
                capture_output=True,
                timeout=5,
            )
        else:
            import signal

            try:
                os.kill(descendant_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

        code, out, _ = rbltool.run_native(
            [sys.executable, "-c", "import sys; sys.stdout.write('x' * (2 * 1024 * 1024))"]
        )
        if code != 0 or len(out.encode("utf-8")) > rbltool.STREAM_CAPTURE_LIMIT_BYTES:
            raise AssertionError("streaming diagnostic capture exceeded its 1 MiB bound")
    finally:
        rbltool.PROGRAM_TIMEOUT = old_timeout
        sys.stdout, sys.stderr = old_stdout, old_stderr
        if old_setting is None:
            os.environ.pop("RBL_STREAM_OUTPUT", None)
        else:
            os.environ["RBL_STREAM_OUTPUT"] = old_setting

    print("PASS: incremental stdout/stderr, bounded capture, blocked stdin timeout, and inherited-pipe detachment")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
