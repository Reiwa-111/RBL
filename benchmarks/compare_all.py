#!/usr/bin/env python3
"""High-resolution RBL vs CPython vs hand-written x86-64 ASM benchmark.

Timed region is the whole process execution. All three workloads perform the same
algorithm and self-check their result via exit status. RBL programs are compiled
through the direct ASM backend; no C compiler is involved in user-program builds.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
PURE_DIR = ROOT / "benchmarks" / "pure_asm"
RBL_SRCS = {
    "sum": ROOT / "benchmarks" / "rbl_bench_sum.rbl",
    "calls": ROOT / "benchmarks" / "rbl_bench_calls.rbl",
    "branches": ROOT / "benchmarks" / "rbl_bench_branches.rbl",
}
PY_SRCS = {
    "sum": ROOT / "benchmarks" / "python" / "sum.py",
    "calls": ROOT / "benchmarks" / "python" / "calls.py",
    "branches": ROOT / "benchmarks" / "python" / "branches.py",
}
PURE_SRCS = {name: PURE_DIR / f"{name}.s" for name in RBL_SRCS}
# Native mirrors of the same three workloads, compiled with -O2.
C_SRCS = {name: ROOT / "benchmarks" / "c" / f"{name}.c" for name in RBL_SRCS}
CPP_SRCS = {name: ROOT / "benchmarks" / "cpp" / f"{name}.cpp" for name in RBL_SRCS}

@dataclass
class Stats:
    samples: list[float]
    @property
    def mean(self) -> float:
        return statistics.fmean(self.samples)
    @property
    def median(self) -> float:
        return statistics.median(self.samples)
    @property
    def stdev(self) -> float:
        return statistics.stdev(self.samples) if len(self.samples) > 1 else 0.0
    @property
    def min(self) -> float:
        return min(self.samples)
    @property
    def max(self) -> float:
        return max(self.samples)

def pin_current_process() -> None:
    if hasattr(os, "sched_setaffinity"):
        try:
            cpus = os.sched_getaffinity(0)
            if cpus:
                os.sched_setaffinity(0, {min(cpus)})
                return
        except OSError:
            pass

def run_checked(cmd: list[str]) -> float:
    start = time.perf_counter_ns()
    p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    elapsed = (time.perf_counter_ns() - start) / 1_000_000_000.0
    if p.returncode != 0:
        err = p.stderr.decode(errors="replace").strip()
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(cmd)}\n{err}")
    return elapsed

def stats_for(cmd: list[str], warmups: int, reps: int) -> Stats:
    for _ in range(warmups):
        run_checked(cmd)
    samples = [run_checked(cmd) for _ in range(reps)]
    return Stats(samples)

def build_rbl() -> dict[str, Path]:
    sys.path.insert(0, str(ROOT / "tools"))
    import rbltool
    out: dict[str, Path] = {}
    for name, src in RBL_SRCS.items():
        rc, out_text, info = rbltool.build(src, {})
        if rc:
            raise RuntimeError(f"RBL build failed for {name}:\n{out_text}")
        out[name] = ROOT / "build" / src.stem / src.stem
    return out

def build_pure_asm(tmp: Path) -> dict[str, Path]:
    tmp.mkdir(parents=True, exist_ok=True)
    out: dict[str, Path] = {}
    for name, src in PURE_SRCS.items():
        obj = tmp / f"{name}.o"
        exe = tmp / f"pure_{name}"
        subprocess.run(["as", "--64", str(src), "-o", str(obj)], check=True, cwd=ROOT)
        subprocess.run(["ld", str(obj), "-o", str(exe)], check=True, cwd=ROOT)
        out[name] = exe
    return out

def build_native(tmp: Path) -> tuple[dict[str, Path], dict[str, Path]]:
    """Compile the C and C++ mirrors of the same workloads with -O2."""
    tmp.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("cc") or shutil.which("gcc")
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cc or not cxx:
        raise RuntimeError("a C compiler (cc/gcc) and a C++ compiler (c++/g++) are required")
    c_out: dict[str, Path] = {}
    cpp_out: dict[str, Path] = {}
    for name in RBL_SRCS:
        c_exe = tmp / f"c_{name}"
        subprocess.run([cc, "-O2", str(C_SRCS[name]), "-o", str(c_exe)], check=True, cwd=ROOT)
        c_out[name] = c_exe
        cpp_exe = tmp / f"cpp_{name}"
        subprocess.run([cxx, "-O2", str(CPP_SRCS[name]), "-o", str(cpp_exe)], check=True, cwd=ROOT)
        cpp_out[name] = cpp_exe
    return c_out, cpp_out

def fmt(ms: float) -> str:
    return f"{ms:9.3f} ms"

def speedup(slow: Stats, fast: Stats) -> float:
    return slow.median / fast.median

def print_stats(label: str, stats: Stats) -> None:
    print(
        f"  {label:<8} median={fmt(stats.median*1000)}  mean={fmt(stats.mean*1000)} "
        f"min={fmt(stats.min*1000)}  max={fmt(stats.max*1000)}  sd={fmt(stats.stdev*1000)}"
    )
    print("           " + " ".join(f"{x*1000:.3f}" for x in stats.samples) + " ms")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=10, help="timed runs per implementation")
    ap.add_argument("--warmups", type=int, default=2, help="warmup runs per implementation")
    args = ap.parse_args()
    if args.reps < 1 or args.warmups < 0:
        ap.error("--reps must be >= 1 and --warmups must be >= 0")

    if os.name == "nt":
        # The hand-written baseline programs issue Linux syscalls directly and are
        # linked with GNU ld and no C runtime, so they cannot become PE images, and
        # the whole comparison is only meaningful on one host anyway.
        print("This 3-way benchmark needs a Linux host (or WSL): the hand-written ASM")
        print("baseline programs are Linux syscall programs linked without libc.")
        print("Use one of:")
        print("  wsl -d Ubuntu-24.04 -- bash -lc 'cd <repo> && python3 benchmarks/compare_all.py'")
        print("  rbl bench        # benchmarks/run_benchmarks.py works on both backends")
        return 2

    pin_current_process()
    print("RBL Studio high-resolution 3-way benchmark")
    print(f"Python: {platform.python_version()} | OS: {platform.platform()}")
    print(f"Warmups: {args.warmups} | Timed runs: {args.reps}")
    print("Timer: Python perf_counter_ns around the complete process execution")
    print("RBL path: RBL -> direct x86-64 ASM -> as -> ld")
    print("Pure ASM path: hand-written x86-64 ASM -> as -> ld")

    t0 = time.perf_counter_ns()
    rbl_bins = build_rbl()
    build_ms = (time.perf_counter_ns() - t0) / 1_000_000
    pure_bins = build_pure_asm(ROOT / "build" / "benchmark_pure_asm")
    c_bins, cpp_bins = build_native(ROOT / "build" / "benchmark_native")
    print(f"\nRBL build phase: {build_ms:.3f} ms total")

    results: dict[str, dict[str, Stats]] = {}
    cmds = {}
    for name in RBL_SRCS:
        cmds[name] = {
            "RBL": [str(rbl_bins[name])],
            "Python": [PYTHON, str(PY_SRCS[name])],
            "ASM": [str(pure_bins[name])],
            "C": [str(c_bins[name])],
            "C++": [str(cpp_bins[name])],
        }

    ratios: list[tuple[str, float, float]] = []
    for name in ("sum", "calls", "branches"):
        print(f"\n== {name} ==")
        results[name] = {}
        for impl in ("RBL", "C", "C++", "Python", "ASM"):
            results[name][impl] = stats_for(cmds[name][impl], args.warmups, args.reps)
            print_stats(impl, results[name][impl])
        rbl = results[name]["RBL"]
        py = results[name]["Python"]
        asm = results[name]["ASM"]
        py_over_rbl = speedup(py, rbl)
        rbl_over_asm = speedup(rbl, asm)
        py_over_asm = speedup(py, asm)
        ratios.append((name, py_over_rbl, rbl_over_asm))
        print(f"  Speedup Python / RBL      : {py_over_rbl:8.3f}x")
        print(f"  Gap RBL / hand ASM       : {rbl_over_asm:8.3f}x")
        print(f"  Speedup Python / hand ASM: {py_over_asm:8.3f}x")

    print("\n== Summary (median) ==")
    print(f"{'Workload':<10} {'RBL':>10} {'C -O2':>10} {'C++ -O2':>10} {'Python':>12} {'ASM':>10} {'RBL/ASM':>9} {'Py/RBL':>8}")
    print("-" * 92)
    for name in ("sum", "calls", "branches"):
        r = results[name]
        print(
            f"{name:<10} {r['RBL'].median*1000:8.3f}ms {r['C'].median*1000:8.3f}ms "
            f"{r['C++'].median*1000:8.3f}ms {r['Python'].median*1000:10.3f}ms {r['ASM'].median*1000:8.3f}ms "
            f"{speedup(r['RBL'], r['ASM']):8.3f}x {speedup(r['Python'], r['RBL']):7.2f}x"
        )
    print("\nNote: these are workload-specific runtime measurements, not universal language rankings.")
    print("Process startup is included equally in all three measurements; compiler build time is reported separately.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
