# last verified: v0.8.0 | 2026.11.11 | GATE G PROJECT WORKFLOW RECHECK

## How-to: project workflow

## Create a new project

From the repository checkout root:

```bash
python3 rbl.py new my_app
```

Choose another destination:

```bash
python3 rbl.py new my_app --path ../my_app
```

The target directory must be missing or empty. NAME must be 1-64 ASCII letters, digits, `_` or `-`, starting with a letter or digit. A new project contains:

```text
my_app/
├── rbl.toml
├── README.md
├── src/
│   └── main.rbl
├── tests/
└── benchmarks/
```

## Initialize an existing directory

```bash
python3 /path/to/RBLStudio/rbl.py init
```

This creates `rbl.toml`, `src/`, `tests/`, `benchmarks/` and a starter `src/main.rbl` when they do not already exist. The current directory name must satisfy the project-name rule above. An existing manifest is never overwritten.

## Build and run

The CLI is not installed globally. Set `RBL_STUDIO_ROOT` to your RBL Studio checkout, then run from the project directory:

```bash
export RBL_STUDIO_ROOT="/path/to/RBLStudio"
python3 "$RBL_STUDIO_ROOT/rbl.py" check src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" build src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" run src/main.rbl
python3 "$RBL_STUDIO_ROOT/rbl.py" asm src/main.rbl
```

Windows PowerShell:

```powershell
$env:RBL_STUDIO_ROOT = "C:\path\to\RBLStudio"
python "$env:RBL_STUDIO_ROOT\rbl.py" check src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" build src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" run src\main.rbl
python "$env:RBL_STUDIO_ROOT\rbl.py" asm src\main.rbl
```

You can pass a source path explicitly after the command. Without one, the CLI uses `package.entry` from `rbl.toml`, defaulting to `src/main.rbl`. If that file does not exist, the driver also checks for `main.rbl` in the project root.

## Format a source file

```bash
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl --check
```

The formatter normalizes block indentation based on parentheses at the beginning/end of lines. It is not a configurable full-language formatter and does not enforce every style convention.

## Generate a simple function listing

```bash
python3 /path/to/RBLStudio/rbl.py doc src/main.rbl -o target/api.md
```

`rbl doc` is a simple function-declaration listing that extracts names, parameters and return annotations using a text pattern. It is not a full documentation generator, type analyzer or standard-library documentation generator.

## Clean build outputs

The checkout separates disposable artifacts by purpose:

- `build/cache/sources/` holds compiled user programs and downloaded upstream
  source archives. It is expensive to rebuild.
- `build/output/` holds IDE, worker, Cargo and bootstrap outputs built from the
  current checkout.

```bash
python3 /path/to/RBLStudio/rbl.py clean   # compiler cache only
make clean-cache                          # same cache, from the checkout root
make clean-build                          # IDE/worker/bootstrap outputs
make clean                                # alias for clean-build
```

`rbl.py clean` removes only the compiler source cache at `build/cache/sources/`.
It does not remove `build/output/`, the current project build directory or other
build evidence. `make clean` never removes the cache; use `make clean-cache` for
that.

## Inspect the environment

```bash
python3 /path/to/RBLStudio/rbl.py doctor
python3 /path/to/RBLStudio/rbl.py env
```

`doctor` checks toolchain availability. `env` prints the RBL installation root, current project directory and Python executable.

## What does `rbl.py test` do?

`rbl.py test` runs the following RBL Studio repository suites sequentially. It does not run the current project's `tests/` directory:

The current runner includes:

- Direct compiler regression, standard-library/input, build-cache isolation, generated-project scaffold and safe-cleanup tests.
- Linux Runtime call-alignment instrumentation, Runtime ABI compatibility/startup, GC safe-point code generation, managed-string ownership/OOM and repeated I/O under GC pressure.
- Streaming output, bundled-toolchain and manifest checks, Linux launcher, POSIX metadata, wx filesystem-security and worker-lifecycle checks, plus DPI static checks.
- Windows-only registry-policy and save-cleanup checks, followed by compiler source-diagnostics tests.

The exact execution order and platform gates are defined by `run_tests()` in `rbl.py`; individual contracts are documented in the [testing guide](testing.md).

It is not a universal test runner for arbitrary projects and does not automatically execute files under `my_app/tests/`. For the individual test contracts and backend-differential checks, see the [testing guide](testing.md) and [CLI reference](../reference/cli.md).
