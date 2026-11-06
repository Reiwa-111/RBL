# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL command-line tooling

The root `rbl` Bash wrapper is intended for Linux and WSL shells; because it is not stored with the executable bit in this repository, invoke it as `bash ./rbl`. `rbl.py` is the cross-platform Python entry point and is the clearest option for scripts. The repository does not install a global `rbl` command: run the wrapper from the checkout root, add an executable wrapper to your own `PATH`, or invoke `rbl.py` by path from a project directory.

## Commands

| Command | Purpose |
|---|---|
| `rbl new NAME [--path DIR]` | Create a project. |
| `rbl init` | Initialize the current directory. |
| `rbl check [FILE]` | Run compiler validation and code generation without writing an output assembly file or linking. |
| `rbl build [FILE]` | Build a native executable. |
| `rbl run [FILE]` | Build and execute a program. |
| `rbl asm [FILE]` | Emit assembly for inspection. |
| `rbl test` | Run the regression suite and the separate standard-library smoke test. |
| `rbl bench` | Run the portable build/run benchmark suite. |
| `rbl bench3way [--reps N] [--warmups N]` | Run the five-implementation comparison (RBL, C, C++, CPython and hand-written ASM) on Linux. The command name is retained for compatibility. |
| `rbl diff [--a MODE] [--b MODE] [PATH ...]` | Compare two backend modes. |
| `rbl fmt [FILE] [--check]` | Apply or check the source formatter. |
| `rbl doc [FILE] [-o FILE]` | Generate a simple API listing from function declarations. |
| `rbl clean` | Remove the current project's `build/` directory. |
| `rbl doctor` | Diagnose the active toolchain. |
| `rbl env` | Print project and Python environment information. |
| `rbl version` | Print the CLI version. |
| `rbl bootstrap` | Force rebuilding the bootstrap compiler/runtime objects. |

`rbl bench` does not currently accept the `--reps` and `--warmups` options. Those options belong to `rbl bench3way`.

## Project manifest

A new project contains an `rbl.toml` manifest:

```toml
[package]
name = "hello"
version = "0.7.1"
entry = "src/main.rbl"
```

The current CLI reads `name`, `version` and `entry`. It does not yet resolve dependencies, fetch packages or compile imported source modules.

## Build pipeline

The driver in `tools/rbltool.py` locates the appropriate compiler, assembler and linker, creates build output under `build/<source-name>/`, and executes the generated program using the selected backend. On Windows it can use native MinGW-w64 tools or WSL.

`rbl check` invokes the compiler's full source-to-assembly validation path but does not invoke the assembler or linker. It should not be described as parse-only validation.

## Low-level driver

For scripts and toolchain debugging:

```bash
python3 tools/rbltool.py doctor
python3 tools/rbltool.py check examples/test.rbl
python3 tools/rbltool.py build examples/test.rbl
python3 tools/rbltool.py run examples/test.rbl
python3 tools/rbltool.py asm examples/test.rbl
```

The low-level driver exposes `doctor`, `check`, `build`, `run` and `asm`. The project CLI adds project creation/initialization, tests, benchmarks, formatting, API listing and cleanup.
