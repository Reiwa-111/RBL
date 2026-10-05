# RBL vs Python vs pure x86-64 ASM

`compare_pure_asm.sh` is now the unified three-way benchmark runner.

It measures the same algorithm in three implementations:

1. **RBL** — RBL Studio direct x86-64 ASM backend, then GNU `as` + `ld`.
2. **Python** — CPython implementation of the same loop/function/branch workload.
3. **Pure ASM** — hand-written x86-64 assembly, then GNU `as` + `ld`.

The timed region is the complete process execution. Each workload validates its result through exit status. The benchmark uses Python's `perf_counter_ns()` instead of `/usr/bin/time`, so sub-centisecond differences remain visible.

By default it performs 2 warmups and 10 timed samples per implementation and reports median, mean, min, max, standard deviation, and ratios.

Run:

```bash
./benchmarks/compare_pure_asm.sh
```

For a quick smoke run:

```bash
./benchmarks/compare_pure_asm.sh --warmups 1 --reps 3
```

Current workloads:

- `sum`: 100,000,000 integer additions.
- `calls`: 10,000,000 integer-only function calls.
- `branches`: 100,000,000 branch iterations.

The benchmark intentionally compares runtime only. RBL compilation time is reported separately because Python has no directly equivalent source-to-native compilation stage in this test.
