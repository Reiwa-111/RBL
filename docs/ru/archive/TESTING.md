# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Testing

The primary regression runner is `tests/run_tests.py`. Its current `CASES` table contains **59 direct RBL test programs**. The runner reports each case and returns a non-zero status if any case fails.

```bash
python3 tests/run_tests.py
```

The project CLI command `rbl test` runs this suite and then runs the separate standard-library/input smoke test in `tests/test_stdlib.py`.

## Coverage

The direct suite covers arithmetic, floats, strings, escapes, interpolation, assignments, conditions, range and condition loops, functions, recursion, evaluation order, parameter checks, modulo, extended math, `switch`, collections, tuples, structs, `null`, identity, GC pressure and expected diagnostics.

The test count is a source-level count of entries in `tests/run_tests.py`, not a count of every file under `tests/cases`. Some files are diagnostic fixtures and are exercised by the differential runner rather than the successful-output runner.

## Differential testing

`tests/differential.py` builds and executes the same corpus through two selected toolchain modes and compares build status, exit status, stdout and stderr. Its default modes are `wsl` and `native`. A meaningful Linux-versus-Windows comparison requires running on Windows with both WSL and native MinGW-w64 configured; on Linux, the driver does not create a Windows backend, so selecting these mode names does not create a cross-OS comparison.

```powershell
python tests\differential.py --a wsl --b native
```

The corpus includes test cases, `tests/stdlib_smoke.rbl`, examples and benchmark programs. Programs containing random calls or `rbl.fs.cwd()` are marked platform-dependent and compared by exit status only; exact output is not expected to match those cases.

## Memory pressure

`tests/cases/gc_pressure.rbl` exercises container allocation and collection with 50,000 iterations as part of the normal suite. The documentation also describes a separate 1,000,000-iteration manual check under a process address-space limit. That manual run is not counted among the 59 regression cases.

The container collector reclaims list/tuple/dictionary allocations. It does not currently reclaim all string allocations, so this test must not be interpreted as proof that every runtime allocation is collected.

## Timeouts and diagnostics

The toolchain driver applies a program timeout (default 180 seconds, configurable through `RBL_TIMEOUT`). This prevents a regression into an endless loop from hanging the test process indefinitely.

## What differential testing proves

The differential runner compares the two target/runtime paths. It does not compare the current compiler against the historical Rust interpreter. It is a consistency check for the current Linux and Windows implementations, not a complete language-conformance proof.
