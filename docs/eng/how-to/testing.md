# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: testing RBL

The current checkout has three complementary validation paths: the regression suite, standard-library smoke tests, and a differential runner for two backends.

## Regression tests

From the checkout root:

```bash
python3 tests/run_tests.py
```

In the verified version, the suite contains 59 direct RBL test cases. The runner executes each case through the active backend and compares the exit code, stdout and selected diagnostic fragments.

## Standard library and input

```bash
python3 tests/test_stdlib.py
```

This test separately checks global functions and standard-library namespaces, file I/O, and interactive `input(prompt)`.

## Run both checks through the CLI

```bash
python3 rbl.py test
```

This command runs both suites sequentially using paths inside the RBL repository. It does not automatically discover user tests in every RBL project directory.

## Compare Linux and Windows backends

On Windows, with both WSL and native MinGW-w64 configured:

```powershell
python tests\differential.py --a wsl --b native
```

The `--a` and `--b` options select the backend; common values are `wsl` and `native`. Specific test paths can be added after the options. The runner compares deterministic behavior; `cwd`, random values and other intentionally platform-dependent results require separate treatment.

## Rebuild the bootstrap toolchain

```bash
python3 rbl.py bootstrap
```

This forcibly rebuilds bootstrap compiler/runtime artifacts. It is not normally required: the driver tracks source files and reuses current binary components when possible.

## After changing the language

1. Add a minimal `.rbl` scenario under `tests/cases/`.
2. If the change affects the standard library or input, extend `tests/stdlib_smoke.rbl` and/or `tests/test_stdlib.py`.
3. Run both main test suites.
4. On Windows, run the differential test for native and WSL backends.
5. Update the relevant [reference](../reference/README.md) pages and limitations if the public contract changed.
