#!/usr/bin/env python3
"""Measure full, parallel, incremental and no-op GNU Make compiler builds.

Run from the repository root on Linux/WSL. The incremental case only updates
the lexer source timestamp; it does not alter its contents.
"""
from __future__ import annotations

import argparse
import json
import math
import platform
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def summarize(values: list[float]) -> dict[str, float | list[float]]:
    ordered = sorted(values)
    median = statistics.median(values)
    return {
        "median_ms": median,
        "p90_ms": ordered[max(0, math.ceil(0.90 * len(ordered)) - 1)],
        "mad_ms": statistics.median(abs(value - median) for value in values),
        "samples_ms": values,
    }


def run(command: list[str]) -> float:
    start = time.perf_counter_ns()
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    elapsed = (time.perf_counter_ns() - start) / 1_000_000
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return elapsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, nargs="+", default=[1, 6, 12])
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--reps", type=int, default=12)
    parser.add_argument("--json", type=Path, default=ROOT / "build" / "make_build_perf.json")
    args = parser.parse_args()
    if args.warmups < 0 or args.reps < 1 or any(value < 1 for value in args.jobs):
        parser.error("jobs and repetitions must be positive; warmups must be non-negative")

    commands = {f"full_j{jobs}": ["make", "-B", f"-j{jobs}", "compiler"] for jobs in args.jobs}
    commands["incremental_one_module"] = ["make", "compiler"]
    commands["noop"] = ["make", "compiler"]
    samples = {name: [] for name in commands}
    lexer = ROOT / "compiler" / "lexer.c"
    args.json.parent.mkdir(parents=True, exist_ok=True)

    for iteration in range(args.warmups + args.reps):
        full_names = [f"full_j{jobs}" for jobs in args.jobs]
        if iteration % 2:
            full_names.reverse()
        for name in full_names:
            elapsed = run(commands[name])
            if iteration >= args.warmups:
                samples[name].append(elapsed)

        # WSL on a Windows-mounted worktree can expose a few milliseconds of
        # timestamp skew between the host and the Linux clock.
        time.sleep(0.10)
        lexer.touch()
        incremental = run(commands["incremental_one_module"])
        noop = run(commands["noop"])
        if iteration >= args.warmups:
            samples["incremental_one_module"].append(incremental)
            samples["noop"].append(noop)

    report = {
        "metadata": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "warmups": args.warmups,
            "repetitions": args.reps,
            "note": "full builds use -B; incremental case touches only compiler/lexer.c then runs make compiler",
        },
        "results": {name: summarize(values) for name, values in samples.items()},
    }
    args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("GNU Make build benchmark (median / p90 / MAD, ms)")
    for name, values in report["results"].items():
        print(f"{name:24} {values['median_ms']:8.2f} / {values['p90_ms']:8.2f} / {values['mad_ms']:7.2f}")
    print(f"JSON report: {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
