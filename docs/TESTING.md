# Testing

The suite is designed around the semantics of the original Rust interpreter, not merely compiler success.

Current direct ASM regression count: **57 cases**.

Memory is verified separately from correctness, because a leaking collector still
prints the right answer. The check is a hard address-space limit:

```bash
# 1M iterations, each allocating a list and a dictionary (~320 MB without a collector)
$ ulimit -v 122880 && ./build/gc_pressure/gc_pressure    # must print 1000000 and exit 0
```

`tests/cases/gc_pressure.rbl` is the same program with 50 000 iterations, so it can live in
the normal suite; the 1M variant stays a manual check because it takes seconds.

Four more files live in `tests/cases` but are deliberately not in the runner list, because they must be rejected by the compiler and diagnostics embed the host file path: `literals_bad.rbl` (a malformed digit separator), `strings_bad_nul.rbl` (a NUL escape, which RBL forbids because strings are NUL-terminated), `fstrings_bad.rbl` (an unterminated `{` in an f-string) and `braces_rejected.rbl` (braces are no longer a block delimiter), plus `break_outside.rbl` (`break` outside a loop). All of them are covered by `tests/differential.py`, which checks that the Linux and Windows builds of the compiler produce the same diagnostic.

Coverage includes:

- arithmetic and operator precedence
- floats
- strings and concatenation
- equality / comparison
- `not`, `and`, `or`
- functions and recursion
- argument arity timing
- parameter type errors
- return from inside loops
- inclusive/exclusive ranges
- `set` duplicate errors
- `let` unknown/type errors
- non-short-circuit evaluation
- duplicate functions
- Unicode identifiers
- `Unit`
- `warn.log` / `error.log`

Run:

```bash
python3 tests/run_tests.py
```

The test runner reports every case independently, exits non-zero on the first aggregate failure, and runs every program with a timeout (default 180 s, override with `RBL_TIMEOUT`). A program that regresses into an endless loop therefore fails instead of blocking the suite.

The suite is backend-agnostic: it builds and runs through whatever toolchain is active. Point it at a specific one with the `RBL_TOOLCHAIN` environment variable:

```powershell
$env:RBL_TOOLCHAIN = 'native'; python tests\run_tests.py
$env:RBL_TOOLCHAIN = 'wsl';    python tests\run_tests.py
```

## Differential runner

`tests/differential.py` (see `docs/WINDOWS.md`) compiles and runs the whole corpus through two backends and compares exit code, stdout and stderr byte for byte:

```bash
python3 tests/differential.py --a wsl --b native
```

This is the check that keeps the hand-written Linux assembly runtime and the Windows C runtime in agreement.

Benchmarks:

```bash
python3 benchmarks/run_benchmarks.py
```

Benchmarks intentionally report both compilation cost and executable runtime so compiler work is not confused with language runtime speed.


Pure ASM comparison:

```bash
./benchmarks/compare_pure_asm.sh
```

This builds three hand-written x86-64 Linux programs with GNU `as` + `ld` and compares them with equivalent RBL native programs and CPython. It is deliberately a separate benchmark from the semantic regression suite.


## v0.7 standard-library + syntax regression

`tests/cases/regalloc_loop.rbl` exercises register-held loop state and fast integer function calls. It is included in the 32-case core suite, together with six regressions for defects found in the code audit:

- `tests/cases/fast_loop_labels.rbl` — an optimized loop whose body emits labels, followed by another `if` in the same function (used to produce duplicate assembler labels);
- `tests/cases/fast_loop_nested_branches.rbl` — the same class of defect with nested branches, two optimized loops and several following `if`s (checks every label class, and asserts that the fast path is really taken);
- `tests/cases/selfrec_int_analysis.rbl` — (mutually) recursive single-return `int` functions (used to crash the compiler with SIGSEGV even when never called);
- `tests/cases/loop_inc_overflow.rbl` — an inclusive range that ends at `INT64_MAX` with an int-only body (used to loop forever instead of reporting `integer overflow`);
- `tests/cases/range_outside_for.rbl` — a range value outside a `for` header (generated code used to reference the runtime-internal label `.Lmsg_range_value`, so the program never linked);
- `tests/cases/for_non_range.rbl` — a `for` header that is not a range (same code path).

## Known coverage gap

Float output is only asserted for values that are exactly representable (`10f`, `2f`, `3.5`, `0.25`), where C `%.17g` and the Rust reference's shortest round-trip form agree. Values such as `0.1` or `1.0 / 3.0` still differ from the reference; see the limitations section of `README.md`.
