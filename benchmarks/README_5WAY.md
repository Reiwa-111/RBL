# last verified: v0.8.0 | 2026.11.09 | FIVE-WAY BENCHMARK

## Five-implementation performance benchmark

`compare_all.py` compares three integer workloads across five implementations:

1. **RBL** — compiled through the direct x86-64 assembly backend.
2. **C** — equivalent C workload compiled with `-O2`.
3. **C++** — equivalent C++ workload compiled with `-O2`.
4. **CPython** — equivalent Python workload.
5. **Handwritten ASM** — hand-authored x86-64 assembly linked as a Linux executable.

## Run

From the repository root on Linux:

```bash
python3 benchmarks/compare_all.py
```

Or through the project CLI:

```bash
python3 rbl.py bench5way --warmups 2 --reps 10
```

The CLI command `bench5way` invokes this five-implementation comparison. The old `bench3way` name remains available as a compatibility alias. Defaults are two warmups and ten timed runs per implementation. The handwritten ASM programs issue Linux syscalls directly, so the comparison runs on Linux (including WSL), not as a native Windows executable.

## Workloads

| Workload | Work |
|---|---|
| `sum` | Accumulate integers from 0 through 100,000,000 inclusive. |
| `calls` | Execute 10 million calls to a one-line integer increment function. |
| `branches` | Run 100 million integer updates, split by a conditional at 50 million. |

Each implementation validates the expected result through its exit status. The C and C++ iteration counts are passed at runtime. In the call workload, `inc` is marked `noinline` so the optimized machine code retains the intended function call. These safeguards prevent a compiler from deleting the complete fixed-bound sum/call loop as a constant result.

## Measurement model

The runner uses `perf_counter_ns()` around the complete child process execution, so process startup is included. It runs configured warmups, then records timed repetitions and reports median, mean, minimum, maximum, standard deviation and raw samples. RBL build time is measured separately. Where available, the runner pins itself and child processes only when `--cpu` is explicitly supplied; by default it leaves scheduler affinity unchanged. Pinning to the lowest allowed CPU implicitly can select an overloaded logical CPU and distort release comparisons.

## Optimization caveat

C and C++ are optimized with GCC/G++ `-O2`. This is intentionally a comparison against optimized native code, not a comparison of identical instruction sequences. The compiler may strength-reduce arithmetic loops or replace predictable branches with conditional moves. Inspect disassembly before making claims about the cost of an individual source-level construct.

## Results

The latest recorded measurement in this checkout is in [`RESULTS.md`](RESULTS.md). It includes the environment, medians, computed ratios, caveats and reproduction command. Do not treat it as a permanent ranking; rerun on the machine and toolchain being evaluated.

## Other benchmark

`benchmarks/run_benchmarks.py` is a separate RBL-only suite and can be run through `python3 rbl.py bench` using the active backend. It reports build time, whole-process run time, startup-adjusted net time and executable size. Its values are not directly interchangeable with this five-way comparison.
