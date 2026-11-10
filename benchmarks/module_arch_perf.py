#!/usr/bin/env python3
"""Measure the cost of translation-unit boundaries in the RBL C compiler.

The amalgamated and modular builds use the exact same current C source bodies,
compiler, flags, and linker. The only intentional difference is whether the
seven implementation files are compiled as one translation unit or seven.
Compiler-latency cases must emit byte-identical assembly before timings count.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import shlex
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

from compiler_perf import workloads

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "common.c", "lexer.c", "ast.c", "parser.c", "sema.c", "codegen.c", "driver.c",
)
FLAGS = (
    "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic",
    "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wredundant-decls",
    "-Werror=missing-prototypes", "-Werror=redundant-decls",
    "-Werror=implicit-function-declaration",
)


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def stats(values: list[float]) -> dict[str, float | list[float]]:
    median = statistics.median(values)
    return {
        "median_ms": median,
        "p90_ms": percentile(values, 0.90),
        "mad_ms": statistics.median(abs(value - median) for value in values),
        "min_ms": min(values),
        "max_ms": max(values),
        "samples_ms": values,
    }


def run_timed(command: list[str]) -> float:
    start = time.perf_counter_ns()
    result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    elapsed = (time.perf_counter_ns() - start) / 1_000_000
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return elapsed


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--target", choices=("linux", "win"), default="linux")
    parser.add_argument("--warmups", type=int, default=5)
    parser.add_argument("--reps", type=int, default=30)
    parser.add_argument("--build-warmups", type=int, default=3)
    parser.add_argument("--build-reps", type=int, default=15)
    parser.add_argument("--json", type=Path, default=ROOT / "build" / "module_arch_perf.json")
    args = parser.parse_args()
    if args.warmups < 0 or args.reps < 1 or args.build_warmups < 0 or args.build_reps < 1:
        parser.error("warmups must be non-negative and repetitions must be positive")

    cc = shlex.split(args.cc)
    cc_version = subprocess.check_output([*cc, "--version"], text=True).splitlines()[0]
    build_dir = ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    args.json.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="rbl-module-perf-", dir=build_dir) as td:
        temp = Path(td)
        amalgamated_source = temp / "rblc_amalgamated.c"
        chunks = ['#include "rblc_internal.h"\n']
        for name in SOURCES:
            body = (ROOT / "compiler" / name).read_text(encoding="utf-8")
            body = body.replace('#include "rblc_internal.h"', "")
            chunks.append(f"\n/* ---- {name} ---- */\n{body}")
        amalgamated_source.write_text("".join(chunks), encoding="utf-8")

        common = [*cc, *FLAGS, "-I", str(ROOT / "compiler")]
        build_commands = {
            "amalgamated": [*common, str(amalgamated_source), "-o", str(temp / "rblc-amalgamated")],
            "modular": [*common, *(str(ROOT / "compiler" / name) for name in SOURCES), "-o", str(temp / "rblc-modular")],
        }
        build_samples: dict[str, list[float]] = {key: [] for key in build_commands}
        for iteration in range(args.build_warmups + args.build_reps):
            order = list(build_commands) if iteration % 2 == 0 else list(reversed(build_commands))
            for name in order:
                elapsed = run_timed(build_commands[name])
                if iteration >= args.build_warmups:
                    build_samples[name].append(elapsed)

        binaries = {name: temp / f"rblc-{name}" for name in build_commands}
        source_dir = temp / "sources"
        source_dir.mkdir(exist_ok=True)
        cases = workloads(source_dir)
        compile_samples: dict[str, dict[str, list[float]]] = {
            name: {key: [] for key in binaries} for name, _ in cases
        }
        output_hashes: dict[str, str] = {}
        for case_name, source in cases:
            outputs = {key: temp / f"{key}-{source.stem}.s" for key in binaries}
            for iteration in range(args.warmups + args.reps):
                order = list(binaries) if iteration % 2 == 0 else list(reversed(binaries))
                for name in order:
                    elapsed = run_timed([
                        str(binaries[name]), str(source), "-S", str(outputs[name]),
                        "--target", args.target,
                    ])
                    if iteration >= args.warmups:
                        compile_samples[case_name][name].append(elapsed)
            hashes = {
                name: hashlib.sha256(outputs[name].read_bytes()).hexdigest()
                for name in binaries
            }
            if len(set(hashes.values())) != 1:
                raise RuntimeError(f"assembly mismatch for {case_name}: {hashes}")
            output_hashes[case_name] = hashes["modular"]

        result = {
            "metadata": {
                "platform": platform.platform(),
                "machine": platform.machine(),
                "cpu": cpu_model(),
                "compiler": cc_version,
                "compiler_command": cc,
                "flags": FLAGS,
                "target": args.target,
                "build_warmups": args.build_warmups,
                "build_repetitions": args.build_reps,
                "compile_warmups": args.warmups,
                "compile_repetitions": args.reps,
                "definition": "amalgamated = same current source bodies in one TU; modular = seven TUs",
            },
            "build": {name: stats(values) for name, values in build_samples.items()},
            "compile": {
                case: {name: stats(values) for name, values in variants.items()}
                for case, variants in compile_samples.items()
            },
            "assembly_sha256": output_hashes,
        }
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")

        print(f"Host: {result['metadata']['cpu']} | {platform.platform()}")
        print(f"Compiler: {cc_version} | target={args.target}")
        print("Build time (median / p90 / MAD, ms):")
        for name, values in result["build"].items():
            print(f"  {name:12} {values['median_ms']:8.2f} / {values['p90_ms']:8.2f} / {values['mad_ms']:7.2f}")
        print("RBL compile time (median ms; generated assembly SHA-256 equal for every pair):")
        print("  workload                 amalgamated   modular   modular/amalgamated")
        for case, values in result["compile"].items():
            old = values["amalgamated"]["median_ms"]
            new = values["modular"]["median_ms"]
            print(f"  {case:24} {old:11.2f} {new:9.2f} {new / old:10.3f}x")
        print(f"JSON report: {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
