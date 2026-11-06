# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: benchmarking performance

The repository contains two distinct benchmark suites. Do not combine their results: they answer different questions and use different workloads.

## Portable RBL-only benchmark

```bash
python3 rbl.py bench
```

This runs through the active backend and reports build time, total process runtime, an estimated `net` value (run time minus the empty-program startup baseline), the runtime range and executable size. For small workloads, process startup may dominate the measurement.

## Five-implementation comparison

On Linux or in WSL:

```bash
python3 benchmarks/compare_all.py --warmups 2 --reps 10
```

Through the CLI:

```bash
python3 rbl.py bench3way --warmups 2 --reps 10
```

Despite its historical name, `bench3way` currently compares **five** implementations: RBL, C `-O2`, C++ `-O2`, CPython and handwritten x86-64 assembly. GNU `as`, GNU `ld`, a C compiler and a C++ compiler are required. Native Windows execution is not supported because the assembly baseline uses Linux syscalls.

## Make measurements comparable

1. Compare results on the same machine, in the same OS/environment, using the same toolchain versions.
2. Do not change workloads or optimization flags between runs.
3. Use more warmups and timed repetitions for published results; record all parameters.
4. Record CPU, OS/kernel, Python, GCC/G++, binutils, backend, warmups/repetitions and raw samples.
5. Repeat the measurements after changing benchmark code or the environment.

The runner includes child-process startup in the measured time; RBL build time is reported separately. The current five-way runner pins its own process to one CPU when the OS exposes `sched_setaffinity`.

## Interpret the comparison

C and C++ are built with `-O2`. The compiler is allowed to transform loops while preserving the result: for example, it can replace a conditional jump with `cmov` or simplify arithmetic. Iteration counts are passed to native programs at runtime so GCC/G++ cannot delete certain workloads entirely; the call workload marks `inc` as `noinline`.

This is a comparison of specific workloads, not a universal ranking of programming languages. Different algorithms, data structures, type distributions, I/O, memory usage, optimization settings and startup overhead can change the relative results.

## Recorded result

The measurement snapshot from 6 November 2026, its methodology and raw sample values are documented in [`benchmarks/RESULTS.md`](../../../benchmarks/RESULTS.md). Interpretation and caveats are covered in the [performance explanation](../explanation/performance.md).
