# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL — Reiwa Bat Language

RBL is a compiled programming language and native development toolchain. The repository includes a C-based compiler that emits x86-64 assembly directly, platform runtimes, RBL Studio, a project CLI, tests and benchmarks.

**Repository version:** `0.7.2` (`VERSION`). The CLI reads this value; newly generated user projects have their own manifest version, initialized to `0.1.0`.

## Current scope

- **Targets:** Linux x86-64 (ELF64) and Windows x86-64 (PE/COFF).
- **Compiler:** UTF-8 lexer, recursive-descent parser, AST, static analysis, conservative scalar/loop optimizations and direct assembly code generation.
- **Runtime:** hand-written assembly platform runtime on Linux; C platform runtime on Windows; shared C container runtime on both targets.
- **Language:** integers, floats, booleans, strings, `Unit`, `null`, functions, recursion, ranges, lists, dictionaries, tuples, record-like `struct` declarations, control flow and a built-in standard library.
- **Tools:** `rbl` CLI, RBL Studio, regression and differential runners, and benchmark tools.

RBL does not generate C source for user programs. On Windows, MinGW-w64 GCC builds the Windows runtime object and acts as the linker driver; generated user-program code is still assembly.

## Quick start

For prerequisites and the first working program, start with the [RBL documentation](docs/README.md) and its [first-program tutorial](docs/eng/tutorial/first-program.md) or [Russian tutorial](docs/ru/tutorial/first-program.md).

A minimal RBL program:

```rbl
func main() (
    print("Hello from RBL")
)
```

Save it as `hello.rbl`, then run it from the repository checkout:

```bash
python3 rbl.py run hello.rbl
```

Linux (from the repository checkout):

```bash
python3 rbl.py doctor
python3 rbl.py run examples/test.rbl
python3 tests/run_tests.py
```

Windows PowerShell:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
python tests\run_tests.py
```

For a new project created inside the checkout:

```bash
python3 rbl.py new hello
cd hello
python3 ../rbl.py run
```

The repository does not install a global `rbl` command. Invoke the Bash wrapper as `bash ./rbl` from the checkout root, or use `python3 rbl.py`; add a separately executable wrapper to `PATH` only if you want a short global command. On Windows or from another directory, invoke the driver by its path, for example `python C:\path\to\RBL\rbl.py run C:\path\to\hello.rbl`. The project manifest is `rbl.toml`; its current supported `[package]` fields are `name`, `version` and `entry`. It is not yet a dependency manifest or package-manager configuration.

## Compilation overview

```text
RBL source
  -> lexer / parser / AST
  -> static analysis and local optimizations
  -> x86-64 assembly
  -> GNU assembler
  -> platform runtime + system libraries
  -> native executable
```

Linux links the generated object with GNU `ld`, the assembly runtime object and the shared container-runtime object. Windows links with MinGW-w64 GCC, the Windows runtime object and the same container-runtime implementation compiled for Windows.

## Documentation

Start at [`docs/README.md`](docs/README.md) to choose English or Russian. Both language editions are organized into tutorial, how-to, reference and explanation sections following Diátaxis. The [English language syntax reference](docs/eng/reference/syntax.md), [Russian language syntax reference](docs/ru/reference/syntax.md), [standard-library reference](docs/eng/reference/standard-library.md), [CLI reference](docs/eng/reference/cli.md), [architecture explanation](docs/eng/explanation/architecture.md) and [performance results](benchmarks/RESULTS.md) describe the current implementation.

## Current limitations

RBL currently targets x86-64 only. Source-level imports and multi-file compilation, package/dependency resolution, exceptions, closures, async execution, a bytecode VM, a dedicated RBL IR/CFG and a source-level debugger are not implemented. `struct` instances currently use dictionary storage rather than a dedicated runtime object type. The container collector does not reclaim all string allocations. See the [full limitations reference](docs/eng/reference/limitations.md) and [design directions](docs/eng/explanation/design.md) for details.

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
