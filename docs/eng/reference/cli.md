# last verified: v0.8.1 | 2026.11.17 | CURRENT DOCUMENTATION REVIEW

## Reference: RBL CLI

The main entry point is the `rbl.py` Python script in the RBL repository. It does not install a global `rbl` command. Examples use `python3` on Linux/WSL and `python` in PowerShell.

## Commands

| Command | Purpose |
|---|---|
| `rbl.py version` | Prints the version from the root `VERSION` file. |
| `rbl.py doctor` | Checks the toolchain through the low-level driver. |
| `rbl.py bootstrap` | Forces a rebuild of bootstrap compiler/runtime artifacts. |
| `rbl.py env` | Prints the RBL root, project root and Python executable. |
| `rbl.py new NAME [--path DIR]` | Creates a new project. NAME must be 1-64 ASCII letters, digits, `_` or `-`, starting with a letter or digit. |
| `rbl.py init` | Initializes the current directory; its directory name must satisfy the same project-name rule. |
| `rbl.py check [FILE]` | Runs frontend, analysis and code generation without invoking the assembler or linker. |
| `rbl.py build [FILE]` | Compiles, assembles and links an executable. |
| `rbl.py run [FILE]` | Builds and runs a program. |
| `rbl.py asm [FILE]` | Runs the assembly preview and displays generated assembly. |
| `rbl.py test` | Runs compiler/Runtime regression and diagnostics plus build-cache/cleanup, GC, toolchain, launcher, filesystem-security, worker-lifecycle and DPI checks; Windows adds registry/save-cleanup tests. |
| `rbl.py bench` | Runs the portable RBL-only benchmark suite. |
| `rbl.py bench5way [--reps N] [--warmups N]` | Runs the five-implementation Linux/WSL comparison; `bench3way` is a legacy alias. |
| `rbl.py diff [--a MODE] [--b MODE] [PATH ...]` | Compares two backends. Defaults to `wsl` and `native`. |
| `rbl.py fmt [FILE] [--check]` | Formats a file or checks whether formatting is already applied. |
| `rbl.py doc [FILE] [-o FILE]` | Generates a simple function-declaration table. |
| `rbl.py clean` | Safely removes only the compiler cache at `build/cache/sources/`; preserves `build/output/`, other build evidence and user data. |

The `FILE` argument for `check`, `build`, `run`, `asm`, `fmt` and `doc` is optional. When omitted, the CLI uses `package.entry` from the manifest, or `src/main.rbl` by default. If that path does not exist and a root-level `main.rbl` exists in the current project, it is used instead.

## Examples

Linux:

```bash
python3 rbl.py new demo
cd demo
python3 ../rbl.py check
python3 ../rbl.py build
python3 ../rbl.py run
```

These relative paths assume `demo` is a direct child of the RBL Studio checkout. For projects elsewhere, use the `RBL_STUDIO_ROOT` environment-variable form in the generated project README. `rbl.py test` runs the RBL Studio repository regression suite; it is not a project test runner.

Windows PowerShell:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

## What `check` does

`check` is not parser-only validation: it processes the source, performs analysis and generates assembly into a temporary stream. It does not invoke the assembler or linker, produce an executable, or save a normal assembly output file.

## `bench` and `bench5way`

These commands invoke different scripts:

- `bench` → `benchmarks/run_benchmarks.py`, RBL-only measurements available through the active backend.
- `bench5way` → `benchmarks/compare_all.py`, five implementations and a Linux syscall assembly baseline; Linux/WSL only.
- `bench3way` remains a backwards-compatible alias for `bench5way`.

The current CLI does not accept `--reps` or `--warmups` for `bench`; those options belong to `bench5way`. See the [benchmarking guide](../how-to/benchmarking.md).

## Low-level driver

For diagnostics and scripting, use `tools/rbltool.py`. It provides `doctor`, `check`, `build`, `run` and `asm`. The higher-level `rbl.py` adds project creation/initialization, tests, benchmarks, formatting, API listing and cleanup. For compiler-stage labels, source coordinates and diagnostic test coverage, see the [diagnostics reference](diagnostics.md).
