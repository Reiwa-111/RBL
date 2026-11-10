# last verified: v0.8.0 | 2026.11.09 | RUNTIME BENCHMARK RESULTS

## RBL 0.8.0 runtime benchmark

The current five-implementation benchmark compares three integer workloads. The values below are medians from two warmups and ten measured runs; the runner and child processes were pinned to one CPU.

| Workload | RBL 0.8.0 | C `-O2` | C++ `-O2` | CPython | Handwritten ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | **47.885 ms** | 25.258 ms | 25.183 ms | 6,483.543 ms | 24.465 ms |
| `calls` | **21.303 ms** | 13.499 ms | 13.306 ms | 852.290 ms | 13.526 ms |
| `branches` | **103.205 ms** | 53.116 ms | 54.099 ms | 6,772.371 ms | 53.408 ms |

### Ratios from the medians

| Workload | Python / RBL | RBL / C | RBL / C++ | RBL / ASM |
|---|---:|---:|---:|---:|
| `sum` | 135.398× | 1.896× | 1.901× | 1.957× |
| `calls` | 40.008× | 1.578× | 1.601× | 1.575× |
| `branches` | 65.621× | 1.943× | 1.908× | 1.932× |

The measurements ran under WSL2 Ubuntu 24.04 on an AMD Ryzen 5 5600 with Python 3.12.3, GCC/G++ 13.3.0 and GNU binutils 2.42. A separate RENA training process was active, so this is a controlled, CPU-pinned measurement under background load, not an idle-host reference. RBL build time for the three programs was 2,198.276 ms and is reported separately. The timer includes full child-process startup.

These are narrow synthetic integer workloads, not a general language ranking. Optimized C/C++ and specialized handwritten assembly were faster on these cases; CPython was slower. I/O, strings, containers, memory-intensive programs and application-sized workloads are not represented. The handwritten assembly uses Linux syscalls, so the comparison runs on Linux/WSL rather than native Windows.

For full environment details, methodology, workload definitions, caveats and reproduction instructions, see [`benchmarks/RESULTS.md`](../../../benchmarks/RESULTS.md). Compiler translation-unit and build measurements are separate and appear in [`benchmarks/PERFORMANCE_AUDIT.md`](../../../benchmarks/PERFORMANCE_AUDIT.md). The direct compiler-only 0.7.2-to-0.8.0 comparison is documented in [`benchmarks/RELEASE_PERFORMANCE.md`](../../../benchmarks/RELEASE_PERFORMANCE.md); it does not measure generated-program runtime.
