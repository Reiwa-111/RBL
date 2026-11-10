# last verified: v0.8.0 | 2026.11.09 | FINAL FIVE-WAY BENCHMARK

## RBL 0.8.0 performance benchmark results

This report records the current RBL 0.8.0 five-implementation benchmark. It is a workload-specific measurement, not a permanent ranking of programming languages. Re-run it when hardware, compiler versions, runtime sources or benchmark code change. The methodology is documented in [`README_5WAY.md`](README_5WAY.md); RBL-only measurements use a separate script.

## Five-implementation runtime benchmark

**Measurement scope:** RBL 0.8.0 with Runtime ABI 1.2.0 startup checks, the current SysV stack-alignment implementation and the non-allocating fast-loop safe-point optimization. Two warmups and ten measured executions per implementation/workload; runner and child processes restricted to CPU 11. Values are medians.

| Workload | RBL 0.8.0 | C `-O2` | C++ `-O2` | CPython | Handwritten ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | **47.885 ms** | 25.258 ms | 25.183 ms | 6,483.543 ms | 24.465 ms |
| `calls` | **21.303 ms** | 13.499 ms | 13.306 ms | 852.290 ms | 13.526 ms |
| `branches` | **103.205 ms** | 53.116 ms | 54.099 ms | 6,772.371 ms | 53.408 ms |

### Ratios from these medians

| Workload | Python / RBL | RBL / C | RBL / C++ | RBL / ASM |
|---|---:|---:|---:|---:|
| `sum` | 135.398× | 1.896× | 1.901× | 1.957× |
| `calls` | 40.008× | 1.578× | 1.601× | 1.575× |
| `branches` | 65.621× | 1.943× | 1.908× | 1.932× |

For the native/ASM columns, a ratio above 1 means RBL took that many times as long. For `Python / RBL`, a ratio above 1 means CPython took that many times as long.

### Environment and caveats

- Host: WSL2, Ubuntu 24.04, Linux 6.6.87.2, x86-64; AMD Ryzen 5 5600 (6 cores / 12 threads).
- Python 3.12.3; GCC/G++ 13.3.0; GNU binutils 2.42.
- Two warmups and ten timed repetitions; the benchmark process and its children were pinned to CPU 11.
- A separate RENA training process was active in WSL. The benchmark was CPU-pinned and repeatable at ten samples, but the host was not globally isolated; treat this as a controlled, contended host measurement rather than an idle-machine reference.
- RBL build phase: **2,198.276 ms total for the three RBL benchmark programs**, reported separately.
- The timer includes complete child-process startup. All programs returned success and validate their computed result through exit status.

The complete terminal output and individual samples were retained in the working build log when this measurement was recorded. Raw logs are not part of the source distribution.

## Workloads

| Workload | Iterations | Operation |
|---|---:|---|
| `sum` | 100,000,001 | Sum integers from 0 through 100,000,000 inclusive. |
| `calls` | 10,000,000 | Calls to a one-line integer `inc` function; C/C++ use `noinline`. |
| `branches` | 100,000,000 | Update an integer according to the condition `i < 50,000,000`. |

Each program validates its result through its exit status. C/C++ iteration counts are passed at runtime so GCC/G++ cannot delete the complete measured workload as a constant result.

## Interpretation and limitations

- In these three synthetic integer-loop workloads, RBL was approximately **40–135× faster than CPython**.
- Optimized C/C++ were approximately **1.58–1.94× faster than RBL**; the specialized handwritten assembly was approximately **1.58–1.96× faster**.
- These results do not generalize to every RBL program: the suite does not cover I/O-heavy, memory-intensive, string/container-heavy or application-sized workloads.
- C/C++ were built with `-O2`. The compiler can transform loops and replace conditional branches with conditional moves while preserving results. This compares optimized programs, not identical instruction sequences.
- Process startup is included in every measurement; RBL build time is reported separately. This matters especially for short programs.
- Handwritten assembly is specialized for these algorithms and uses Linux syscalls; the five-way suite runs on Linux/WSL, not as a native Windows benchmark.

## Reproduce

Run from the repository root on Linux/WSL:

```bash
python3 benchmarks/compare_all.py --warmups 2 --reps 10
```

The benchmark reports medians and individual sample statistics. See [`README_5WAY.md`](README_5WAY.md) for the timing model, native workload safeguards and interpretation notes.

## Separate RBL-only benchmark

The RBL-only suite is invoked with:

```bash
python3 rbl.py bench
```

It measures build time, whole-process run time, startup-baseline-adjusted `net` time and executable size for RBL workloads. Its values are not interchangeable with the five-implementation comparison above.
