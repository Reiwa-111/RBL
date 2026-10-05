# RBL Studio benchmark suite

The benchmark runner now supports a reproducible three-way comparison:

```text
RBL -> direct x86-64 ASM -> as -> ld
Python -> CPython
hand-written ASM -> as -> ld
```

Run on the target machine instead of treating these figures as universal claims:

```bash
./benchmarks/compare_pure_asm.sh
```

For high-resolution timing, the runner uses `perf_counter_ns()` and reports median/mean/min/max/standard deviation plus:

- `Python / RBL`: how many times the Python workload took longer than RBL.
- `RBL / ASM`: how many times RBL took longer than hand-written ASM.
- `Python / ASM`: how many times Python took longer than hand-written ASM.

The workloads are:

| Workload | Work |
|---|---|
| `sum` | 100,000,000 integer additions |
| `calls` | 10,000,000 integer-only function calls |
| `branches` | 100,000,000 branch iterations |

The timing includes complete process startup for all three implementations. RBL source compilation is measured separately and is not folded into runtime.

Older v0.3/v0.4 snapshots remain in the release history below; those values were collected on the development machine and are not universal language rankings.

## Previous development snapshot

| Workload | RBL v0.3 | RBL v0.4 | Hand-written ASM | Python |
|---|---:|---:|---:|---:|
| 100M integer additions | ~0.52 s | ~0.15–0.20 s | ~0.02 s | ~4.7–5.3 s |
| 10M integer-only function calls | ~0.14 s | ~0.02–0.03 s | ~0.01 s | ~0.68–0.75 s |
| 10M integer branches | ~0.08 s | ~0.02 s | below `/usr/bin/time`'s 10 ms resolution | ~0.54–0.68 s |
