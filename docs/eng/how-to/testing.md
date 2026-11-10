# last verified: v0.8.0 | 2026.11.09 | EXAMPLE AND TEST DOCUMENTATION

## How-to: testing RBL

The current 0.8.0 checkout has complementary validation paths: the regression suite, standard-library smoke tests, example-source checks and a differential runner for two backends.

## Regression tests

From the checkout root:

```bash
python3 tests/run_tests.py
```

The RBL 0.8.0 suite contains 61 direct RBL test cases. The runner executes each case through the active backend and compares the exit code, stdout and selected diagnostic fragments. It also inspects generated assembly for GC safe-point placement: proven non-allocating scalar functions omit redundant checks, allocating functions retain them, and proven fast-integer loops contain no per-iteration `rbl_gc_safepoint` call. A separate code-generation regression test exercises the scalar/allocating distinction at runtime.

## Standard library and input

```bash
python3 tests/test_stdlib.py
```

This test separately checks global functions and standard-library namespaces, file I/O, and interactive `input(prompt)`.

## Build-cache isolation

```bash
python3 tests/test_build_cache.py
```

This test creates two temporary RBL sources with the same basename but different contents and deliberately makes one source older. It verifies that they receive separate build directories and execute their own outputs rather than reusing the other program's cached binary. The main `python rbl.py test` command includes this test.

## Linux Runtime call alignment

```bash
python3 tests/test_asm_call_alignment.py
```

The test assembles an instrumented copy of the Linux Runtime. Immediately before each textual assembly `call`, a register- and flags-preserving guard checks that `rsp` is 16-byte aligned as required by SysV AMD64. The current source has 158 textual call sites; the checked-in Runtime object contains 178 emitted call instructions after assembler expansion. It executes the direct regression suite and additional string-comparison/concatenation paths against that instrumented Runtime. On Windows, it uses WSL; without WSL it reports a skip. This checks Runtime-to-helper/libc calls, separately from the generated-fast-function entry alignment test in the Runtime ABI suite.

## Runtime ABI compatibility

```bash
python3 tests/test_runtime_abi.py
```

This compiles a small native probe and generated RBL executables. It verifies ABI layout/offsets, major-to-symbol versioning, C/compiler versus Linux-assembly tag parity, compatible minor/patch versions and descriptor extensions, rejected incompatible layouts/features, link-time rejection when the versioned ABI symbol is absent, the mismatch diagnostic/exit code, startup rejection before entering RBL `main`, and SysV stack alignment for nested fast-integer/boolean calls. The main `python rbl.py test` command includes this test.

## Managed-string ownership and collector OOM

```bash
python3 tests/test_string_ownership.py
```

This native probe checks stack roots, strings stored as list elements and dictionary keys/values, unreachable-string reclamation, list/dictionary heap accounting after capacity growth and GC recomputation, deterministic allocation failures in initial/growing/compacting string registries, the container registry, mark stack, survivor vector and pre-sweep replacement registry, failed-collection rollback/retry (including unchanged container-byte accounting), and idempotent internal release. It also checks that a native C stack-local borrow remains rooted after the caller's copy is cleared. The main `python rbl.py test` command includes this test.

## Repeated input/file reads under GC pressure

```bash
python3 tests/test_string_io_gc.py
```

This integration test performs 96 reads of a 64-KiB file and 12,000 `input()` calls, alternating prompted and unprompted input. It also verifies that empty/`s` formatting preserves arbitrary bytes when no width is requested, while width formatting rejects invalid UTF-8 instead of miscounting it. It checks retained early/late results and prompt behavior while the allocations cross the collector's automatic thresholds. The main `python rbl.py test` command includes this test.

## Compiler diagnostics

```bash
python3 tests/test_diagnostics.py
```

The 63-case suite contains 45 invalid-source diagnostics, 8 valid-source controls, 4 expected runtime-error cases and 6 malformed-UTF-8 byte-sequence tests. It checks exact file/line/column and stage labels, source excerpts and exact caret alignment, UTF-8 columns including multibyte and combining characters, tabs, LF/CRLF/bare-CR handling, numeric separators, invalid Unicode escapes, embedded NUL and malformed UTF-8 classes, unterminated and multiline literals, nested f-string expressions/comments, format-spec syntax and limits, explicit rejection of dynamic/nested specs both at the start and after width, valid format parsing including alignment characters used as fill, runtime type mismatches, integer-precision errors and invalid precision with `s`, parser namespace/constant errors and code-generation call constraints. It runs through the selected backend. `python3 rbl.py test` and `make test` include this suite.

The direct regression case `tests/cases/fstrings.rbl` checks integer bases including the signed 64-bit minimum, float conversion styles and precision through 17 digits, width including the 1,000,000-code-point limit, alignment/fill (including alignment characters used as fill and explicit zero fill), Unicode code-point width, empty/default interpolation and `s` conversion across all runtime value tags. `tests/cases/gc_strings.rbl` also exercises formatted string allocation through repeated collections. `tests/cases/gc_fast_loop.rbl` checks the non-allocating fast-loop path while retaining a managed string root. The syntax reference defines the complete format contract.

