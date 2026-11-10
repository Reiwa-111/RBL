# last verified: v0.8.0 | 2026.11.10 | IDE INTERACTION AND EDITOR AUDIT

# RBL — Reiwa Bat Language

**RBL is a native compiled programming language and development toolchain.** It includes a C bootstrap compiler that generates x86-64 assembly directly, platform runtimes for Linux and Windows, a graphical IDE, a project-oriented command-line interface, regression tests and benchmark tools.

<p align="center">
  <strong>Documentation target: RBL 0.8.0</strong>
  ·
  <a href="docs/README.md">Documentation</a>
  ·
  <a href="examples/beginner/README_ENG.md">Beginner course</a>
  ·
  <a href="docs/ru/README.md">Русская документация</a>
</p>

RBL compiles user programs to native executables. **RBL source code is not translated into C**: the compiler parses the language, performs static analysis and selected optimizations, then emits x86-64 assembly. The C compiler is used to build parts of the compiler/runtime toolchain, not to transpile user programs.

---

## Contents

- [Project overview](#project-overview)
- [Current candidate and scope](#current-candidate-and-scope)
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
| RBL Studio | Optional native wxWidgets desktop IDE |
| Project CLI | Project creation, checking, building, running, formatting and diagnostics |
| Tests and benchmarks | Regression, standard-library, backend differential and performance tooling |

The compiler frontend and code generator are shared between targets where possible. Platform-specific runtime functions, object formats, assembler/linker invocation and system integration are handled by the corresponding target path.

### Current version

The repository/compiler version is stored in the root [`VERSION`](VERSION) file. The current source tree and both bundled compiler binaries target **RBL 0.8.0**. Release publication is a separate operation and is not implied by this documentation.

- `python rbl.py version` reads `0.8.0` from `VERSION`.
- The Linux and Windows `rblc-asm` binaries are built with the same version value and report `0.8.0`.
- Newly created user projects start with their own package version, `0.1.0`; this is independent of the compiler version.
- [`RELEASE_MANIFEST.txt`](RELEASE_MANIFEST.txt) records the 0.8.0 release scope, targets, verification evidence and known boundaries.

RBL 0.8.0 includes bounded f-string format specifications, Runtime-managed dynamic strings, a versioned Runtime ABI, source-position diagnostics, a modular compiler and indexed compiler lookups. The sections below describe the implemented 0.8.0 contract.

---

## Current candidate and scope

RBL 0.8.0 is a single-file language and native compilation toolchain. Its compiler implementation is split across C translation units. The current implementation includes:

- Signed 64-bit integers, binary64 floating-point values, booleans, strings, `Unit` and `null`.
- Functions, recursion, typed function parameters and return statements.
- Conditional expressions, `if`/`elif`/`else`, `switch`, range loops, condition loops, `break` and `continue`.
- Lists, dictionaries, tuples and record-like `struct` declarations with receiver-method syntax.
- String concatenation, escape sequences, multiline strings and f-string interpolation with bounded format specifications.
- Global built-ins and the `rbl.io`, `rbl.math`, `rbl.string`, `rbl.fs`, `rbl.time` and `rbl.random` namespaces.
- Linux x86-64 ELF64 and Windows x86-64 PE/COFF native executable targets.
- A project CLI, optional graphical IDE, regression suite and benchmark runners.

RBL 0.8.0 uses **Runtime ABI 1.2.0**, versioned independently from the language/compiler version. Generated executables embed their required ABI descriptor and validate it before entering RBL `main`; see the [Runtime ABI reference](docs/eng/reference/runtime-abi.md).

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

RBL Studio's primary desktop frontend is the native wxWidgets 3.2 / Scintilla application in `native/wx-ui/`, paired with the Rust `rbl-studio-worker`. It provides multi-tab editing, RBL syntax highlighting and folding, project navigation, Find/Replace, Find in Files, quick-open, command palette, clickable diagnostics, Build/Run/Check/ASM/Tests/Benchmarks/Doctor tasks, streamed output and interactive stdin/EOF. It opens ordinary text files of supported encodings while compiler actions accept only `.rbl` documents. The native GUI can launch without Python; backend actions require Python 3.9+ and the relevant GNU toolchain. The older Rust/egui source remains as a legacy reference; `RBL_STUDIO_ALLOW_LEGACY=1` permits fallback to a separately built executable from a developer checkout when wx is unavailable. The legacy executable is not included in the wx installer payload. The CLI remains independently usable without the IDE. See the [Russian](docs/ru/how-to/ide.md) and [English](docs/eng/how-to/ide.md) IDE guides, [IDE resource measurements](benchmarks/IDE_RESOURCE_USAGE.md) and the [IDE engineering audit](docs/eng/explanation/ide-engineering-audit.md) ([RU](docs/ru/explanation/ide-engineering-audit.md)).

### Regression-oriented development

The repository includes direct compiler regression cases, standard-library smoke tests and a differential runner for comparing Linux and Windows execution paths where their behavior is expected to match.

---

## Quick start

### Requirements

The required toolchain depends on the target.

| Environment | Main requirements |
|---|---|
| Linux x86-64 CLI/compiler | Python 3, GNU assembler (`as`), GNU linker (`ld`), system C/math libraries |
| Windows x86-64 CLI/compiler | Python 3; the repository includes a minimal MinGW-w64/UCRT64 linker+assembler bundle. MSYS2 is needed for developer bootstrap/rebuilds or when using an external toolchain. |
| Windows with WSL | Windows host, WSL distribution with the Linux build tools, and the configured native/WSL integration |
| Linux desktop IDE | wxWidgets 3.2 runtime (including STC/AUI), GTK/display dependencies, Python 3.9+ and GNU Binutils for backend tasks |
| Windows desktop IDE | wxWidgets/MinGW runtime DLLs are included in the build/install payload; Python 3.9+ is required for backend tasks |

On Windows, the toolchain driver supports automatic selection, native Windows compilation and WSL fallback. Native Windows compilation does not require WSL when the MinGW-w64 toolchain is available.

For exact prerequisites and troubleshooting, see the [Windows and WSL guide](docs/eng/how-to/windows-toolchain.md).

### Get the repository

```bash
git clone https://github.com/Reiwa-111/RBL.git
cd RBL
```

This guide describes the RBL 0.8.0 release-candidate source tree, not necessarily the default branch returned by a fresh clone. At the 2026-11-09 audit snapshot, upstream `main` still contains RBL 0.7.2; use a checkout whose root `VERSION` file reports `0.8.0` before following the candidate-specific instructions below.

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

For a guided introduction from the first program through core language features, follow the [20-lesson beginner course](examples/beginner/README_ENG.md).

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

More examples and precise syntax rules are available in the [20-lesson beginner course](examples/beginner/README_ENG.md) and [syntax reference](docs/eng/reference/syntax.md).

---

## Examples

The [`examples/`](examples/README.md) directory contains **53 standalone RBL programs**: a 20-lesson beginner course, compact language and standard-library demonstrations, dedicated f-string formatting and interactive unit-conversion examples, sorting/searching and prime-number algorithms, numerical solvers, descriptive statistics, a card-shuffling experiment, physics simulations, Conway's Game of Life, Mandelbrot/Newton fractals, and procedural image generators.

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
| 09 | Strings, f-strings and string functions | [`09_strings.rbl`](examples/beginner/09_strings.rbl) |
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

The language has dynamically typed runtime values. Function parameters can declare types, and argument values are checked when a function is called. This does not mean that RBL 0.8.0 has a complete static type system.

### Functions

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    print(add(20, 22))
)
```

Functions support parameters, return values and recursion. Parameter types are declared after parameter names. Return annotations are parsed, but return types are not enforced as a statically verified contract in v0.8.0.

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

RBL supports ordinary string literals, escape sequences, multiline strings, expression interpolation and bounded f-string format specifications:

```rbl
set name = "RBL"
set version = 7
print(f"{name} v0.{version}")
print(f"answer={42:>8d}; pi={3.14159:.2f}")
```

Triple quotes create multiline strings:

```rbl
set message = """
first line
second line
"""
```

F-string format specifications support the bounded grammar documented in the [syntax reference](docs/eng/reference/syntax.md). The [formatting example](examples/strings/fstring_formatting.rbl) demonstrates alignment, fill, width, precision and numeric bases. Nested/dynamic specs, sign flags, grouping and locale controls remain unsupported. Runtime strings are NUL-terminated, so embedded NUL escapes are rejected.

### Operators and evaluation details

RBL supports arithmetic, comparisons, logical operators, membership (`in`) and identity (`is`). The `%` operator is integer-only; use the global `fmod` function for floating-point remainder.

Some semantic boundaries matter:

- `and` and `or` evaluate both operands; they do not short-circuit.
- `null` and `Unit` are distinct values.
- A missing dictionary key evaluates to `Unit`, not `null`.
- List indexing is zero-based and requires an in-range integer.
- Negative indexing, slices, tuple destructuring and list concatenation with `+` are not implemented.

The [syntax reference](docs/eng/reference/syntax.md) describes these behaviors and their error cases in detail.

---

## Compiler architecture

RBL 0.8.0 uses a modular C bootstrap compiler:

```text
compiler/common.c       # diagnostics, allocation helpers and hashing
compiler/lexer.c        # UTF-8 lexer
compiler/ast.c          # AST nodes and vector helpers
compiler/parser.c       # recursive-descent parser
compiler/sema.c         # semantic/type analysis and symbol indexes
compiler/codegen.c      # direct x86-64 assembly backend
compiler/driver.c       # compiler CLI and pipeline orchestration
compiler/rblc_internal.h # private compiler data structures and interfaces
```

The compiler is built from separate C translation units. Shared data layouts and private cross-module interfaces live in `rblc_internal.h`. The Makefile compiles one object per module and supports parallel/incremental builds; native and WSL bootstrap paths compile the same source list in one compiler invocation. The dependency direction is common → lexer/AST → parser and semantic analysis → code generation → driver; semantic fast-path eligibility is owned by semantic analysis, so the frontend does not depend on the assembly emitter. There is one shared frontend and one code generator for both targets. The normal path is:

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
- Hash-indexed variable slots, function names and assembly string constants, preserving insertion order and last-definition-wins behavior.

When the compiler cannot safely specialize an expression, it falls back to the general tagged runtime representation.

### What is not in the compiler yet

The current compiler does **not** have a dedicated RBL intermediate representation, explicit CFG/basic-block pipeline, whole-function liveness analysis or global register allocator. These remain later compiler architecture steps, not features of v0.8.0.

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

The repository includes a private 43-file MinGW-w64/UCRT64 subset (about 21.8 MB) for ordinary Windows RBL user-program assembly and linking. It deliberately omits GCC's C frontend and headers, so it is not sufficient to rebuild the bootstrap compiler or to build the Rust IDE itself. Package versions and third-party license notices are documented in [`THIRD_PARTY_NOTICES.md`](toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md).

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

The shared runtime uses a conservative, non-moving mark-and-sweep collector for containers and dynamic strings. It scans the generated RBL stack from the frame pointer captured in `main`, then traces values stored in reachable containers. The compiler emits safe points before statements that may allocate or call code with unknown allocation behavior, and before repeated loop-condition evaluation. Proven non-allocating integer/Boolean scalar statements omit redundant checks; Runtime allocators do not collect in the middle of an expression. Static string literals are not heap-managed.

**Memory-management limitation:** conservative stack scanning can retain unreachable allocations when stale pointer-shaped values remain in scanned memory. Collection timing is therefore nondeterministic. Only the scanned current-thread stack and reachable containers are roots; native heap/global storage and other threads are not scanned. The native ABI does not expose a pinning/root-registration API for borrowed string pointers. Runtime state is process-global and unsynchronized; only the thread that entered generated RBL `main` may call Runtime helpers.

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

RBL Studio's primary frontend is the native wxWidgets 3.2 / `wxStyledTextCtrl`
(Scintilla) application in [`native/wx-ui/`](native/wx-ui/), paired with the
Rust `rbl-studio-worker`. The wx GUI is C++20; the worker reuses the Rust process
supervision layer and launches the existing Python `tools/rbltool.py` backend.
No Chromium, Electron or embedded WebView is used. Python 3.9+ is needed for
compiler/build/test/benchmark actions, not to render the GUI. The previous
Rust/egui application remains in the source tree for rollback; when the wx
executable is unavailable, `RBL_STUDIO_ALLOW_LEGACY=1` permits the launcher to
use a separately built legacy executable from a developer checkout. It is not
included in the wx installer payload.

The wx application is **UNRELEASED / RELEASE BLOCKED**. Windows and Linux Release builds, compiler/worker tests, differential checks, installer acceptance, filesystem-security regressions and Windows GUI acceptance are documented in [`RELEASE_MANIFEST.txt`](RELEASE_MANIFEST.txt). The Windows SaveDocument byte-roundtrip suite passes 27/27 on the current GUI candidate. The preliminary 480-second resource run passed its resource/UI thresholds but used the earlier harness and did not verify graceful shutdown. The second 7,200-second attempt failed early after task #45 and produced no CSV. A subsequent 480-second run with the strengthened harness exited with code 1 after the initial Run Tests dispatch; its retained log contains only startup output and no CSV/verdict. No current-RC two-hour soak PASS is claimed.

DPI static checks pass 12/12, but live 125-200% multi-monitor acceptance remains open because a suitable display is unavailable. Interactive Linux GUI acceptance is also unverified in the WSL session (no X11/Wayland display). The Rust worker corresponding-source bundle is now included by the updated build scripts and verified/copied by both installers; existing RC directories predate this change. The separate 330,067,308-byte MSYS2 toolchain/wx source archive remains local and is not attached to the runtime package. No written offer or alternative source-distribution procedure has been legally reviewed. Binutils `cache_bwrite` is reachable during ordinary PE output writes, while the exact CVE-2026-90801 trigger condition remains unresolved. POSIX existing-target saves now fail closed when `RENAME_EXCHANGE` is unsupported; the unsafe unconditional `renameat` fallback was removed. Windows retains a path-based `ReplaceFileW` leaf race despite recovery links and identity checks; a separate LOW cleanup edge may leave an orphan temporary file if handle metadata retrieval fails immediately after creation. Binary redistribution remains blocked pending source-delivery/legal review and resolution or mitigation of the Binutils CVE.

See the [wx frontend guide](docs/eng/how-to/ide.md), [engineering audit](docs/eng/explanation/ide-engineering-audit.md), [third-party inventory](native/wx-ui/THIRD_PARTY_NOTICES.md) and [Rust worker license inventory](native/wx-ui/RUST_DEPENDENCY_NOTICES.md).

### Build, install and launch

Build from a developer checkout:

```powershell
.\tools\build_wx_ide.ps1
```

```bash
./tools/build_wx_ide.sh
```

The launchers below select the last successful wx build. They do not silently
fall back to egui.

Linux:

```bash
bash RBLStudio.sh
```

Windows PowerShell:

```powershell
.\RBLStudio.cmd
```

User-scope installation (optional):

```powershell
.\installer\install_windows.ps1
```

```bash
./installer/install_linux.sh
```

Uninstall with the installed `Uninstall-RBLStudio.ps1` or
`Uninstall-RBLStudio.sh` script. Updates preserve the previous installation in
a timestamped backup; uninstall removes only manifest-owned program files and
leaves unrelated files/settings/workspaces untouched. See the IDE guide for
isolated `-NoRegister` / `--no-register` test modes and prerequisites.

### Main capabilities

- Multi-tab source editor.
- Line numbers, RBL syntax highlighting, scope guides and structural block folding.
- Undo/redo, configurable themes and editor settings.
- Syntax checking and diagnostics in the Problems panel.
- Build/run workflow, streamed program output and interactive stdin for `input()`.
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
| `Ctrl+B` | Build |
| `Ctrl+Shift+B` | Check source |
| `Ctrl+T` | Run tests |
| `Ctrl+Shift+T` | Run benchmarks |
| `Ctrl+W` | Close current tab |

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
| `test` | Run regression, standard-library/input, build-cache isolation, Linux Runtime call-alignment, Runtime ABI, string ownership/OOM, I/O-under-GC and diagnostics tests |
| `bench` | Run the portable RBL-only benchmark suite |
| `bench5way [--reps N] [--warmups N]` | Run the five-implementation Linux/WSL comparison (`bench3way` remains a legacy alias) |
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
- The test command includes direct regression, standard-library/input smoke, same-basename build-cache isolation, Linux Runtime call-alignment instrumentation, Runtime ABI, deterministic string ownership/OOM, repeated I/O under GC pressure and diagnostics checks.
- `bench` and `bench5way` run different benchmark suites. `bench5way` invokes the five-implementation comparison described in the benchmarking guide. `bench3way` remains a backwards-compatible alias.
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

**Manifest limitation:** `rbl.toml` is not yet a dependency manifest. RBL 0.8.0 does not resolve packages, download dependencies or compile imported user modules. Its currently supported package fields are `name`, `version` and `entry`.

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
python3 tests/test_build_cache.py
python3 tests/test_asm_call_alignment.py
python3 tests/test_runtime_abi.py
python3 tests/test_string_ownership.py
python3 tests/test_string_io_gc.py
python3 tests/test_diagnostics.py
```

`python rbl.py test` also runs build cleanup, GC safe-point code generation, streamed tool output, bundled-toolchain and manifest checks, the Linux launcher, POSIX metadata and wx filesystem-security/worker-lifecycle checks, DPI static checks, and (on Windows) registry/save-cleanup tests. Some checks are platform-gated and report explicit skips. `make test` is a smaller compiler/Runtime subset, not an alias for the complete CLI suite.

The [diagnostics reference](docs/eng/reference/diagnostics.md) documents compiler-stage labels, source-coordinate behavior, excerpt/caret rendering and the focused negative/edge-case suite.

In Windows PowerShell:

```powershell
python rbl.py test
```

The RBL 0.8.0 verification record covers:

- **61/61 direct RBL regression cases passed** on Windows native and Linux/WSL.
- Standard-library and input smoke tests passed on both paths.
- All **53 example `.rbl` files** passed compiler `check` on Windows native and Linux/WSL. The four new 0.8.0 examples (A* grid pathfinding, SIR/RK4 simulation, Monte Carlo pi and least-squares regression) were also run successfully on Windows native; deterministic outputs were compared across WSL/native where applicable.
- Across `examples/` and compiler-test sources in `tests/`, **121 unique `.rbl` sources** are present: 114 expected successful compilations and 7 expected negative cases. All 114 positives assemble and all 7 negatives are rejected on both targets.
- The default differential runner executes **83 selected source cases**: 61 successful deterministic programs compare exit code/stdout/stderr; 11 expected runtime-error programs must fail with matching exit code/stdout/stderr; 7 expected-negative compiler fixtures must be rejected with identical diagnostics; 4 random/platform-dependent programs must build and exit successfully on both backends (their output is not compared). All **30 diagnostic differential cases** match.
- Runtime ABI, instrumented Linux Runtime call alignment, same-basename/backend build-cache isolation, deterministic GC/ownership/OOM probes, repeated `input()`/`read_file()` under GC pressure and the 63-scenario diagnostic suite pass.
- CLI and rebuilt compiler binaries report `0.8.0`.

The regression cases cover arithmetic, floats, strings, function behavior, evaluation order, loops, ranges, containers, `null`, standard-library behavior and known compiler defects. The runner uses timeouts so a program that unexpectedly loops forever does not hang the complete suite.

### Differential testing

On Windows, with WSL and native MinGW-w64 available:

```powershell
python tests\differential.py --a wsl --b native
```

The default differential runner selects 68 files from `tests/cases/`, the standard-library smoke file, 8 examples directly under `examples/` and 6 benchmark programs directly under `benchmarks/`; it is not a recursive run of all 53 examples. It compares output for 61 successful deterministic programs and 11 expected runtime-error programs (including their non-zero exit code), requires 7 known negative fixtures to be rejected with identical compiler diagnostics, and requires 4 random/platform-dependent programs to build and exit successfully on both backends without comparing their output. Pass a directory to scan it recursively, or pass explicit `.rbl` paths for a focused comparison.

Run the tracked source corpus checker on the active backend with `python tests/check_corpus.py`; use `RBL_TOOLCHAIN=wsl` on Windows to check Linux/WSL. Run `python tests/check_documentation.py` to validate relative links, heading fragments and RU/EN page structure.

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
python3 benchmarks/compare_all.py --warmups 2 --reps 10
```

Or through the CLI:

```bash
python3 rbl.py bench5way --warmups 2 --reps 10
```

The comparison covers:

1. RBL.
2. C compiled with GCC `-O2`.
3. C++ compiled with G++ `-O2`.
4. CPython.
5. Handwritten x86-64 assembly.

It measures three synthetic integer workloads: accumulation, function calls and conditional updates. The C/C++ loop counts are supplied at runtime, and the call function is marked `noinline`, preventing the native compiler from replacing the entire measured workload with a constant result.

### Final RBL 0.8.0 five-way runtime benchmark (7 November 2026)

The post-ABI run used two warmups and ten measured executions, with the benchmark pinned to CPU 11. Values are medians:

| Workload | RBL 0.8.0 | C `-O2` | C++ `-O2` | CPython | Handwritten ASM |
|---|---:|---:|---:|---:|---:|
| `sum` | **47.885 ms** | 25.258 ms | 25.183 ms | 6,483.543 ms | 24.465 ms |
| `calls` | **21.303 ms** | 13.499 ms | 13.306 ms | 852.290 ms | 13.526 ms |
| `branches` | **103.205 ms** | 53.116 ms | 54.099 ms | 6,772.371 ms | 53.408 ms |

The run took place while a separate RENA training process was active in WSL; CPU affinity was restricted to CPU 11, but the host was not globally isolated. Treat this as a ten-sample controlled measurement under background load, not an idle-machine reference. Full ratios, samples and caveats are in [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md); the raw terminal log is retained in ignored `build/benchmark_five_way_0.8.0_final_2026-11-07.log`.

### Compiler modularity and build performance

The final 0.8.0 source was compared as one amalgamated translation unit and as seven modules. The revalidation measured a geometric-mean modular/one-TU compiler-latency ratio of 1.013× across 11 workloads (range 0.983×–1.146×), with identical generated assembly hashes. The Makefile's object-based parallel build measured 0.80 s with `-j6`, 0.81 s with `-j12`, 1.77 s serially, 0.58 s for a one-module incremental rebuild and 0.029 s for a no-op build. These are host-specific compiler/build measurements, separate from generated-program runtime. See [`benchmarks/PERFORMANCE_AUDIT.md`](benchmarks/PERFORMANCE_AUDIT.md) for methodology and limitations.

The separate [0.7.2-to-0.8.0 compiler-only comparison](benchmarks/RELEASE_PERFORMANCE.md) measures direct compiler latency (without assembler, linker or program execution). It records near-parity/slight noise-level regressions for the smallest cases (0.99× for 100 variables; 0.96× for 100 functions) and improvements up to 2.73× on larger string workloads. These results must not be confused with the five-way generated-program runtime benchmark above.

---

## Documentation

The documentation is maintained in **English and Russian** and follows [Diátaxis](https://diataxis.fr/). Both editions describe the implemented RBL 0.8.0 language, compiler, runtime and tooling contracts.

Start at [`docs/README.md`](docs/README.md) to choose a language.

### English

| Section | Purpose | Main entry |
|---|---|---|
| Beginner course | 20 guided lessons with runnable examples | [Beginner course](examples/beginner/README_ENG.md) |
| How-to | Practical workflows | [Project workflow](docs/eng/how-to/project-workflow.md), [Windows/WSL](docs/eng/how-to/windows-toolchain.md), [IDE](docs/eng/how-to/ide.md), [Testing](docs/eng/how-to/testing.md), [Benchmarking](docs/eng/how-to/benchmarking.md) |
| Reference | Exact language and tool contracts | [Syntax](docs/eng/reference/syntax.md), [Standard library](docs/eng/reference/standard-library.md), [CLI](docs/eng/reference/cli.md), [Diagnostics](docs/eng/reference/diagnostics.md), [Runtime ABI](docs/eng/reference/runtime-abi.md), [Manifest](docs/eng/reference/manifest.md), [Limitations](docs/eng/reference/limitations.md) |
| Explanation | Internal architecture and design trade-offs | [Architecture](docs/eng/explanation/architecture.md), [Execution model](docs/eng/explanation/execution-model.md), [Optimization](docs/eng/explanation/optimization.md), [Performance](docs/eng/explanation/performance.md), [Benchmark results](docs/eng/explanation/performance-results.md), [Design](docs/eng/explanation/design.md), [Roadmap](docs/eng/explanation/roadmap.md) |

### Русский

| Раздел | Назначение | Основные материалы |
|---|---|---|
| Курс для начинающих | 20 уроков с запускаемыми примерами | [Курс для начинающих](examples/beginner/README_RUS.md) |
| How-to | Практические инструкции | [Работа с проектом](docs/ru/how-to/project-workflow.md), [Windows и WSL](docs/ru/how-to/windows-toolchain.md), [IDE](docs/ru/how-to/ide.md), [Тестирование](docs/ru/how-to/testing.md), [Benchmark](docs/ru/how-to/benchmarking.md) |
| Reference | Точный синтаксис и интерфейсы | [Синтаксис](docs/ru/reference/syntax.md), [Стандартная библиотека](docs/ru/reference/standard-library.md), [CLI](docs/ru/reference/cli.md), [Диагностика](docs/ru/reference/diagnostics.md), [Runtime ABI](docs/ru/reference/runtime-abi.md), [Манифест](docs/ru/reference/manifest.md), [Ограничения](docs/ru/reference/limitations.md) |
| Explanation | Архитектура и проектные решения | [Архитектура](docs/ru/explanation/architecture.md), [Модель выполнения](docs/ru/explanation/execution-model.md), [Оптимизации](docs/ru/explanation/optimization.md), [Производительность](docs/ru/explanation/performance.md), [Результаты benchmark](docs/ru/explanation/performance-results.md), [Принципы дизайна](docs/ru/explanation/design.md), [Дорожная карта](docs/ru/explanation/roadmap.md) |

Release notes for the 0.8.0 release candidate use the **Added**, **Changed** and **Fixed** categories:

- [English release notes](docs/eng/release-notes.md)
- [Заметки о релизах на русском](docs/ru/release-notes.md)

---

## Development roadmap

The roadmap records the implemented RBL 0.8.0 baseline and the remaining path to 1.0. It separates implemented functionality from planned work and defines exit criteria; it is not a release-date promise.

| Milestone | Focus | Status in 0.8.0 |
|---|---|---|
| `0.8.0` | Compiler modularization/performance, Runtime ABI/diagnostics, string lifetime model and f-string format specifications | Implemented and validated |
| `0.9` | Explicit RBL IR, CFG/basic blocks, liveness, whole-function register allocation and IR optimizations | Planned |
| `1.0` | Source-module system and deterministic multi-file project builds | Planned; central 1.0 goal |

### RBL 0.8.0 — compiler and runtime stabilization

RBL 0.8.0 implements the modular compiler, indexed lookup, Runtime ABI, source diagnostics, managed dynamic-string lifetime and f-string formatting. See the [full performance audit](benchmarks/PERFORMANCE_AUDIT.md) for module-overhead measurements, build timings and profiled hot paths:

- Organize the compiler into `common`, `lexer`, `ast`, `parser`, `sema`, `codegen` and `driver` modules with a private shared header.
- Use hash indexes for variable, function and string-pool lookups while preserving deterministic output and duplicate-function semantics.

F-string format specifications use the grammar and conversion rules in the [syntax reference](docs/eng/reference/syntax.md). The regression and diagnostic suites cover supported formats, parser limits, runtime incompatibilities, repeated formatting under GC pressure and Linux/Windows parity.

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

The following are important boundaries of the v0.8.0 implementation:

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
- Logical `and` and `or` do not short-circuit.
- There is no source-level debugger or complete Windows source line mapping.
- There is no explicit RBL IR/CFG or whole-function register allocator yet.

### Runtime and tooling

- Conservative string collection may retain unreachable allocations while stale pointer-shaped values remain in scanned stack memory; reclamation timing is nondeterministic.
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
│   ├── common.c                # diagnostics, allocation and hashing
│   ├── lexer.c                 # UTF-8 lexer
│   ├── ast.c                   # AST structures/helpers
│   ├── parser.c                # recursive-descent parser
│   ├── sema.c                  # semantic analysis and indexes
│   ├── codegen.c               # direct x86-64 ASM backend
│   ├── driver.c                # CLI and compiler pipeline
│   └── rblc_internal.h         # private module interfaces
│
├── runtime/
│   ├── rbl_runtime.s           # Linux platform runtime
│   ├── rbl_runtime_win.c       # Windows platform runtime
│   ├── rbl_containers.c        # Shared container runtime and collector
│   ├── rbl_abi.c               # Runtime ABI descriptor and compatibility check
│   └── rbl_abi.h               # Versioned Runtime ABI contract and value layout
│
├── bin/
│   ├── linux-x86_64/           # Linux compiler/runtime objects
│   └── win-x86_64/             # Windows compiler/runtime objects
│
├── toolchain/windows-x86_64/   # Minimal private MinGW-w64/UCRT64 user-build toolchain
│
├── tools/
│   ├── rbltool.py              # Lower-level toolchain driver
│   └── visualize_examples.py   # Optional scientific-example visualizer
│
├── tests/                      # Regression, smoke and differential tests
├── benchmarks/                 # Benchmark runners and recorded results
├── examples/                   # Language and scientific/data examples
│   ├── beginner/                # 20-lesson RBL language course (RU/EN guides)
│   ├── compiler/                # Compiler lookup behavior demonstration
│   ├── algorithms/              # Sorting, searching, A* pathfinding and primes
│   ├── fractals/                # Mandelbrot and Newton renderers
│   ├── generators/              # Procedural image/data generators
│   ├── numerical/               # Root finding, regression, Monte Carlo and ODE solvers
│   ├── strings/                 # F-string formatting example
│   ├── simulations/             # Game of Life, pendulum, N-body gravity and SIR
│   ├── statistics/              # ShuffleLab and descriptive statistics
│   └── utilities/               # Interactive unit converter
├── docs/
│   ├── eng/                    # English documentation
│   └── ru/                     # Russian documentation
│
├── rbl.py                      # Main project CLI
├── rbl                         # Bash wrapper
├── rbl.cmd                     # Windows wrapper
├── Cargo.toml                  # Native Rust IDE package
├── Cargo.lock                  # Locked IDE dependencies
├── src/                        # Native RBL Studio application
├── RBLStudio.sh                # Linux IDE launcher
├── RBLStudio.cmd               # Windows IDE launcher
├── VERSION                     # Repository/compiler version
├── requirements-visualize.txt  # Optional Python visualization dependencies
├── RELEASE_MANIFEST.txt        # RBL 0.8.0 release-candidate manifest
└── README.md
```

The tree above is an orientation map, not a complete file inventory.

---

## License

RBL-authored source code and project documentation are licensed under the **Apache License 2.0**. That license does not automatically cover third-party binaries shipped beside the project.

See [`LICENSE`](LICENSE) for the complete license text.

The portable Windows toolchain is a separately licensed third-party component. Its package versions, observed license metadata and included license texts are listed in [`toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md`](toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md). The bundle includes GPL/LGPL/GCC-exception, ZPL, MIT/BSD, zlib and other applicable notices; redistribution requires a separate corresponding-source/source-offer compliance review. Do not treat the repository's Apache-2.0 license or the SHA-256 manifest as relicensing or legal clearance for those binaries.
