# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Benchmark results: 6 November 2026

This is a recorded measurement snapshot, not a permanent language ranking. The complete methodology, environment details, caveats and reproduction command are documented in [`benchmarks/RESULTS.md`](../../../benchmarks/RESULTS.md).

## Five-implementation comparison

<small>Corrected run: 1 warmup and 5 timed measurements per workload. Values below are medians.</small>

| Workload | RBL | C `-O2` | C++ `-O2` | CPython | Handwritten ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | **72.388 ms** | 26.389 ms | 26.500 ms | 6,897.844 ms | 28.789 ms |
| `calls` | **25.240 ms** | 14.318 ms | 13.394 ms | 911.629 ms | 14.078 ms |
| `branches` | **107.853 ms** | 52.381 ms | 54.095 ms | 8,189.178 ms | 62.340 ms |

### Median ratios

| Workload | Python / RBL | RBL / C | RBL / C++ | RBL / handwritten ASM |
|---|---:|---:|---:|---:|
| `sum` | 95.290× | 2.744× | 2.732× | 2.514× |
| `calls` | 36.118× | 1.763× | 1.884× | 1.793× |
| `branches` | 75.929× | 2.059× | 1.994× | 1.730× |

For the last three ratios, a value above 1 means RBL took that many times as long as the comparison implementation. For `Python / RBL`, it means Python took that many times as long.

## Environment

- WSL2, Ubuntu 24.04 userland; kernel `Linux 6.6.87.2-microsoft-standard-WSL2`, glibc 2.39.
- CPU reported inside WSL: `AMD Ryzen 5 5600 6-Core Processor` (6 cores / 12 threads reported by the environment).
- Python 3.12.3; GCC/G++ 13.3.0; GNU binutils 2.42.
- The runner pinned itself to one CPU when `sched_setaffinity` was available.
- `perf_counter_ns()` measured complete child-process execution, including startup.
- Total build phase for the three RBL programs: **88.359 ms**, reported separately from runtime.

## Workloads

| Workload | Iterations | Operation |
|---|---:|---|
| `sum` | 100,000,001 | Sum integers from 0 through 100,000,000 inclusive. |
| `calls` | 10,000,000 | Calls to a one-line integer `inc` function; C/C++ use `noinline`. |
| `branches` | 100,000,000 | Update an integer according to the condition `i < 50,000,000`. |

Each program validates its result through its exit status. C/C++ iteration counts are passed at runtime so GCC/G++ cannot delete complete loops as compile-time constants.

## Interpretation and limitations

- In these three synthetic loop workloads, RBL was approximately **36–95× faster than CPython**.
- Optimized C/C++ were approximately **1.76–2.74× faster than RBL**; handwritten ASM was approximately **1.73–2.51× faster**.
- These results do not generalize to every RBL program: the tests do not cover I/O, memory-intensive work, strings, containers or application-sized programs.
- C/C++ were built with `-O2`. The compiler can transform loops and replace conditional branches with `cmov` while preserving results. This compares optimized programs, not instruction sequences one-for-one.
- Process startup is included in every measurement; RBL build time is reported separately. This matters especially for short programs.
- Handwritten ASM is specialized for these particular algorithms and uses Linux syscalls; the benchmark requires Linux or WSL, not native Windows.

The first run was discarded because GCC/G++ removed the C/C++ `sum` and `calls` loops through compile-time optimization. The results above were obtained after passing the iteration count as a runtime argument and adding `noinline` to the `inc` function.
