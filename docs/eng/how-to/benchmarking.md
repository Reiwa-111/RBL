# last verified: v0.8.0 | 2026.11.09 | BENCHMARKING GUIDE

## How-to: measure RBL performance

The repository contains two distinct benchmark suites. Keep their results separate because their workloads and timing models differ.

## Portable RBL-only benchmark

```bash
python3 rbl.py bench
```

This suite uses the active backend and reports build time, total process runtime, an estimated `net` value (runtime minus the empty-program startup baseline), runtime range and executable size. Process startup can dominate short workloads.

## Five-implementation comparison

Run on Linux or in WSL:

```bash
python3 benchmarks/compare_all.py --warmups 2 --reps 10
```

The same suite is available through the CLI:

```bash
python3 rbl.py bench5way --warmups 2 --reps 10
```

It compares RBL, C `-O2`, C++ `-O2`, CPython and handwritten x86-64 assembly across three synthetic integer workloads. GNU `as`, GNU `ld`, a C compiler and a C++ compiler are required. The assembly baseline uses Linux syscalls, so this suite runs on Linux/WSL, not as a native Windows executable. The old command name `bench3way` remains available as a backwards-compatible alias.

The runner times complete child-process execution, including startup, and reports RBL build time separately. It pins the benchmark process to one CPU when the OS exposes `sched_setaffinity`.

## Compiler modularity and build measurements

To compare one translation unit with the seven compiler modules, run in WSL/Linux:

```bash
python3 benchmarks/module_arch_perf.py --warmups 3 --reps 10 --build-warmups 2 --build-reps 12
```

To measure full, parallel, incremental and no-op Makefile builds:

```bash
python3 benchmarks/make_build_perf.py --jobs 1 6 12 --warmups 2 --reps 12
```

**Run the Makefile benchmark only in a disposable source copy/worktree.** It invokes `make -B`, rewrites compiler build outputs and touches the copied `compiler/lexer.c` timestamp. Do not run it in a working checkout whose generated binaries or object files must be preserved.

Both commands measure compiler/build behavior, not generated-program runtime. Full methodology and recorded measurements are in [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md).

## Make measurements comparable

1. Use the same machine, OS/environment and toolchain versions.
2. Keep workloads and optimization flags unchanged between runs.
3. Use enough warmups and timed repetitions; record all parameters.
4. Record CPU, OS/kernel, Python, GCC/G++, binutils, backend and raw samples.
5. Repeat after changing benchmark code, compiler sources or the environment.

C and C++ are built with `-O2`; the compiler may transform loops while preserving results. Runtime iteration counts and `noinline` on the call workload prevent the native compiler from deleting the intended measured work entirely. This is a comparison of specific programs, not a universal language ranking.

## Recorded results

The current five-implementation RBL 0.8.0 runtime measurements, environment and interpretation are in [`benchmarks/RESULTS.md`](../../../benchmarks/RESULTS.md). Compiler modularity, translation-unit overhead and build timings are in [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md).
