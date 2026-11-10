#!/usr/bin/env python3
"""Compare compiler-only latency for two rblc-asm executables.

The harness generates deterministic source workloads, invokes each compiler
directly (no rbltool build cache, assembler, linker or program execution), and
records generated-assembly size and SHA-256 for each compiler. Different
releases are expected to emit different assembly when ABI checks, diagnostics
or code-generation behavior changes; output identity is therefore reported,
not required for a valid timing comparison.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def workloads(directory: Path) -> list[tuple[str, Path]]:
    cases: list[tuple[str, Path]] = []

    for count in (100, 500, 1000, 2000):
        source = directory / f"variables_{count}.rbl"
        body = "".join(f"    set value_{i} = {i}\n" for i in range(count))
        source.write_text(f"func main() (\n{body}    print(value_0)\n)\n", encoding="utf-8")
        cases.append((f"variables/{count}", source))

    for count in (500, 1000, 2000, 4000):
        source = directory / f"strings_{count}.rbl"
        body = "".join(f'    set value_{i} = "unique_string_{i}"\n' for i in range(count))
        source.write_text(f"func main() (\n{body}    print(value_0)\n)\n", encoding="utf-8")
        cases.append((f"unique-strings/{count}", source))

    for count in (100, 300, 600):
        source = directory / f"functions_{count}.rbl"
        functions = "\n".join(
            f"func fn_{i}(x int) int (\n    return x + {i}\n)" for i in range(count)
        )
        calls = "".join(f"    set result_{i} = fn_{i}({i})\n" for i in range(count))
        source.write_text(
            f"{functions}\n\nfunc main() (\n{calls}    print(result_0)\n)\n",
            encoding="utf-8",
        )
        cases.append((f"functions/{count}", source))

    return cases


def pin_current_process(requested: int | None) -> int | None:
    """Pin the runner and inherited compiler children to one allowed CPU."""
    if not hasattr(os, "sched_getaffinity") or not hasattr(os, "sched_setaffinity"):
        return None
    try:
        allowed = os.sched_getaffinity(0)
        cpu = requested if requested is not None else min(allowed)
        if cpu not in allowed:
            raise OSError(f"CPU {cpu} is outside the allowed affinity set {sorted(allowed)}")
        os.sched_setaffinity(0, {cpu})
        return cpu
    except OSError:
        return None


def run_compiler(compiler: Path, source: Path, output: Path, target: str) -> float:
    start = time.perf_counter()
    result = subprocess.run(
        [str(compiler), str(source), "-S", str(output), "--target", target],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    if result.returncode:
        raise RuntimeError(
            f"{compiler} failed on {source.name} (exit {result.returncode}):\n"
            f"{result.stdout}{result.stderr}"
        )
    return elapsed_ms


def summarize(samples: list[float]) -> tuple[float, float, float, list[float]]:
    median = statistics.median(samples)
    deviations = [abs(value - median) for value in samples]
    ordered = sorted(samples)
    p90 = ordered[max(0, math.ceil(0.90 * len(ordered)) - 1)]
    return median, p90, statistics.median(deviations), samples


def measure_pair(baseline: Path, candidate: Path, source: Path,
                 baseline_output: Path, candidate_output: Path, target: str,
                 warmups: int, repetitions: int):
    samples = {baseline: [], candidate: []}
    outputs = {baseline: baseline_output, candidate: candidate_output}
    artifacts = {}
    # Alternate execution order for each pair to reduce systematic timing drift.
    for index in range(warmups + repetitions):
        order = (baseline, candidate) if index % 2 == 0 else (candidate, baseline)
        for compiler in order:
            output = outputs[compiler]
            elapsed = run_compiler(compiler, source, output, target)
            if index >= warmups:
                samples[compiler].append(elapsed)
            payload = output.read_bytes()
            artifacts[compiler] = {
                "sha256": hashlib.sha256(payload).hexdigest(),
                "bytes": len(payload),
            }
    return (summarize(samples[baseline]), summarize(samples[candidate]),
            artifacts[baseline], artifacts[candidate])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path, help="older rblc-asm executable")
    parser.add_argument("--candidate", required=True, type=Path, help="new rblc-asm executable")
    parser.add_argument("--target", choices=("linux", "win"),
                        default="win" if os.name == "nt" else "linux")
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--cpu", type=int, default=None,
                        help="pin runner and compiler children to this allowed CPU")
    args = parser.parse_args()
    if args.warmups < 0 or args.reps < 1:
        parser.error("--warmups must be non-negative and --reps must be at least 1")
    baseline = args.baseline.resolve()
    candidate = args.candidate.resolve()
    for compiler in (baseline, candidate):
        if not compiler.is_file():
            parser.error(f"compiler executable not found: {compiler}")

    pinned_cpu = pin_current_process(args.cpu)

    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rbl-compiler-perf-", dir=ROOT / "build") as temp:
        directory = Path(temp)
        cases = workloads(directory)
        print(f"Platform: {platform.platform()} ({platform.machine()})")
        print(f"Target: {args.target}; warmups: {args.warmups}; measured runs: {args.reps}; pinned CPU: {pinned_cpu if pinned_cpu is not None else 'unavailable'}")
        for compiler in (baseline, candidate):
            version = subprocess.check_output([str(compiler), "--version"], text=True).strip()
            print(f"Compiler: {version}")
        print("\nworkload                 baseline ms   candidate ms   speedup   p90 delta   MAD delta   asm bytes old/new   same hash")
        for name, source in cases:
            old, new, old_artifact, new_artifact = measure_pair(
                baseline, candidate, source, directory / "baseline.s",
                directory / "candidate.s", args.target, args.warmups, args.reps,
            )
            print(
                f"{name:24} {old[0]:11.2f} {new[0]:14.2f} "
                f"{old[0] / new[0]:8.2f}x {new[1] - old[1]:10.2f} "
                f"{new[2] - old[2]:10.2f} {old_artifact['bytes']}/{new_artifact['bytes']} "
                f"{old_artifact['sha256'] == new_artifact['sha256']}"
            )
            print(f"  samples baseline: {', '.join(f'{v:.2f}' for v in old[3])} ms")
            print(f"  samples candidate: {', '.join(f'{v:.2f}' for v in new[3])} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
