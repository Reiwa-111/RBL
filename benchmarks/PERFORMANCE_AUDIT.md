# last verified: v0.8.0 | 2026.11.09 | COMPILER MODULARITY AND BUILD AUDIT

## Scope

This report measures the cost and build-system behavior of the seven-module RBL 0.8.0 compiler. It separates translation-unit overhead from the lookup algorithms and records the current build profile. It does not compare language/compiler releases or claim generated-program runtime speedups.

## Environment and method

- CPU: AMD Ryzen 5 5600, 6 cores / 12 threads.
- OS: Ubuntu 24.04 under WSL2, Linux 6.6.87.2.
- C compiler: GCC 13.3.0; target Linux x86-64 ELF.
- Worktree: Windows-mounted source directory under WSL.
- Compiler-latency timing uses `perf_counter_ns`; object/build measurements include process startup and linking where applicable.
- The final module-isolation run uses three warmups and ten measured compiler runs, plus two warmups and twelve full-build samples. The process was pinned to CPU 11.
- The final Makefile build run uses two warmups and twelve measured samples in a disposable source sandbox. A separate RENA training process was active; treat the results as controlled but contended measurements.

Absolute timings are specific to this host, WSL environment and mounted worktree. The purpose is to compare equivalent source/build shapes, not to provide universal compiler timings.

## Translation-unit isolation

The same final RBL 0.8.0 compiler source bodies were compiled in two shapes: one generated/amalgamated translation unit and the seven checked-in modules. Compiler, flags, input bytes, target and link mode were kept identical. Every paired build emitted byte-identical assembly; all 11 SHA-256 pairs matched.

Median compiler execution time in milliseconds:

| Workload | One translation unit | Seven modules | Modular / one TU |
|---|---:|---:|---:|
| 100 variables | 10.30 | 10.34 | 1.004× |
| 500 variables | 15.11 | 14.81 | 0.980× |
| 1,000 variables | 21.93 | 21.46 | 0.979× |
| 2,000 variables | 36.43 | 35.77 | 0.982× |
| 500 unique strings | 17.65 | 18.00 | 1.019× |
| 1,000 unique strings | 26.98 | 27.20 | 1.008× |
| 2,000 unique strings | 49.62 | 49.60 | 1.000× |
| 4,000 unique strings | 92.06 | 91.26 | 0.991× |
| 100 functions | 16.03 | 15.60 | 0.973× |
| 300 functions | 31.60 | 31.12 | 0.985× |
| 600 functions | 52.83 | 52.61 | 0.996× |

The geometric-mean modular/one-TU latency ratio is **1.013×**, with a per-workload range of 0.983×–1.146×. This is close to parity for the measured compiler workloads. Translation-unit boundaries do not change generated assembly on these deterministic inputs.

Full serial C compilation costs more with seven modules: 1,718.26 ms versus 1,522.90 ms for the amalgamated source (+12.8%). This is a build-time effect, not a generated RBL program runtime penalty.

## Makefile build behavior

The Makefile compiles one object per compiler source, links the objects, tracks `rblc_internal.h` for each object and tracks `VERSION` for `driver.o`. Editing one `.c` source recompiles only that object; changing the private header rebuilds all dependent objects.

Final disposable-sandbox GNU Make measurements (milliseconds):

| Operation | Median | p90 | MAD |
|---|---:|---:|---:|
| Full build, `make -B -j1 compiler` | 1,772.70 | 2,046.96 | 91.47 |
| Full build, `make -B -j6 compiler` | 797.81 | 843.47 | 16.04 |
| Full build, `make -B -j12 compiler` | 805.88 | 875.52 | 31.50 |
| Touch lexer only, then `make compiler` | 583.08 | 639.14 | 23.78 |
| No source change, `make compiler` | 28.87 | 30.91 | 1.14 |

On this host, `-j6` reduced full-build time by about 55% versus serial `-j1`; `-j12` did not improve on `-j6`. Incremental and no-op builds were substantially cheaper than full builds. These timings were measured in a disposable copy so forced rebuilds did not overwrite repository binaries or object files.

To repeat safely, use a disposable source copy/worktree because the build benchmark invokes `make -B`, rewrites compiler build outputs and touches the copied lexer timestamp:

```bash
python3 benchmarks/make_build_perf.py --jobs 1 6 12 --warmups 2 --reps 12
```

## Current compiler hot paths

GCC `-pg` / gprof was used on a stress input of approximately 3.84 MB containing 50,000 variable assignments, 50,000 unique string assignments and 5,000 functions. Ten runs were merged to reduce sampling noise (about 1.26 seconds of aggregate sampled CPU time, 10 ms sampling interval).

| Function | Sampled self time |
|---|---:|
| `lex_all` | 22.22% |
| `pool_intern` | 17.46% |
| `var_find` | 13.49% |
| `emit_program` | 7.94% |
| `rbl_hash_string` | 7.14% |
| `pool_index_insert` | 7.14% |
| `var_index_insert` | 6.35% |
| `emit_stmt_asm` | 3.97% |

Lexing, string interning, variable lookup, code emission and index maintenance account for the largest sampled costs in this stress profile. These values provide a profiling baseline for future targeted changes; they are not a general-purpose workload distribution.

## Conclusions

- The seven-module compiler has near-parity frontend latency with the equivalent one-translation-unit build on the measured workloads.
- Module boundaries preserve byte-identical generated assembly for all 11 paired isolation inputs.
- Serial C compilation is slower with multiple translation units, while object-level parallel and incremental builds reduce the measured build time.
- Runtime execution performance is a separate measurement and is documented in [`RESULTS.md`](RESULTS.md).

## Related benchmark commands

```bash
python3 benchmarks/module_arch_perf.py --warmups 3 --reps 10 --build-warmups 2 --build-reps 12
python3 benchmarks/make_build_perf.py --jobs 1 6 12 --warmups 2 --reps 12
```

Run the Makefile benchmark only in a disposable source copy/worktree.
