# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

# RBL — Reiwa Bat Language

**RBL is a native compiled programming language and development toolchain.** It includes a C bootstrap compiler that generates x86-64 assembly directly, platform runtimes for Linux and Windows, a graphical IDE, a project-oriented command-line interface, regression tests and benchmark tools.

<p align="center">
  <strong>Current release: <a href="https://github.com/Reiwa-111/RBL/releases/tag/v0.7.2">RBL 0.7.2</a></strong>
  ·
  <a href="docs/README.md">Documentation</a>
  ·
  <a href="docs/eng/tutorial/first-program.md">First program</a>
  ·
  <a href="docs/ru/README.md">Русская документация</a>
</p>

RBL compiles user programs to native executables. **RBL source code is not translated into C**: the compiler parses the language, performs static analysis and selected optimizations, then emits x86-64 assembly. The C compiler is used to build parts of the compiler/runtime toolchain, not to transpile user programs.

---

## Contents

- [Project overview](#project-overview)
- [Current release and scope](#current-release-and-scope)
- [Highlights](#highlights)
- [Quick start](#quick-start)
- [A small RBL program](#a-small-rbl-program)
- [Examples](#examples)
- [Language overview](#language-overview)
- [Compiler architecture](#compiler-architecture)
- [Targets and toolchains](#targets-and-toolchains)
- [Runtime and memory](#runtime-and-memory)
- [Standard library](#standard-library)
- [RBL Studio](#rbl-studio)
- [Command-line interface](#command-line-interface)
- [Project layout and manifest](#project-layout-and-manifest)
- [Testing](#testing)
- [Benchmarks](#benchmarks)
- [Documentation](#documentation)
- [Development roadmap](#development-roadmap)
- [Known limitations](#known-limitations)
- [Repository structure](#repository-structure)
- [License](#license)

---

## Project overview

RBL brings together a small programming language, a native compiler/runtime and an integrated development environment.

| Component | Role |
|---|---|
| RBL language | Expressions, functions, control flow, strings and collection types |
| Native compiler | C bootstrap compiler; direct x86-64 assembly generation |
| Linux runtime | Hand-written x86-64 assembly runtime |
| Windows runtime | C platform runtime built for Windows |
| Shared container runtime | C implementation of lists, dictionaries, tuples and container collection |
| RBL Studio | Optional Python/Tkinter graphical IDE |
| Project CLI | Project creation, checking, building, running, formatting and diagnostics |
| Tests and benchmarks | Regression, standard-library, backend differential and performance tooling |

The compiler frontend and code generator are shared between targets where possible. Platform-specific runtime functions, object formats, assembler/linker invocation and system integration are handled by the corresponding target path.

### Current release

The repository version is stored in the root [`VERSION`](VERSION) file. In v0.7.2:

- `python rbl.py version` reads the version from `VERSION`.
- The bundled Linux and Windows `rblc-asm` binaries report `0.7.2`.
- Newly created user projects start with their own package version, `0.1.0`; this is independent of the compiler version.
- The current release is tagged [`v0.7.2`](https://github.com/Reiwa-111/RBL/releases/tag/v0.7.2).

See the [v0.7.2 release notes](docs/eng/release-notes.md) for the detailed change list.

---

## Current release and scope

RBL 0.7.2 is a working single-source-file language/toolchain release. Its current implementation includes:

- Signed 64-bit integers, binary64 floating-point values, booleans, strings, `Unit` and `null`.
- Functions, recursion, typed function parameters and return statements.
- Conditional expressions, `if`/`elif`/`else`, `switch`, range loops, condition loops, `break` and `continue`.
- Lists, dictionaries, tuples and record-like `struct` declarations with receiver-method syntax.
- String concatenation, escape sequences, multiline strings and basic f-string interpolation.
- Global built-ins and the `rbl.io`, `rbl.math`, `rbl.string`, `rbl.fs`, `rbl.time` and `rbl.random` namespaces.
- Linux x86-64 ELF64 and Windows x86-64 PE/COFF native executable targets.
- A project CLI, optional graphical IDE, regression suite and benchmark runners.

The exact language contract is documented in the [syntax reference](docs/eng/reference/syntax.md). The [limitations reference](docs/eng/reference/limitations.md) distinguishes implemented behavior from features that are not available yet.

---

## Highlights

### Direct native compilation

```text
RBL source
    |
    v
UTF-8 lexer
    |
    v
Recursive-descent parser
    |
    v
AST
    |
    v
Static analysis and conservative optimizations
    |
    v
x86-64 assembly
    |
    v
GNU assembler
    |
    v
Platform runtime + system libraries
    |
    v
Native executable
```

There is no generated C program between RBL source and assembly output.

### Two x86-64 operating-system targets

| Target | Executable format | Platform runtime |
|---|---|---|
| Linux x86-64 | ELF64 | `runtime/rbl_runtime.s` |
| Windows x86-64 | PE/COFF | `runtime/rbl_runtime_win.c` |

Both targets use `runtime/rbl_containers.c` for the shared container implementation.

### Native IDE and CLI

RBL Studio offers an editor and build/run workflow, while the CLI supports headless use, automation, testing and toolchain diagnostics. Neither depends on the other: the IDE is optional.

### Regression-oriented development

The repository includes direct compiler regression cases, standard-library smoke tests and a differential runner for comparing Linux and Windows execution paths where their behavior is expected to match.

---

## Quick start

### Requirements

The required toolchain depends on the target.

| Environment | Main requirements |
|---|---|
| Linux x86-64 | Python 3, GNU assembler (`as`), GNU linker (`ld`), system C/math libraries |
| Windows x86-64, native | Python 3, MSYS2 MinGW-w64/UCRT64 toolchain, including GCC and GNU assembler |
| Windows with WSL | Windows host, WSL distribution with the Linux build tools, and the configured native/WSL integration |

On Windows, the toolchain driver supports automatic selection, native Windows compilation and WSL fallback. Native Windows compilation does not require WSL when the MinGW-w64 toolchain is available.

For exact prerequisites and troubleshooting, see the [Windows and WSL guide](docs/eng/how-to/windows-toolchain.md).

### Get the repository

```bash
git clone https://github.com/Reiwa-111/RBL.git
cd RBL
```

### Check the toolchain

Linux:

```bash
python3 rbl.py doctor
python3 rbl.py version
```

Windows PowerShell:

```powershell
python rbl.py doctor
python rbl.py version
```

`doctor` reports the active backend and checks the compiler, assembler, linker and configured platform integration.

### Run an example

Linux:

```bash
python3 rbl.py run examples/test.rbl
```

Windows PowerShell:

```powershell
python rbl.py run examples\test.rbl
```

### Create a project

Run from the repository checkout:

```bash
python3 rbl.py new hello
cd hello
python3 ../rbl.py run
```

In Windows PowerShell:

```powershell
python rbl.py new hello
cd hello
python ..\rbl.py run
```

The project is created with an `rbl.toml` manifest and a starter source file at `src/main.rbl`.

**CLI invocation note:** RBL does not install a global `rbl` executable. Use `python3 rbl.py` on Linux/WSL or `python rbl.py` in PowerShell from the checkout root. From another project directory, invoke the script by its relative or absolute path. The repository also contains Bash and Windows wrapper scripts.

For a guided setup from project creation to execution, follow the [first-program tutorial](docs/eng/tutorial/first-program.md).

---

## A small RBL program

```rbl
func square(value int) int (
    return value * value
)

func main() (
    set total = 0

    for (i in 1..=5) (
        let total += square(i)
    )

    print("sum of squares:", total)
)
```

Expected output:

```text
sum of squares: 55
```

This example uses:

- `func` to declare a function.
- A typed function parameter (`value int`) and return annotation (`int`).
- `set` to declare a variable.
- `let` to update an existing variable.
- An inclusive range (`1..=5`).
- `return` to return a value from a function.

Save it as `hello.rbl` and run it through the CLI:

```bash
python3 rbl.py run hello.rbl
```

More examples and precise syntax rules are available in the [language tutorial](docs/eng/tutorial/first-program.md) and [syntax reference](docs/eng/reference/syntax.md).

---

## Examples

The [`examples/`](examples/README.md) directory contains **44 standalone RBL programs**: a 20-lesson beginner course, compact language and standard-library demonstrations, sorting/searching and prime-number algorithms, numerical solvers, a card-shuffling statistics experiment, physics simulations, Conway's Game of Life, Mandelbrot/Newton fractals, and procedural image generators.

### Learn RBL through 20 short lessons

The [Beginner Course](examples/beginner/README.md) teaches the language as a sequence of **20 small, practical articles**, each paired with a runnable `.rbl` example. It starts with variables and a first program, then builds up through control flow, functions, collections, structs, strings, files, recursion and the standard library. The guide explains the concepts and syntax; the lesson files let you experiment with them directly.

- **[Русское руководство](examples/beginner/README_RUS.md)** · [English guide](examples/beginner/README_ENG.md)

| Lesson | Topic | Example |
|---:|---|---|
| 01 | Hello world, variables and output | [`01_hello_variables.rbl`](examples/beginner/01_hello_variables.rbl) |
| 02 | Calculator, input and arithmetic | [`02_calculator.rbl`](examples/beginner/02_calculator.rbl) |
| 03 | Conditions, comparisons and `switch` | [`03_conditions.rbl`](examples/beginner/03_conditions.rbl) |
| 04 | Loops, ranges, `break` and `continue` | [`04_loops.rbl`](examples/beginner/04_loops.rbl) |
| 05 | Functions, parameters and return values | [`05_functions.rbl`](examples/beginner/05_functions.rbl) |
| 06 | Lists and indexing | [`06_lists.rbl`](examples/beginner/06_lists.rbl) |
| 07 | Dictionaries and tuples | [`07_dictionaries_tuples.rbl`](examples/beginner/07_dictionaries_tuples.rbl) |
| 08 | Structs and receiver methods | [`08_structs.rbl`](examples/beginner/08_structs.rbl) |
| 09 | Strings and string functions | [`09_strings.rbl`](examples/beginner/09_strings.rbl) |
| 10 | Standard library overview | [`10_standard_library.rbl`](examples/beginner/10_standard_library.rbl) |
| 11 | Nested loops | [`11_nested_loops.rbl`](examples/beginner/11_nested_loops.rbl) |
| 12 | Condition-controlled loops | [`12_condition_loops.rbl`](examples/beginner/12_condition_loops.rbl) |
| 13 | Nested lists | [`13_nested_lists.rbl`](examples/beginner/13_nested_lists.rbl) |
| 14 | File input and output | [`14_file_io.rbl`](examples/beginner/14_file_io.rbl) |
| 15 | Multiline strings and escape sequences | [`15_multiline_strings_escapes.rbl`](examples/beginner/15_multiline_strings_escapes.rbl) |
| 16 | `break` and `continue` in nested loops | [`16_nested_break_continue.rbl`](examples/beginner/16_nested_break_continue.rbl) |
| 17 | Recursion | [`17_recursion.rbl`](examples/beginner/17_recursion.rbl) |
| 18 | Nested dictionaries | [`18_nested_dictionaries.rbl`](examples/beginner/18_nested_dictionaries.rbl) |
| 19 | Logging and diagnostics | [`19_logging.rbl`](examples/beginner/19_logging.rbl) |
| 20 | Math, time and filesystem APIs | [`20_math_time_filesystem.rbl`](examples/beginner/20_math_time_filesystem.rbl) |

Run any lesson from the repository root, for example:

```bash
python3 rbl.py check examples/beginner/01_hello_variables.rbl
python3 rbl.py run examples/beginner/01_hello_variables.rbl
```

The [example catalog](examples/README.md) documents the remaining programs, output formats, model assumptions and Linux/Windows run commands.

The numerical programs stream structured **CSV** or plain-text **PGM (P2)** output to standard output. This keeps simulation and pixel generation inside RBL while allowing external tools to visualize or analyze the resulting data. See the [example catalog](examples/README.md) for output schemas, model assumptions and Linux/Windows run commands.

```bash
python3 rbl.py run examples/simulations/double_pendulum.rbl > double_pendulum.csv
python3 rbl.py run examples/fractals/mandelbrot_256.rbl > mandelbrot_256.pgm
```

An optional Python visualization pipeline converts the scientific examples into PNG images, GIF animations and 2D/3D plots:

```bash
python3 -m pip install -r requirements-visualize.txt
python3 tools/visualize_examples.py
```

Generated images, animations and plots are saved under `build/visualizations/`; the captured PGM/CSV text streams are retained in its `data/` subdirectory. The default pipeline renders a preview tile for the 4K Mandelbrot example; use `python3 tools/visualize_examples.py tile --full` to render and assemble all 30 tiles into a 3840×2160 PNG. See the [example catalog](examples/README.md) for all programs, output formats, model assumptions and commands.

---

## Language overview

This section is an introduction, not a replacement for the [complete syntax reference](docs/eng/reference/syntax.md).

### Values and variables

The language/runtime supports:

| Value | Description |
|---|---|
| `int` | Signed 64-bit integer |
| `float` | IEEE-754 binary64 floating-point value |
| `bool` | `true` or `false` |
| `string` | UTF-8 string |
| `Unit` | No meaningful return value |
| `null` | A distinct null value |
| list | Mutable indexed sequence |
| dictionary | Mutable key/value mapping |
| tuple | Ordered immutable sequence |
| struct instance | Record-like value represented by a dictionary |

Variables are declared and updated explicitly:

```rbl
set count = 10
let count = 11
let count += 4
let count++
```

- `set name = expression` declares a new variable.
- `let name = expression` updates an existing variable.
- Duplicate declarations and updates to unknown names are errors.

The language has dynamically typed runtime values. Function parameters can declare types, and argument values are checked when a function is called. This does not mean that RBL 0.7.2 has a complete static type system.

### Functions

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    print(add(20, 22))
)
```

Functions support parameters, return values and recursion. Parameter types are declared after parameter names. Return annotations are parsed, but return types are not enforced as a statically verified contract in v0.7.2.

### Control flow

Conditional blocks use parentheses:

```rbl
if (score >= 90) (
    print("high")
) elif (score >= 60) (
    print("medium")
) else (
    print("low")
)
```

Range loops can be exclusive or inclusive:

```rbl
for (i in 0..5) (
    print(i)
)

for (i in 0..=5) (
    print(i)
)
```

`break` exits the innermost loop and `continue` advances to its next iteration. RBL also supports condition loops and `switch`.

### Collections

Lists:

```rbl
set values = [10, 20, 30]
print(values[0])
let values[1] = 25
print(len(values))
```

Dictionaries:

```rbl
set config = {"mode": "safe", "retries": 3}
print(config["mode"])
let config["retries"] = 4
print("mode" in config)
```

Tuples:

```rbl
set pair = (10, 20)
print(pair[0])
```

Struct declarations and receiver-method syntax are available:

```rbl
struct Point (
    x int
    y int
)

func (p Point) move(dx int, dy int) (
    let p.x = p.x + dx
    let p.y = p.y + dy
)

func main() (
    set p = Point(3, 4)
    p.move(1, 2)
    print(p.x, p.y)
)
```

**Struct implementation note:** struct instances currently use dictionary storage. Receiver methods are lowered to ordinary function calls with the receiver as their first argument. There is no separate runtime class identity or full object type system.

### Strings

RBL supports ordinary string literals, escape sequences, multiline strings and basic interpolation:

```rbl
set name = "RBL"
set version = 7
print(f"{name} v0.{version}")
```

Triple quotes create multiline strings:

```rbl
set message = """
first line
second line
"""
```

F-string interpolation works, but format specifications such as `{value:.2f}` are not implemented in v0.7.2. Runtime strings are NUL-terminated, so embedded NUL escapes are rejected.

### Operators and evaluation details

RBL supports arithmetic, comparisons, logical operators, membership (`in`) and identity (`is`). The `%` operator is integer-only; use `rbl.math.fmod` for floating-point remainder.

Some semantic boundaries matter:

- `and` and `or` evaluate both operands; they do not short-circuit.
- `null` and `Unit` are distinct values.
- A missing dictionary key evaluates to `Unit`, not `null`.
- List indexing is zero-based and requires an in-range integer.
- Negative indexing, slices, tuple destructuring and list concatenation with `+` are not implemented.

The [syntax reference](docs/eng/reference/syntax.md) describes these behaviors and their error cases in detail.

---

## Compiler architecture

RBL 0.7.2 uses a C bootstrap compiler located at:

```text
compiler/rblc_asm.c
```

The current compiler translation unit contains the UTF-8 lexer, recursive-descent parser, AST structures, static analysis and direct code generation. The normal path is:

1. Read and tokenize the RBL source.
2. Parse tokens into an abstract syntax tree.
3. Perform static checks and conservative analysis.
4. Apply eligible scalar and loop fast paths.
5. Emit target-specific x86-64 assembly.
6. Assemble and link the program with the matching runtime.

### Current optimizations

The compiler contains conservative optimizations for cases whose properties can be proven:

- Typed scalar fast paths for eligible integer and Boolean operations.
- Integer-focused loop optimization.
- Induction-variable analysis and register-held loop locals.
- Loop-local register allocation.
- Overflow checks for optimized integer operations.

When the compiler cannot safely specialize an expression, it falls back to the general tagged runtime representation.

### What is not in the compiler yet

The current compiler does **not** have a dedicated RBL intermediate representation, explicit CFG/basic-block pipeline, whole-function liveness analysis or global register allocator. These are planned compiler architecture steps, not features of v0.7.2.

See [compiler architecture](docs/eng/explanation/architecture.md) and [optimization details](docs/eng/explanation/optimization.md).

---

## Targets and toolchains

### Linux x86-64

Linux executables use ELF64.

Main components:

```text
Compiler:       bin/linux-x86_64/rblc-asm
Platform runtime: runtime/rbl_runtime.s
Shared containers: runtime/rbl_containers.c
Assembler:      GNU as
Linker:         GNU ld
Libraries:      libc and libm
```

The build path emits assembly, assembles it into an object file and links the result with the Linux runtime objects and system libraries.

### Windows x86-64

Windows executables use PE/COFF.

Main components:

```text
Compiler:       bin/win-x86_64/rblc-asm.exe
Platform runtime: runtime/rbl_runtime_win.c
Shared containers: runtime/rbl_containers.c
Toolchain:      MinGW-w64 (GCC + GNU assembler)
```

MinGW-w64 GCC builds the Windows runtime objects and acts as the linker driver. Generated RBL program code remains assembly; the compiler does not translate the program to C.

### Native Windows and WSL

On Windows, the toolchain driver can use the native MinGW-w64 path or WSL. Automatic mode prefers the native toolchain when available and can use WSL as a fallback.

Relevant configuration includes:

- `RBL_TOOLCHAIN` — toolchain selection override.
- `RBL_WSL_DISTRO` — WSL distribution selection; the documented default is `Ubuntu-24.04`.

For setup, supported modes and troubleshooting, see the [Windows toolchain guide](docs/eng/how-to/windows-toolchain.md).

---

## Runtime and memory

General dynamic values use a tag/payload representation. In the general runtime path, generated code uses:

```text
rax = value tag
rdx = value payload
```

Current general value tags:

| Tag | Value |
|---:|---|
| `0` | `Unit` |
| `1` | `Int` |
| `2` | `Float` |
| `3` | `String` |
| `4` | `Bool` |
| `5` | `List` |
| `6` | `Dictionary` |
| `7` | `Tuple` |
| `8` | `null` |

Some proven-safe scalar paths use unboxed machine values instead of the general representation.

### Runtime components

| Source file | Responsibility |
|---|---|
| `runtime/rbl_runtime.s` | Linux platform runtime |
| `runtime/rbl_runtime_win.c` | Windows platform runtime |
| `runtime/rbl_containers.c` | Shared list, dictionary, tuple and collector implementation |

The container runtime uses a conservative mark-and-sweep collector for list, tuple and dictionary objects. It tracks container allocations, searches the machine stack for candidate roots and traverses container references.

**Memory-management limitation:** this collector is not a full garbage collector for every runtime allocation. Dynamic strings, including concatenation and rendering results, are not all reclaimed individually and may remain allocated until process exit.

### Platform-specific behavior

A shared language API does not mean every system-observable value is identical across operating systems:

- `rbl.time.now_ms()` uses different clock sources on Linux and Windows.
- `rbl.fs.cwd()` returns a platform-dependent path.
- Linux and Windows pseudorandom sequences are not expected to match.
- Deterministic scalar/container behavior is tested for parity where appropriate.

See the [execution model](docs/eng/explanation/execution-model.md) and [standard-library reference](docs/eng/reference/standard-library.md).

---

## Standard library

Built-ins and namespace functions are provided by the compiler/runtime. They are available without a source-level `import` statement.

### Global built-ins

| Function | Purpose |
|---|---|
| `print(value, ...)` | Print values separated by spaces and followed by a newline |
| `len(value)` | String code-point count or collection length |
| `input()` / `input(prompt)` | Read a line from standard input |
| `read_file(path)` / `write_file(path, text)` | Text file input/output |
| `abs`, `sqrt`, `min`, `max` | Common numeric operations |
| `int`, `float`, `str` | Supported scalar conversions |
| `pow`, `floor`, `ceil`, `round` | Power and rounding |
| `sin`, `cos`, `tan`, `log`, `exp` | Mathematical functions |
| `random_int`, `random_float`, `random_bool` | Pseudorandom values |

Additional math aliases include `log10`, `log2`, `trunc`, `fmod`, `hypot`, `degrees`, `radians`, `clamp`, `sign`, `is_nan` and `is_inf`.

### Namespaces

| Namespace | Main functionality |
|---|---|
| `rbl.io` | Input, prompts and text file I/O |
| `rbl.math` | Mathematical functions and constants |
| `rbl.string` | String length, ASCII case conversion, trimming and search |
| `rbl.fs` | File existence, deletion and current working directory |
| `rbl.time` | Millisecond clock value and sleep |
| `rbl.random` | Pseudorandom integer, float and Boolean values |

Logging helpers are also available:

```rbl
warn.log("message")
error.log("message")
```

Important caveats:

- `rbl.string.upper()` and `lower()` perform ASCII case conversion, not full Unicode case mapping.
- The pseudorandom generator is not cryptographically secure.
- Clock behavior and random sequences differ by platform.
- Namespace names are compiler/runtime built-ins; they are not user-defined source modules.

For function details and platform behavior, see the [standard-library reference](docs/eng/reference/standard-library.md).

---

## RBL Studio

RBL Studio is the optional graphical IDE included in the repository. It is implemented in Python with Tkinter. The CLI and compiler can be used without launching the IDE.

### Launch

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

### Main capabilities

- Multi-tab source editor.
- Line numbers and RBL syntax highlighting.
- Indentation helpers, undo/redo and configurable editor settings.
- Syntax checking and inline diagnostics.
- Build/run workflow and console output.
- Generated assembly preview.
- Test and benchmark integration.
- Toolchain diagnostics and environment inspection.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `F5` | Build and run selected file |
| `Ctrl+B` | Build / assembly preview |
| `Ctrl+Shift+B` | Check source |
| `Ctrl+T` | Run tests |
| `Ctrl+Shift+T` | Run benchmarks |

The exact UI can evolve between Studio builds. For the current launch, shortcut and diagnostic workflow, see the [RBL Studio guide](docs/eng/how-to/ide.md).

---

## Command-line interface

The main project/toolchain entry point is `rbl.py`. It is a Python script in the repository, not a globally installed command.

### Commands

| Command | Purpose |
|---|---|
| `version` | Print the version from the root `VERSION` file |
| `doctor` | Check the active compiler and platform toolchain |
| `bootstrap` | Force a rebuild of compiler/runtime bootstrap artifacts |
| `env` | Print RBL root, project root and Python executable |
| `new NAME [--path DIR]` | Create a new project |
| `init` | Initialize the current directory |
| `check [FILE]` | Run frontend, analysis and assembly generation without linking |
| `build [FILE]` | Compile, assemble and link an executable |
| `run [FILE]` | Build and run a program |
| `asm [FILE]` | Generate and display assembly |
| `test` | Run this repository's regression and standard-library tests |
| `bench` | Run the portable RBL-only benchmark suite |
| `bench3way [--reps N] [--warmups N]` | Run the five-implementation Linux/WSL comparison |
| `diff [--a MODE] [--b MODE] [PATH ...]` | Compare two backend modes |
| `fmt [FILE] [--check]` | Format a file or check formatting |
| `doc [FILE] [-o FILE]` | Generate a simple function-declaration listing |
| `clean` | Remove `build/` in the current project directory |

Examples from the repository root:

Linux:

```bash
python3 rbl.py check examples/test.rbl
python3 rbl.py build examples/test.rbl
python3 rbl.py run examples/test.rbl
python3 rbl.py test
python3 rbl.py bench
```

Windows PowerShell:

```powershell
python rbl.py check examples\test.rbl
python rbl.py build examples\test.rbl
python rbl.py run examples\test.rbl
python rbl.py test
```

When a source file is omitted, the CLI uses `package.entry` from `rbl.toml`, defaulting to `src/main.rbl`. If the selected file does not exist but a root-level `main.rbl` exists, the driver can use that file.

### Important command boundaries

- `check` processes the source, performs analysis and generates assembly, but does not invoke the assembler/linker or produce an executable.
- `test` runs tests belonging to the RBL checkout. It is not a universal test runner for arbitrary projects.
- `bench` and `bench3way` run different benchmark suites. `bench3way` is a historical command name; it currently invokes a five-implementation comparison.
- `doc` produces a simple text-pattern-based function listing. It is not a full API documentation generator.
- `clean` removes `build/` from the current project directory; check that you do not need its contents first.

For full argument details, see the [CLI reference](docs/eng/reference/cli.md).

### Low-level driver and direct compiler

The lower-level Python driver is:

```text
tools/rbltool.py
```

It provides diagnostic and scripting operations such as `doctor`, `check`, `build`, `run` and `asm`.

Bundled compiler executables:

```text
Linux:   bin/linux-x86_64/rblc-asm
Windows: bin/win-x86_64/rblc-asm.exe
```

The compiler can emit assembly directly:

```bash
mkdir -p build
bin/linux-x86_64/rblc-asm examples/test.rbl -S build/test.s
```

The higher-level CLI is normally preferred for project work because it manages the build flow and toolchain selection.

---

## Project layout and manifest

A project uses an `rbl.toml` file:

```toml
[package]
name = "hello"
version = "0.1.0"
entry = "src/main.rbl"
```

| Field | Meaning |
|---|---|
| `package.name` | Project/package name |
| `package.version` | Version of the user project |
| `package.entry` | Entry source path, relative to the project directory |

The default entry is `src/main.rbl`. The default version for a newly generated project is `0.1.0`, independent of the installed RBL compiler version.

Build output is stored under:

```text
build/
```

A typical generated build contains assembly, object files and a native executable.

**Manifest limitation:** `rbl.toml` is not yet a dependency manifest. RBL 0.7.2 does not resolve packages, download dependencies or compile imported user modules. Its currently supported package fields are `name`, `version` and `entry`.

See the [manifest reference](docs/eng/reference/manifest.md) and [project workflow guide](docs/eng/how-to/project-workflow.md).

---

## Testing

The repository includes a direct compiler regression suite, separate standard-library/input checks and a differential runner.

### Run the main suites

From the repository root:

```bash
python3 rbl.py test
```

Equivalent individual checks:

```bash
python3 tests/run_tests.py
python3 tests/test_stdlib.py
```

In Windows PowerShell:

```powershell
python rbl.py test
```

The v0.7.2 release verification included:

- **59/59 direct RBL regression cases passed.**
- Standard-library and input smoke tests passed.
- CLI and bundled compiler version checks reported `0.7.2`.

The regression cases cover arithmetic, floats, strings, function behavior, evaluation order, loops, ranges, containers, `null`, standard-library behavior and known compiler defects. The runner uses timeouts so a program that unexpectedly loops forever does not hang the complete suite.

### Differential testing

On Windows, with WSL and native MinGW-w64 available:

```powershell
python tests\differential.py --a wsl --b native
```

The differential runner executes the same programs through two backend paths and compares exit status and output for deterministic cases. Platform-dependent outputs, such as current-directory strings and random values, require separate treatment.

For a complete testing workflow, see the [testing guide](docs/eng/how-to/testing.md).

---

## Benchmarks

The repository provides two different benchmark suites. Their measurements should not be mixed because their workloads and timing models differ.

### Portable RBL-only benchmarks

Run:

```bash
python3 rbl.py bench
```

This suite reports build time, complete process run time, startup-baseline-adjusted net time and executable size for RBL workloads.

### Five-implementation comparison

Run on Linux or WSL:

```bash
python3 benchmarks/compare_all.py --warmups 1 --reps 5
```

Or through the CLI:

```bash
python3 rbl.py bench3way --warmups 1 --reps 5
```

The comparison covers:

1. RBL.
2. C compiled with GCC `-O2`.
3. C++ compiled with G++ `-O2`.
4. CPython.
5. Handwritten x86-64 assembly.

It measures three synthetic integer workloads: accumulation, function calls and conditional updates. The C/C++ loop counts are supplied at runtime, and the call function is marked `noinline`, preventing the native compiler from replacing the entire measured workload with a constant result.

### Recorded v0.7.2 benchmark snapshot

The corrected run from 6 November 2026 used one warmup and five measured executions per workload. Values below are medians:

| Workload | RBL | C `-O2` | C++ `-O2` | CPython | Handwritten ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | 72.388 ms | 26.389 ms | 26.500 ms | 6,897.844 ms | 28.789 ms |
| `calls` | 25.240 ms | 14.318 ms | 13.394 ms | 911.629 ms | 14.078 ms |
| `branches` | 107.853 ms | 52.381 ms | 54.095 ms | 8,189.178 ms | 62.340 ms |

These are workload-specific measurements, not a universal ranking of programming languages. In these three tests, optimized C/C++ and the specialized handwritten assembly were faster than RBL; RBL was faster than CPython. The workloads do not represent I/O-heavy, memory-intensive, container-heavy or application-sized programs.

The comparison includes process startup in each timed run. RBL build time is reported separately. The handwritten assembly baseline uses Linux syscalls, so this five-way suite runs on Linux/WSL rather than as a native Windows benchmark.

Full methodology, environment, ratios, raw-run notes and caveats are documented in [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md) and [`benchmarks/README_3WAY.md`](benchmarks/README_3WAY.md).

---

## Documentation

The current documentation is maintained in **English and Russian** and follows [Diátaxis](https://diataxis.fr/). It documents implemented RBL 0.7.2 behavior; the archive is retained as historical reference, not as the active language specification.

Start at [`docs/README.md`](docs/README.md) to choose a language.

### English

| Section | Purpose | Main entry |
|---|---|---|
| Tutorial | A guided first program | [Your first RBL program](docs/eng/tutorial/first-program.md) |
| How-to | Practical workflows | [Project workflow](docs/eng/how-to/project-workflow.md), [Windows/WSL](docs/eng/how-to/windows-toolchain.md), [IDE](docs/eng/how-to/ide.md), [Testing](docs/eng/how-to/testing.md), [Benchmarking](docs/eng/how-to/benchmarking.md) |
| Reference | Exact language and tool contracts | [Syntax](docs/eng/reference/syntax.md), [Standard library](docs/eng/reference/standard-library.md), [CLI](docs/eng/reference/cli.md), [Manifest](docs/eng/reference/manifest.md), [Limitations](docs/eng/reference/limitations.md) |
| Explanation | Internal architecture and design trade-offs | [Architecture](docs/eng/explanation/architecture.md), [Execution model](docs/eng/explanation/execution-model.md), [Optimization](docs/eng/explanation/optimization.md), [Design](docs/eng/explanation/design.md) |

### Русский

| Раздел | Назначение | Основные материалы |
|---|---|---|
| Tutorial | Обучение на последовательном примере | [Первая программа](docs/ru/tutorial/first-program.md) |
| How-to | Практические инструкции | [Работа с проектом](docs/ru/how-to/project-workflow.md), [Windows и WSL](docs/ru/how-to/windows-toolchain.md), [IDE](docs/ru/how-to/ide.md), [Тестирование](docs/ru/how-to/testing.md), [Benchmark](docs/ru/how-to/benchmarking.md) |
| Reference | Точный синтаксис и интерфейсы | [Синтаксис](docs/ru/reference/syntax.md), [Стандартная библиотека](docs/ru/reference/standard-library.md), [CLI](docs/ru/reference/cli.md), [Манифест](docs/ru/reference/manifest.md), [Ограничения](docs/ru/reference/limitations.md) |
| Explanation | Архитектура и проектные решения | [Архитектура](docs/ru/explanation/architecture.md), [Модель выполнения](docs/ru/explanation/execution-model.md), [Оптимизации](docs/ru/explanation/optimization.md), [Принципы дизайна](docs/ru/explanation/design.md) |

Release history is maintained separately using the **Added**, **Changed** and **Fixed** categories:

- [English release notes](docs/eng/release-notes.md)
- [Заметки о релизах на русском](docs/ru/release-notes.md)

Older documents are preserved under `docs/eng/archive/` and `docs/ru/archive/`. When an archived page conflicts with the current documentation or implementation tests, the current reference and regression tests take precedence.

---

## Development roadmap

The active roadmap describes the work planned after v0.7.2. It separates implemented functionality from planned work and defines exit criteria; it is not a release-date promise.

| Milestone | Focus | Status in v0.7.2 |
|---|---|---|
| `0.7.2` | Version consistency, documentation and benchmark correction | Released |
| `0.8` | Compiler source modularization, runtime ABI/diagnostics, string lifetime model and f-string format specifications | Planned |
| `0.9` | Explicit RBL IR, CFG/basic blocks, liveness, whole-function register allocation and IR optimizations | Planned |
| `1.0` | Source-module system and deterministic multi-file project builds | Planned; central 1.0 goal |

### v0.8 — compiler and runtime stabilization

The next engineering milestone focuses on maintainability and runtime contracts:

- Split the current compiler translation unit into well-defined internal modules without changing language behavior.
- Specify and version the compiler/runtime ABI independently from the language version.
- Improve source-location tracking and diagnostics.
- Define and test a memory lifetime model for dynamic strings.
- Specify and implement f-string format specifications with consistent Linux/Windows behavior.

### v0.9 — RBL IR and optimization infrastructure

The compiler is planned to gain an explicit intermediate representation and control-flow graph:

- Define IR values, instructions, blocks, terminators and invariants.
- Lower the existing AST into IR.
- Introduce CFG verification and source-location metadata.
- Move x86-64 code generation to the IR backend.
- Add liveness analysis and whole-function register allocation.
- Add carefully constrained constant folding, dead-code/dead-store elimination and common-subexpression elimination.

### v1.0 — source modules and multi-file projects

The central user-facing goal of v1.0 is to move beyond the current one-source-file-per-program model. The design is expected to cover:

- Source-level imports, including module and path-based forms.
- Deterministic module resolution and module identity.
- Public/private visibility and symbol conflict rules.
- Dependency graph construction and cycle diagnostics.
- Inter-module calls and source-location-aware errors.
- A project model with an entry module and build graph.
- Correct cache invalidation when a source dependency changes.
- Linux/Windows behavior parity and regression/integration coverage.

The source-module system is separate from loading native plugins into the IDE or compiler process. Package registries, external dependency resolution, native plugin hosting, async/await, closures, exception handling, a source debugger, additional architectures and self-hosting are not promised as part of v1.0.

Read the full [RBL roadmap](docs/eng/explanation/roadmap.md) or [дорожную карту на русском](docs/ru/explanation/roadmap.md) for phases, dependencies, completion criteria and explicitly deferred work.

---

## Known limitations

The following are important boundaries of the v0.7.2 implementation:

### Language and compiler

- Only x86-64 Linux and Windows targets are implemented.
- A program is compiled from one source file. Source `import`, user modules and multi-file compilation are not available.
- There are no global variables or constants.
- Exceptions (`try`/`catch`/`finally`, `raise`) and `defer` are not implemented.
- Lambda expressions and closures are not implemented.
- Default, variadic and named arguments are not implemented; there is no overload resolution.
- `async`/`await` and a task scheduler are not implemented.
- There is no complete static type system or generic/container type-expression system.
- Struct values are dictionaries, not a distinct runtime object type.
- Tuple destructuring, list slices, negative indexing and dictionary iteration in loops are not implemented.
- F-string format specifications are not implemented.
- Logical `and` and `or` do not short-circuit.
- There is no source-level debugger or complete Windows source line mapping.
- There is no explicit RBL IR/CFG or whole-function register allocator yet.

### Runtime and tooling

- Container garbage collection does not reclaim all dynamic string allocations.
- `rbl.string.upper/lower` do not implement Unicode case mapping.
- The random generator is not cryptographically secure; Linux and Windows sequences differ.
- `rbl.time.now_ms()` and `rbl.fs.cwd()` have platform-dependent behavior.
- There is no package manager, package registry or external dependency resolver.
- `rbl doc` is a simple function listing, not a full documentation generator.
- `rbl test` tests the RBL repository; it is not a generic runner for arbitrary user projects.

The [limitations reference](docs/eng/reference/limitations.md) is the normative list for the language version.

---

## Repository structure

The main parts of the checkout are organized as follows:

```text
RBL/
├── compiler/
│   └── rblc_asm.c              # C bootstrap compiler
│
├── runtime/
│   ├── rbl_runtime.s           # Linux platform runtime
│   ├── rbl_runtime_win.c       # Windows platform runtime
│   └── rbl_containers.c        # Shared container runtime and collector
│
├── bin/
│   ├── linux-x86_64/           # Linux compiler/runtime objects
│   └── win-x86_64/             # Windows compiler/runtime objects
│
├── ide/
│   ├── rbl_studio.py
│   └── launch.py
│
├── tools/
│   ├── rbltool.py              # Lower-level toolchain driver
│   └── visualize_examples.py   # Optional scientific-example visualizer
│
├── tests/                      # Regression, smoke and differential tests
├── benchmarks/                 # Benchmark runners and recorded results
├── examples/                   # Language and scientific/data examples
│   ├── beginner/                # 20-lesson RBL language course (RU/EN guides)
│   ├── algorithms/              # Sorting, searching and prime-number algorithms
│   ├── fractals/                # Mandelbrot and Newton renderers
│   ├── generators/              # Procedural image/data generators
│   ├── numerical/               # Root finding, linear systems and ODE solvers
│   ├── simulations/             # Game of Life, pendulum, N-body gravity
│   └── statistics/              # ShuffleLab statistical experiment
├── docs/
│   ├── eng/                    # English documentation
│   └── ru/                     # Russian documentation
│
├── rbl.py                      # Main project CLI
├── rbl                         # Bash wrapper
├── rbl.cmd                     # Windows wrapper
├── RBLStudio.py                # IDE entry point
├── RBLStudio.sh                # Linux IDE launcher
├── RBLStudio.cmd               # Windows IDE launcher
├── VERSION                     # Repository/compiler version
├── requirements-visualize.txt  # Optional Python visualization dependencies
├── RELEASE_MANIFEST.txt        # Release implementation summary
└── README.md
```

The tree above is an orientation map, not a complete file inventory.

---

## License

RBL is released under the **Apache License 2.0**. You may use, modify and redistribute the project under the terms of that license.

See [`LICENSE`](LICENSE) for the complete license text.
