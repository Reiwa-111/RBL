# last verified: v0.8.0 | 2026.11.09 | RELEASE PERFORMANCE AUDIT

# RBL 0.8.0 release-candidate performance audit

## Compiler-only comparison: 0.7.2 vs 0.8.0

Measured on WSL2 / Linux x86-64 with the 0.7.2 compiler executable retained from
the repository baseline and a freshly rebuilt 0.8.0 compiler. The harness invokes
the compilers directly: no cache, assembler, linker or program execution is part
of these timings. Each case uses 5 warmups, 30 measured runs, alternating compiler
order and CPU affinity pinned to logical CPU 10. The output-size column records
generated assembly bytes; hashes are expected to differ across releases.

| Workload | 0.7.2 median | 0.8.0 median | Speedup (0.7.2 / 0.8.0) | Assembly bytes 0.7.2 / 0.8.0 |
|---|---:|---:|---:|---:|
| 100 variables | 8.14 ms | 8.22 ms | 0.99x | 31,110 / 31,469 |
| 500 variables | 14.35 ms | 13.30 ms | 1.08x | 156,341 / 156,700 |
| 1,000 variables | 24.17 ms | 19.19 ms | 1.26x | 314,844 / 315,203 |
| 2,000 variables | 51.08 ms | 31.41 ms | 1.63x | 640,844 / 641,203 |
| 500 unique strings | 17.47 ms | 16.03 ms | 1.09x | 226,300 / 239,659 |
| 1,000 unique strings | 32.22 ms | 24.63 ms | 1.31x | 457,301 / 483,660 |
| 2,000 unique strings | 78.58 ms | 44.09 ms | 1.78x | 931,301 / 983,660 |
| 4,000 unique strings | 203.81 ms | 74.54 ms | 2.73x | 1,879,301 / 1,983,660 |
| 100 functions | 12.09 ms | 12.55 ms | 0.96x | 178,824 / 180,783 |
| 300 functions | 24.75 ms | 24.15 ms | 1.02x | 540,824 / 545,983 |
| 600 functions | 47.19 ms | 42.56 ms | 1.11x | 1,086,961 / 1,096,920 |

Interpretation:

- The small 100-variable and 100-function cases are approximately 1% and 4%
  slower, respectively. This is close to the measurement spread and should not
  be presented as a proven broad regression.
- Larger symbol/string workloads improve substantially, consistent with the
  hash-indexed compiler data structures introduced in 0.8.0.
- 0.8.0 output is larger because it adds the versioned Runtime ABI descriptor,
  ABI startup check, corrected GC stack-base setup, and safer stack alignment for
  expressions that may call helpers. These are deliberate correctness costs.

## Code-generation optimization made during this audit

The previous code generator emitted `rbl_gc_safepoint` before every statement,
including statically proven integer/Boolean scalar statements that cannot
allocate. It also used a 16-byte temporary stack slot even when the right-hand
operand was a leaf and could not call anything.

The candidate now:

- omits safe points only for expressions proven to be non-allocating integer or
  Boolean scalar work; allocating expressions and unknown calls retain checks;
- retains loop-entry and loop-condition checks, and retains the existing
  per-iteration policy for loops that can allocate or mutate pointer roots;
- uses an 8-byte `push`/`pop` only when the right-hand integer operand is proven
  to be a leaf; call-capable expressions still reserve 16 bytes for SysV ABI
  alignment.

`tests/test_gc_safepoint_codegen.py` checks the emitted assembly and executes a
scalar-plus-allocation fixture. The complete RBL test suite passed after this
change, including the existing GC pressure, managed-string ownership and
157-call-site Linux stack-alignment tests.

## Runtime comparison caveat

`benchmarks/compare_release_runtime.py` builds identical source files with each
release's own compiler and Runtime, then measures process execution with
alternating order and CPU affinity. The first comparison run was contaminated by
heavy unrelated CPU work on the WSL host and showed unstable timings (including
large run-to-run jumps). The generated hot integer-loop bodies were inspected:
the loop instructions are equivalent; 0.8.0 adds one ABI check and one GC safe
point before entering the loop, not a per-iteration call. Therefore the noisy
runtime measurements are not accepted as evidence of a 0.8.0 runtime slowdown.
The first repeat used an implicitly selected logical CPU (the harness previously
pinned to the lowest allowed CPU when `--cpu` was omitted). This is not a reliable
way to select an idle core. The harness now leaves affinity unchanged unless
`--cpu` is explicit and includes an empty-program workload to quantify process
startup. Repeat runtime measurements on an otherwise idle host before publishing
a release performance claim.

## Reproduction

Compiler-only:

```bash
python3 benchmarks/compiler_perf.py \
  --baseline build/perf/rblc-baseline \
  --candidate bin/linux-x86_64/rblc-asm \
  --target linux --warmups 5 --reps 30 --cpu 10
```

Full compiler + Runtime execution:

```bash
python3 benchmarks/compare_release_runtime.py \
  --baseline-root build/perf/rbl-0.7.2 \
  --candidate-root . --warmups 5 --reps 30 --cpu 10
```
