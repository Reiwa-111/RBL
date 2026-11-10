#!/usr/bin/env python3
"""Compare identical RBL programs built by two complete release toolchains.

Unlike the five-way benchmark, this isolates release-to-release RBL runtime
behavior: each version builds and links with its own compiler and Runtime.
Build time is excluded; process startup is included for both versions.
"""
from __future__ import annotations

import argparse
import hashlib
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKLOADS = (
    "benchmarks/rbl_bench_empty.rbl",
    "benchmarks/rbl_bench_sum.rbl",
    "benchmarks/rbl_bench_calls.rbl",
    "benchmarks/rbl_bench_branches.rbl",
)


def pin_cpu(requested: int | None) -> int | None:
    if not hasattr(os, "sched_getaffinity"):
        return None
    if requested is None:
        return None
    try:
        allowed = os.sched_getaffinity(0)
        cpu = requested
        if cpu not in allowed:
            raise OSError(f"CPU {cpu} is outside the allowed affinity set {sorted(allowed)}")
        os.sched_setaffinity(0, {cpu})
        return cpu
    except OSError:
        return None


def build(root: Path, relative: str, env: dict[str, str]) -> Path:
    source = root / relative
    result = subprocess.run(
        [sys.executable, str(root / "rbl.py"), "build", relative],
        cwd=root, env=env, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if result.returncode:
        raise RuntimeError(f"build failed in {root} for {relative}:\n{result.stdout}")
    prefix = "Built: "
    executable = next(
        (Path(line[len(prefix):].strip()) for line in result.stdout.splitlines()
         if line.startswith(prefix)), None
    )
    if executable is None:
        raise RuntimeError(f"build did not report its executable for {relative}:\n{result.stdout}")
    if not executable.is_absolute():
        executable = root / executable
    executable = executable.resolve()
    if not executable.is_file():
        raise RuntimeError(f"built executable does not exist: {executable}")
    return executable


def run(executable: Path, cwd: Path) -> float:
    start = time.perf_counter_ns()
    result = subprocess.run([str(executable)], cwd=cwd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, check=False)
    elapsed = (time.perf_counter_ns() - start) / 1_000_000
    if result.returncode:
        raise RuntimeError(
            f"program failed ({result.returncode}): {executable}\n"
            f"{result.stderr.decode(errors='replace')}"
        )
    return elapsed


def summarize(samples: list[float]) -> tuple[float, float, float]:
    median = statistics.median(samples)
    p90 = sorted(samples)[max(0, math.ceil(0.90 * len(samples)) - 1)]
    mad = statistics.median(abs(value - median) for value in samples)
    return median, p90, mad


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-root", required=True, type=Path)
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--cpu", type=int, default=None, help="pin runner and child processes to this allowed CPU")
    args = parser.parse_args()
    if args.warmups < 0 or args.reps < 3:
        parser.error("--warmups must be non-negative and --reps at least 3")
    baseline_root = args.baseline_root.resolve()
    candidate_root = args.candidate_root.resolve()
    for root in (baseline_root, candidate_root):
        if not (root / "rbl.py").is_file() or not (root / "VERSION").is_file():
            parser.error(f"not an RBL checkout: {root}")

    env = os.environ.copy()
    env["RBL_TOOLCHAIN"] = "native"
    cpu = pin_cpu(args.cpu)
    versions = {
        "baseline": (baseline_root / "VERSION").read_text(encoding="utf-8").strip(),
        "candidate": (candidate_root / "VERSION").read_text(encoding="utf-8").strip(),
    }
    print(f"Platform: {sys.platform}; pinned CPU: {cpu if cpu is not None else 'unavailable'}")
    print(f"Baseline: {versions['baseline']} ({baseline_root})")
    print(f"Candidate: {versions['candidate']} ({candidate_root})")
    print(f"Warmups: {args.warmups}; measured runs: {args.reps}; process startup included")

    with tempfile.TemporaryDirectory(prefix="rbl-release-runtime-") as temp:
        run_root = Path(temp)
        for relative in WORKLOADS:
            old_source = baseline_root / relative
            new_source = candidate_root / relative
            if hashlib.sha256(old_source.read_bytes()).digest() != hashlib.sha256(new_source.read_bytes()).digest():
                raise RuntimeError(f"workload source differs between releases: {relative}")
            old_exe = build(baseline_root, relative, env)
            new_exe = build(candidate_root, relative, env)
            samples = {"baseline": [], "candidate": []}
            for index in range(args.warmups + args.reps):
                order = (("baseline", old_exe), ("candidate", new_exe))
                if index % 2:
                    order = tuple(reversed(order))
                for label, executable in order:
                    cwd = run_root / label
                    cwd.mkdir(exist_ok=True)
                    elapsed = run(executable, cwd)
                    if index >= args.warmups:
                        samples[label].append(elapsed)
            old = summarize(samples["baseline"])
            new = summarize(samples["candidate"])
            ratio = old[0] / new[0]
            print(
                f"{Path(relative).stem:22} {old[0]:9.3f} ms -> {new[0]:9.3f} ms "
                f"speedup={ratio:5.3f}x p90_delta={new[1]-old[1]:+8.3f} ms "
                f"MAD_delta={new[2]-old[2]:+7.3f} ms"
            )
            print("  baseline:  " + ", ".join(f"{v:.2f}" for v in samples["baseline"]))
            print("  candidate: " + ", ".join(f"{v:.2f}" for v in samples["candidate"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
