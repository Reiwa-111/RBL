# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL performance benchmark results

This file contains a measured snapshot, not a permanent ranking of programming languages. Re-run the benchmark when hardware, compiler versions, runtime sources or benchmark code change. The methodology is documented in [`README_3WAY.md`](README_3WAY.md); RBL-only measurements use a separate script.

## Five-implementation run — 2026-11-06

<small>Corrected run, 1 warmup and 5 measured executions per workload. Values below are medians.</small>

| Workload | RBL | C `-O2` | C++ `-O2` | CPython | Hand ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | **72.388 ms** | 26.389 ms | 26.500 ms | 6,897.844 ms | 28.789 ms |
| `calls` | **25.240 ms** | 14.318 ms | 13.394 ms | 911.629 ms | 14.078 ms |
| `branches` | **107.853 ms** | 52.381 ms | 54.095 ms | 8,189.178 ms | 62.340 ms |

### Ratios from these medians

| Workload | Python / RBL | RBL / C | RBL / C++ | RBL / hand ASM |
|---|---:|---:|---:|---:|
| `sum` | 95.290× | 2.744× | 2.732× | 2.514× |
| `calls` | 36.118× | 1.763× | 1.884× | 1.793× |
| `branches` | 75.929× | 2.059× | 1.994× | 1.730× |

For the last three columns, a ratio above 1 means RBL took that many times as long as the comparison implementation. For `Python / RBL`, a ratio above 1 means Python took that many times as long as RBL.

### Environment

- Host execution path: WSL2, Ubuntu 24.04 userland.
- Kernel: `Linux 6.6.87.2-microsoft-standard-WSL2`, x86-64; glibc 2.39.
- CPU model reported by `lscpu` inside WSL: `AMD Ryzen 5 5600 6-Core Processor` (6 cores / 12 threads reported).
- Python: 3.12.3.
- GCC and G++: 13.3.0.
- GNU binutils: assembler/linker 2.42.
- One warmup, five timed repetitions. Runner pins its process to the lowest allowed CPU affinity when supported.
- Timer: Python `perf_counter_ns()` surrounding the complete child-process execution.
- RBL compilation phase: **88.359 ms total for the three RBL benchmark programs**, reported separately from run time.

The CPU name above is the identifier exposed inside this WSL environment, not an independent hardware inventory. Results should be reproduced on the target machine before being used to compare other systems.

### Workloads

| Workload | Iterations | Operation |
|---|---:|---|
| `sum` | 100,000,001 | Integer accumulation for `i` from 0 through 100,000,000 inclusive. |
| `calls` | 10,000,000 | Calls to a one-line integer `inc` function. Native C/C++ functions are `noinline`. |
| `branches` | 100,000,000 | Integer update selected by the `i < 50,000,000` condition. |

Each program checks its result using its exit status. C and C++ workloads receive the iteration count as a runtime command-line argument so GCC/G++ cannot simply remove the entire fixed-bound sum/call loops at compile time.

### Interpretation and caveats

- RBL was about **36–95× faster than CPython** in these three synthetic loop workloads.
- The optimized C and C++ versions were about **1.76–2.74× faster than RBL**. The hand-written ASM baselines were about **1.73–2.51× faster than RBL**.
- These values do not imply that every RBL program has these ratios. The suite covers three narrow integer workloads, not I/O, memory-intensive workloads, strings, containers or application-sized programs.
- C/C++ were built with `-O2`. Disassembly showed that the call loop retains the `inc` call after the `noinline` change. The compiler may still legally strength-reduce arithmetic loops, and the branch workload may lower to conditional moves (`cmov`) rather than an actual conditional jump. This is a comparison of optimized implementations, not an instruction-for-instruction equivalence test.
- The full process startup is included in each timed sample; RBL build time is reported separately. Startup overhead is especially relevant to small workloads.
- Handwritten ASM is specialized code for these exact algorithms and uses Linux syscalls. The five-way runner therefore requires Linux or WSL and is not a native Windows benchmark.
- Medians are shown here; the raw log also includes each sample, means, minima, maxima and standard deviations.

### Reproducibility

Run from the repository root on Linux/WSL:

```bash
python3 benchmarks/compare_all.py --warmups 1 --reps 5
```

The corrected raw output for this snapshot is stored in `build/benchmark_five_way_corrected_2026-11-06.log` in the working checkout. An earlier run was discarded because GCC/G++ optimized away the fixed-bound C/C++ sum and call workloads; the benchmark sources were corrected to use runtime iteration counts and `noinline` for the call function before the results above were recorded.

## Portable RBL-only benchmark

The other suite is `benchmarks/run_benchmarks.py`, invoked with:

```bash
python3 rbl.py bench
```

It measures build time, whole-process run time, startup-baseline-adjusted `net`, and executable size for three RBL workloads. Its values are deliberately not combined with the five-implementation table: the programs and timing methodology are different.