## Run the full test set through the CLI

```bash
python3 rbl.py test
```

This command runs the complete checkout suite sequentially: direct regression, standard-library/input, build-cache isolation and safe cleanup, instrumented Linux Runtime call-alignment, Runtime ABI, GC safe-point code generation, managed-string ownership/OOM, repeated input/read_file GC stress, streamed tool output, bundled-toolchain and manifest checks, Linux launcher, POSIX metadata, wx filesystem-security and worker-lifecycle checks, DPI static checks, and compiler diagnostics. Windows additionally runs registry-policy and save-cleanup tests. Platform-specific checks may report explicit skips when their prerequisites are unavailable. It does not automatically discover user tests in every RBL project directory.

`make test` runs a smaller compiler/Runtime subset (direct regression, standard library, cache isolation, call alignment, Runtime ABI, ownership, I/O-under-GC, toolchain manifest and diagnostics). It is not equivalent to `python rbl.py test`.

## Compiler lookup example

The compiler refactor is an internal architecture change; separately, RBL 0.8.0 adds bounded f-string format specifications but no source-level module system. This ordinary RBL example exercises repeated variable/function resolution and string reuse; its hash indexes remain private compiler implementation details:

```bash
python3 rbl.py check examples/compiler/lookup_demo.rbl
python3 rbl.py run examples/compiler/lookup_demo.rbl
```

The [example notes](../../../examples/compiler/README.md) document the expected output. This is a functional demonstration, not a collision stress test or a timing benchmark.

To check all example sources from the repository root on Windows PowerShell:

```powershell
Get-ChildItem examples -Recurse -Filter *.rbl | ForEach-Object {
    python rbl.py check $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "RBL check failed: $($_.FullName)" }
}
```

Run the tracked complete-corpus checker from the repository root:

```bash
python3 tests/check_corpus.py
```

It checks every source under `examples/` and `tests/`, verifies the seven expected negative fixtures, and assembles all positive outputs. On Windows, use the native backend for PE; run `RBL_TOOLCHAIN=wsl python tests/check_corpus.py` in PowerShell to verify the Linux/ELF backend. `python3 tests/check_documentation.py` checks all relative Markdown links, heading fragments and RU/EN page inventories/hierarchy.

## Compare Linux and Windows backends

On Windows, with both WSL and native MinGW-w64 configured:

```powershell
python tests\differential.py --a wsl --b native
```

The default selection is 83 source cases: 68 files under `tests/cases/`, the standard-library smoke source, 8 RBL files directly under `examples/` and 6 directly under `benchmarks/`. It does not recursively execute every example. The runner compares exit code/stdout/stderr for 61 successful deterministic programs and 11 expected runtime-error programs (including their non-zero exit code), requires 7 expected-negative fixtures to be rejected with matching compiler diagnostics, and requires 4 random/platform-dependent programs to build and exit successfully on both backends without comparing their output. Each executable runs in a private temporary working directory, so relative file outputs do not overwrite repository files. The `--a` and `--b` options select the backend; common values are `wsl` and `native`. Specific paths can be added after the options.

For a focused cross-platform comparison of compiler diagnostics (including source positions, excerpts, malformed UTF-8 and nested f-string cases), run:

```powershell
python tests\differential_diagnostics.py
```

## Rebuild the bootstrap toolchain

```bash
python3 rbl.py bootstrap
```

This forcibly rebuilds bootstrap compiler/runtime artifacts. It is not normally required: the driver tracks source files and reuses current binary components when possible.

## After changing the language or compiler

1. Add a minimal `.rbl` scenario under `tests/cases/` and register it in the `CASES` table in `tests/run_tests.py` with the expected exit code, stdout and stderr. That runner uses an explicit list; it does not auto-discover new case files.
2. If the change affects the standard library or input, extend `tests/stdlib_smoke.rbl` and/or `tests/test_stdlib.py`.
3. Run `python3 rbl.py test` and `python3 tests/check_corpus.py`.
4. On Windows, run the program and diagnostic differential suites against WSL.
5. Update the relevant [reference](../reference/README.md) pages and limitations if the public contract changed.
6. For compiler-internal changes, rebuild both platform backends, run the full corpus and differential checks, and update the [architecture](../explanation/architecture.md) and [optimization](../explanation/optimization.md) explanations without presenting private compiler internals as RBL language features.

For an additional GCC static-analysis pass over all seven compiler translation units and the three shared Runtime C modules, run `bash tests/run_gcc_analyzer.sh` on Linux/WSL or `make analyze`. It uses `-fanalyzer` and strict prototype checks, writing intermediate objects only to a temporary directory.
