# RBL

**Reiwa Bat Language (RBL)** is a compiled programming language with a native IDE, project CLI, direct x86-64 assembly backend and its own runtime.

Current release: **RBL v0.7.1**

RBL is designed as a compact native language/toolchain with a straightforward compilation pipeline:

```text
RBL source
    ↓
lexer
    ↓
recursive-descent parser
    ↓
AST
    ↓
static analysis
    ↓
optimization
    ↓
x86-64 assembly
    ↓
object file
    ↓
RBL runtime + system libraries
    ↓
native executable
```

The repository contains the compiler, runtime, IDE, CLI, tests, benchmarks, installers, examples and technical documentation required to develop and run RBL programs.

---

# Contents

- [What is RBL](#what-is-rbl)
- [Current feature set](#current-feature-set)
- [Quick start](#quick-start)
- [Targets and toolchains](#targets-and-toolchains)
- [Compiler architecture](#compiler-architecture)
- [Runtime and value representation](#runtime-and-value-representation)
- [RBL language](#rbl-language)
- [Types and values](#types-and-values)
- [Control flow](#control-flow)
- [Functions](#functions)
- [Collections](#collections)
- [Strings](#strings)
- [Standard library](#standard-library)
- [RBL Studio](#rbl-studio)
- [RBL CLI](#rbl-cli)
- [Project files](#project-files)
- [Compiler and low-level tools](#compiler-and-low-level-tools)
- [Optimization](#optimization)
- [Testing](#testing)
- [Differential testing](#differential-testing)
- [Benchmarks](#benchmarks)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Building the compiler](#building-the-compiler)
- [Current limitations](#current-limitations)
- [Roadmap](#roadmap)
- [License](#license)

---

# What is RBL

RBL is a native compiled programming language.

The normal execution path is:

```text
source.rbl
    ↓
compiler
    ↓
assembly
    ↓
object
    ↓
link
    ↓
native executable
```

The compiler works directly with the RBL syntax tree and emits x86-64 assembly. User programs do not pass through a generated C source file.

RBL currently targets:

- **Linux x86-64**
- **Windows x86-64**

The same compiler frontend, optimizer and assembly backend are used for both targets where possible. Platform-specific pieces are handled by the target runtime, object format and linker setup.

The project also provides:

- a graphical IDE;
- a project-oriented CLI;
- standalone compiler/runtime binaries;
- a regression test suite;
- cross-backend differential testing;
- benchmark tooling;
- installation helpers;
- documentation for the compiler, runtime and IDE.

---

# Current feature set

## Language

RBL currently supports:

- integers;
- floating-point values;
- booleans;
- strings;
- `Unit`;
- `null`;
- functions;
- recursion;
- lists;
- dictionaries;
- tuples;
- structs;
- ranges;
- `if`;
- conditional expressions;
- `switch`;
- `for`;
- `break`;
- `continue`;
- compound assignment;
- increment operations;
- comparisons;
- arithmetic;
- modulo;
- logical operations;
- Unicode identifiers;
- comments;
- numeric literal extensions;
- string escape sequences;
- multiline strings;
- interpolated strings.

## Compiler

The current compiler provides:

- lexer;
- recursive-descent parser;
- AST construction;
- semantic/static analysis;
- direct x86-64 assembly generation;
- typed scalar fast paths;
- loop optimization;
- local register allocation;
- runtime integration;
- Linux ELF output;
- Windows PE/COFF output.

## Toolchain

The repository contains:

- `rblc-asm` compiler binaries;
- Linux x86-64 runtime objects;
- Windows x86-64 runtime objects;
- the `rbl` project CLI;
- the lower-level `rbltool.py` driver;
- RBL Studio;
- test runners;
- benchmark runners;
- installers and diagnostics.

## IDE

RBL Studio provides:

- tabs;
- line numbers;
- syntax highlighting;
- indentation helpers;
- undo/redo;
- automatic syntax checking;
- inline problems;
- build/run integration;
- console output;
- generated assembly viewing;
- test execution;
- benchmark execution;
- toolchain diagnostics;
- persistent settings;
- Windows file association;
- Linux desktop integration.

---

# Quick start

## Windows

Launch RBL Studio:

```text
RBLStudio.cmd
```

Check the active compiler/toolchain:

```powershell
python rbl.py doctor
```

Run an example:

```powershell
python rbl.py run examples\test.rbl
```

Check the Windows/WSL integration:

```powershell
python tools\windows_wsl_smoke.py
```

## Linux

Launch RBL Studio:

```bash
./RBLStudio.sh
```

Check the environment:

```bash
python3 tools/ide_doctor.py
```

Run an example:

```bash
./rbl run examples/test.rbl
```

Run the regression suite:

```bash
python3 tests/run_tests.py
```

---

# Targets and toolchains

## Linux x86-64

The Linux backend produces ELF64 executables.

Main tools:

```text
GNU as
GNU ld
libc
libm
```

Runtime:

```text
runtime/rbl_runtime.s
```

The bundled compiler is:

```text
bin/linux-x86_64/rblc-asm
```

The bundled runtime object is:

```text
bin/linux-x86_64/rbl_runtime.o
```

Low-level example:

```bash
bin/linux-x86_64/rblc-asm examples/test.rbl \
    -S build/test/test.s

as --64 build/test/test.s \
    -o build/test/test.o

ld build/test/test.o \
   bin/linux-x86_64/rbl_runtime.o \
   -o build/test/test \
   -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
   -lc -lm
```

## Windows x86-64

The Windows backend produces PE/COFF executables.

Native toolchain:

```text
MinGW-w64
gcc
GNU as
```

Runtime:

```text
runtime/rbl_runtime_win.c
```

Bundled tools:

```text
bin/win-x86_64/rblc-asm.exe
bin/win-x86_64/rbl_containers_win.o
bin/win-x86_64/rbl_runtime_win.o
```

Low-level example:

```powershell
bin\win-x86_64\rblc-asm.exe examples\test.rbl `
    -S build\test\test.s `
    --target win

as --64 build\test\test.s `
    -o build\test\test.o

gcc build\test\test.o `
    bin\win-x86_64\rbl_runtime_win.o `
    -o build\test\test.exe `
    -static -lm
```

## Windows toolchain selection

RBL supports:

```text
auto
native
wsl
```

`auto` prefers the native Windows toolchain and can fall back to WSL when appropriate.

Explicit selection can be made with the project/IDE setting or:

```text
RBL_TOOLCHAIN
```

WSL configuration:

```text
RBL_WSL_DISTRO
```

The default WSL distribution is:

```text
Ubuntu-24.04
```

---

# Compiler architecture

The current compiler pipeline is:

```text
RBL source
   │
   ▼
Lexer
   │
   ▼
Recursive-descent parser
   │
   ▼
AST
   │
   ▼
Static analysis
   │
   ▼
Optimization
   ├── typed scalar fast paths
   ├── loop optimization
   └── local register allocation
   │
   ▼
x86-64 assembly generation
   │
   ▼
.s
   │
   ▼
GNU assembler
   │
   ▼
.o
   │
   ▼
RBL runtime + system libraries
   │
   ▼
native executable
```

## Lexer

The lexer converts RBL source text into the tokens consumed by the parser.

The current language includes:

- identifiers;
- keywords;
- integer literals;
- floating-point literals;
- string literals;
- interpolation syntax;
- operators;
- punctuation;
- comments.

Unicode identifiers are supported.

## Parser

RBL uses a recursive-descent parser.

The parser builds the AST used by the remaining compiler pipeline.

Parentheses are the canonical block syntax in RBL 0.7.

Example:

```rbl
func main() (
    let x = 10

    if (x > 5) (
        print("large")
    )
)
```

Curly-brace blocks remain accepted as legacy syntax.

## Static analysis

Static analysis is used to determine properties that can be proven before code generation.

The optimizer can take advantage of information such as:

- statically known integer values;
- statically known boolean values;
- eligible loop induction variables;
- integer-only function paths;
- expression types.

When a value cannot be safely specialized, the compiler falls back to the general runtime representation.

## Code generation

The current backend emits x86-64 assembly directly.

Generated code is then assembled and linked using the target platform toolchain.

---

# Runtime and value representation

RBL uses a compact tagged representation for general runtime values.

Expression results use two registers:

```text
rax = value tag
rdx = value payload
```

Current tags:

```text
0 = Unit
1 = Int(i64)
2 = Float(f64 bits)
3 = String pointer
4 = Bool
```

The runtime additionally supports complex values such as:

```text
lists
dictionaries
tuples
structs
functions
ranges
null
```

The runtime is responsible for the operations that cannot be fully represented by primitive machine values alone, including dynamic strings, containers and I/O.

## Local storage

Local variables are represented using numbered slots.

The current slot layout is:

```text
+0   bound flag
+8   type/value tag
+16  payload
```

The exact low-level representation is an implementation detail of the compiler/runtime and may evolve without changing the source language.

---

# RBL language

## Blocks

Canonical syntax:

```rbl
func main() (
    print("hello")
)
```

Nested blocks:

```rbl
func main() (
    let x = 10

    if (x > 0) (
        print("positive")

        if (x > 5) (
            print("large")
        )
    )
)
```

## Variables

The language uses explicit variable declarations.

Example:

```rbl
let count = 10
```

The implementation also contains assignment/declaration forms used by the current language semantics and test suite.

## Comments

Line comments use:

```rbl
# this is a comment
```

## Numeric literals

RBL supports decimal values as well as additional integer literal forms:

```rbl
let a = 0xFF
let b = 0b1010
let c = 0o755
let d = 1_000_000
let e = 1e42
let f = 1.5e-3
```

---

# Control flow

## if

```rbl
if (x > 10) (
    print("greater")
)
```

## if / else

```rbl
if (x > 10) (
    print("greater")
) else (
    print("not greater")
)
```

## Conditional expression

```rbl
let state = "yes" if (ok) else "no"
```

## for

Canonical range example:

```rbl
for (i in 0..10) (
    print(i)
)
```

Exclusive range:

```text
0..10
```

Inclusive range:

```text
0..=10
```

The compiler also supports the current unified loop forms tested by the regression suite.

## break

```rbl
for (i in 0..100) (
    if (i == 10) (
        break
    )
)
```

## continue

```rbl
for (i in 0..100) (
    if (i % 2 == 0) (
        continue
    )

    print(i)
)
```

## switch

Example:

```rbl
switch value (
    1: print("one")
    2: print("two")
    default: print("other")
)
```

---

# Functions

RBL supports functions, typed parameters, return values and recursion.

Example:

```rbl
func add(a: int, b: int) (
    return a + b
)

func main() (
    let result = add(10, 20)
    print(result)
)
```

Recursive example:

```rbl
func factorial(n: int) (
    if (n <= 1) (
        return 1
    )

    return n * factorial(n - 1)
)
```

The compiler performs argument and parameter checks according to the current language semantics.

The regression suite also contains tests for:

- function arity;
- parameter types;
- recursive functions;
- argument evaluation order;
- duplicate function declarations;
- return behavior.

---

# Collections

RBL supports several container/value types.

## Lists

Lists support literals, indexing, assignment and iteration.

Example:

```rbl
let values = [10, 20, 30]

print(values[0])
values[1] = 25
```

## Dictionaries

Dictionaries support key/value storage and lookup.

Supported key categories in the current test suite include:

```text
integer
float
string
```

Example:

```rbl
let user = {
    "name": "Reiwa"
}

print(user["name"])
```

## Tuples

Tuples provide grouped values.

```rbl
let pair = (10, 20)
```

## Structs

Structs provide structured records with fields and current receiver-method support.

The exact syntax is defined in the language/reference documentation and regression tests.

---

# Strings

RBL strings support:

- ordinary string literals;
- escape sequences;
- multiline strings;
- interpolation;
- concatenation.

## Escape sequences

The current implementation supports:

```text
\n
\t
\r
\\
\"
\xNN
\uXXXX
\UXXXXXXXX
```

NUL characters are rejected by the current parser/runtime path.

## Multiline strings

Multiline strings use triple quotes:

```rbl
let message = """
line one
line two
line three
"""
```

## Interpolation

Interpolated strings use the `f"..."` form:

```rbl
let name = "RBL"
let version = 7

print(f"{name} v0.{version}")
```

Expression parts are evaluated and converted through the normal string conversion path.

---

# Types and values

The current runtime value model includes:

```text
Unit
Int(i64)
Float(f64)
Bool
String
null
```

Additional language-level values include:

```text
List
Dictionary
Tuple
Struct
Function
Range
```

The runtime can therefore represent both primitive machine-friendly values and dynamic language objects.

## Unit

`Unit` represents the absence of a meaningful value.

The current behavior prints it as an empty string.

## Null

`null` is a distinct language value and is covered by the current regression suite.

## Integer and floating-point values

The direct optimizer can specialize integer and boolean code when the compiler can prove the required types.

Dynamic or uncertain expressions use the general tagged runtime path.

---

# Standard library

RBL provides global built-ins:

```text
print
len
input
read_file
write_file

abs
sqrt
min
max

int
float
str

pow
floor
ceil
round

sin
cos
tan
log
exp

random_int
random_float
random_bool
```

## Namespaces

```text
rbl.io
rbl.math
rbl.string
rbl.fs
rbl.time
rbl.random
```

See:

```text
docs/STDLIB.md
```

for the detailed standard-library reference.

## I/O

Examples:

```rbl
let name = input("Name: ")
print(name)
```

File access:

```rbl
let text = read_file("example.txt")
write_file("copy.txt", text)
```

## Conversion

```rbl
int(x)
float(x)
str(x)
```

These functions convert supported scalar values.

---

# RBL Studio

RBL Studio is the graphical development environment included with the repository.

It is implemented with Python and Tkinter and has no third-party Python dependency for normal operation.

## Editor

The editor provides:

- multiple tabs;
- line numbers;
- RBL syntax highlighting;
- indentation helpers;
- undo/redo;
- scrolling;
- persistent font size;
- persistent tab size;
- automatic syntax checking;
- automatic save options.

## Output area

The bottom panel provides:

```text
Run
Console
Problems
```

The Run action performs the complete build/run workflow for the selected source.

## Build and run

`F5`:

```text
save
 ↓
syntax check
 ↓
compile
 ↓
assemble
 ↓
link
 ↓
execute
 ↓
show stdout/stderr
```

## Assembly viewer

RBL Studio can show generated `.s` files, which makes it possible to inspect the output of the direct assembly backend without leaving the IDE.

## Toolchain Doctor

The IDE/toolchain diagnostics can check:

- active backend;
- compiler availability;
- assembler;
- linker;
- Windows native toolchain;
- WSL configuration;
- basic path/tool integration.

## Themes

Current themes:

```text
Dark
Light
Nord
Monokai
```

## IDE shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `F5` | Build + run |
| `Ctrl+B` | Build direct ASM |
| `Ctrl+Shift+B` | Syntax check |
| `Ctrl+T` | Run tests |
| `Ctrl+Shift+T` | Run benchmarks |

---

# RBL CLI

The `rbl` command is the preferred command-line entry point for project work.

Available commands:

```text
new
init
check
build
run
asm
test
bench
fmt
doc
clean
doctor
env
version
```

## new

Create a project:

```bash
rbl new hello
```

## init

Initialize an existing directory:

```bash
rbl init
```

## check

Check a project without running it:

```bash
rbl check
```

## build

Compile the project:

```bash
rbl build
```

## run

Compile and run:

```bash
rbl run
```

## asm

Generate assembly output:

```bash
rbl asm
```

## test

Run project tests:

```bash
rbl test
```

## bench

Run benchmarks:

```bash
rbl bench
```

Optional benchmark settings:

```bash
rbl bench --reps 5 --warmups 1
```

## fmt

Format project source:

```bash
rbl fmt
```

## doc

Build/generate project documentation:

```bash
rbl doc
```

## clean

Remove generated build output:

```bash
rbl clean
```

## doctor

Inspect the active environment/toolchain:

```bash
rbl doctor
```

## env

Inspect project/toolchain environment data:

```bash
rbl env
```

## version

Print the current toolchain version:

```bash
rbl version
```

---

# Project files

A project uses an `rbl.toml` manifest.

Example:

```toml
[package]
name = "hello"
version = "0.1.0"
entry = "src/main.rbl"
```

The manifest identifies:

- package name;
- package version;
- entry source.

Project build output is kept under:

```text
build/
```

Typical generated files:

```text
build/<program>/
    program.s
    program.o
    program
```

---

# Low-level compiler tools

The project also includes the lower-level driver:

```text
tools/rbltool.py
```

Examples:

```bash
python3 tools/rbltool.py doctor

python3 tools/rbltool.py check examples/test.rbl

python3 tools/rbltool.py build examples/test.rbl

python3 tools/rbltool.py run examples/test.rbl

python3 tools/rbltool.py asm examples/test.rbl
```

This interface is useful for:

- scripts;
- integration work;
- compiler development;
- debugging the toolchain;
- reproducing low-level build steps.

---

# Direct compiler usage

The bundled Linux compiler is:

```text
bin/linux-x86_64/rblc-asm
```

The bundled Windows compiler is:

```text
bin/win-x86_64/rblc-asm.exe
```

The compiler accepts an RBL source file and can emit assembly.

Linux:

```bash
bin/linux-x86_64/rblc-asm \
    examples/test.rbl \
    -S build/test/test.s
```

Windows:

```powershell
bin\win-x86_64\rblc-asm.exe `
    examples\test.rbl `
    -S build\test\test.s `
    --target win
```

---

# Optimization

The current backend performs several classes of optimization before assembly generation.

## Typed scalar fast paths

When the compiler can statically prove that a value is an eligible scalar type, it can avoid part of the general dynamic runtime path.

The main target is:

```text
int
bool
```

This allows simple arithmetic, boolean and loop-heavy code to use a smaller and faster representation.

## Loop optimization

The current backend includes loop-specific optimization for cases that can be proven safe.

This includes:

- induction-variable analysis;
- optimized integer loops;
- register-held induction variables;
- hot local placement;
- loop-local register allocation;
- overflow checks required by the optimized representation.

## Local register allocation

Register allocation is currently local to optimized loops.

The compiler uses available callee-saved x86-64 registers for selected loop locals and induction variables.

Current flow:

```text
AST
 ↓
static analysis
 ↓
loop optimization
 ↓
local register allocation
 ↓
x86-64 ASM
```

This is deliberately conservative: code that cannot be safely specialized falls back to the general runtime representation.

---

# Runtime ABI

General expression results are represented as:

```text
rax = tag
rdx = payload
```

Current tag values:

```text
0 = Unit
1 = Int
2 = Float
3 = String
4 = Bool
```

Local slots:

```text
+0   bound flag
+8   tag
+16  payload
```

The runtime contains the platform-specific implementation needed by generated programs.

Linux runtime:

```text
runtime/rbl_runtime.s
```

Windows runtime:

```text
runtime/rbl_runtime_win.c
runtime/rbl_containers.c
```

Bundled runtime objects are available under:

```text
bin/linux-x86_64/
bin/win-x86_64/
```

---

# Testing

The repository includes a dedicated regression suite.

Run it with:

```bash
python3 tests/run_tests.py
```

The current suite contains **57 cases**.

Coverage includes:

- arithmetic;
- precedence;
- integers;
- floating-point operations;
- strings;
- escapes;
- multiline strings;
- interpolation;
- assignments;
- increments;
- loops;
- `break`;
- `continue`;
- functions;
- recursion;
- argument evaluation;
- parameter checks;
- ranges;
- modulo;
- switch;
- lists;
- dictionaries;
- tuples;
- structs;
- `null`;
- equality;
- comparisons;
- standard library behavior;
- Unicode identifiers;
- Unit values;
- logging;
- expected errors.

The suite includes regression cases for compiler defects found during implementation.

Every executable test is run with a timeout.

This prevents an accidental compiler/runtime regression into an endless loop from hanging the complete suite.

---

# Differential testing

RBL can compare its supported backend paths against one another.

Run:

```bash
python3 tests/differential.py --a wsl --b native
```

The test driver builds and executes the same programs through both paths and compares:

```text
exit code
stdout
stderr
```

The comparison covers:

- regression cases;
- standard-library smoke tests;
- examples;
- benchmarks.

Platform-dependent programs are handled separately where exact output cannot be expected to match.

---

# Benchmarks

The repository contains a dedicated benchmark suite.

Run:

```bash
python3 benchmarks/run_benchmarks.py
```

or:

```bash
rbl bench
```

The main benchmark fields are:

```text
build
run
net
```

Definitions:

```text
build = compile + assemble + link
run   = complete process execution
net   = run - baseline
```

The baseline measures an empty/minimal process so that startup overhead can be separated from the actual workload.

Results are stored in:

```text
benchmarks/RESULTS.md
```

## Three-way comparison

The repository also contains a Linux-only comparison involving:

```text
RBL native ASM
CPython
hand-written x86-64 ASM
```

Run:

```bash
./benchmarks/compare_pure_asm.sh
```

The comparison is intended for relative measurements on the same machine rather than universal performance claims.

---

# Repository layout

```text
RBL/
├── compiler/
│   └── rblc_asm.c
│
├── runtime/
│   ├── rbl_runtime.s
│   ├── rbl_containers.c
│   └── rbl_runtime_win.c
│
├── bin/
│   ├── linux-x86_64/
│   │   ├── rblc-asm
│   │   ├── rbl_containers.o
│   │   └── rbl_runtime.o
│   │
│   └── win-x86_64/
│       ├── rblc-asm.exe
│       ├── rbl_containers_win.o
│       └── rbl_runtime_win.o
│
├── ide/
│   ├── rbl_studio.py
│   └── launch.py
│
├── tools/
│   ├── rbltool.py
│   ├── ide_doctor.py
│   ├── gui_probe.py
│   ├── ide_smoke.py
│   ├── windows_wsl_smoke.py
│   └── ...
│
├── tests/
├── benchmarks/
├── examples/
├── docs/
├── installer/
├── assets/
├── artifacts/
├── reference/
│
├── rbl
├── rbl.py
├── RBLStudio.py
├── Makefile
├── VERSION
├── README.md
└── LICENSE
```

---

# Documentation

The repository contains separate documentation for different parts of the project.

## Compiler and backend

```text
docs/CURRENT_IMPLEMENTATION.md
docs/ASM_BACKEND.md
docs/OPTIMIZER.md
```

## Language

```text
docs/SYNTAX_V07.md
docs/SYNTAX_DECISIONS.md
docs/RBL_QUICK_REFERENCE.md
docs/STDLIB.md
```

## IDE and tooling

```text
docs/IDE_MANUAL.md
docs/TOOLING.md
docs/WINDOWS.md
docs/GUI_TROUBLESHOOTING.md
```

## Testing and development

```text
docs/TESTING.md
docs/ROADMAP.md
docs/RELEASE_NOTES.md
```

## Additional technical material

```text
docs/QUESTIONS_ANSWERS_SOLUTION.md
docs/RUSSIAN_AUDIT.md
```

---

# Building the compiler

The repository ships prebuilt compiler and runtime binaries.

Normal RBL development therefore does not require rebuilding `rblc-asm`.

The compiler implementation is located at:

```text
compiler/rblc_asm.c
```

The runtime implementations are located under:

```text
runtime/
```

The project provides a rebuild helper:

```bash
tools/rebuild_compiler.sh
```

The C compiler is used for building the compiler/runtime implementation itself. RBL user programs are still compiled by the direct assembly backend.

---

# Tooling and diagnostics

Several tools are included for debugging the project itself.

## IDE diagnostics

```bash
python3 tools/ide_doctor.py
```

## GUI probe

```bash
python3 tools/gui_probe.py
```

## GUI smoke test

```bash
python3 tools/ide_smoke.py
```

## Windows/WSL smoke test

```powershell
python tools\windows_wsl_smoke.py
```

## Setup helper

```bash
bash tools/setup_wsl.sh
```

The tools are intended to make environment/toolchain failures easier to distinguish from compiler failures.

---

# Windows → WSL bridge

When the WSL backend is selected, RBL Studio uses the configured WSL distribution and converts Windows paths before invoking Linux-side tools.

The default distribution is:

```text
Ubuntu-24.04
```

The bridge normalizes host paths before calling `wslpath`.

The bundled Linux `rblc-asm` and runtime objects can be used without rebuilding the compiler.

To explicitly rebuild from source:

```text
rbl bootstrap
```

or:

```text
RBL_REBUILD_COMPILER=1
```

The native Windows backend does not require WSL for normal Windows builds.

---

# Current implementation status

RBL v0.7.x currently provides a working:

```text
language
+
compiler
+
runtime
+
IDE
+
CLI
+
test suite
+
benchmark suite
```

The main native compilation target is x86-64.

Linux uses ELF64.

Windows uses PE/COFF.

The Windows path is native-first, with WSL available as a fallback.

The compiler already performs direct assembly generation and selected low-level optimization, while the project still has several planned compiler architecture steps before the optimizer is fully global.

---

# Current limitations

Current limitations include:

1. x86-64 is the only implemented CPU architecture.
2. Supported operating-system targets are Linux x86-64 and Windows x86-64.
3. There is no source-level debugger yet.
4. Generated Windows binaries currently do not contain source line tables.
5. Dynamic values continue to use the general tagged runtime representation.
6. Register allocation is currently local to optimized loops.
7. The optimizer is conservative around dynamic values and uncertain types.
8. Float formatting is not yet identical to every detail of the reference formatting behavior.
9. The next major compiler architecture step is a dedicated RBL IR/CFG/liveness pipeline.

---

# Roadmap

The next major backend architecture is planned as:

```text
AST
 ↓
RBL IR
 ↓
CFG / basic blocks
 ↓
liveness analysis
 ↓
register allocation
 ↓
x86-64 assembly
```

The IR stage is intended to make several optimizations easier to implement cleanly:

- constant folding;
- dead-code elimination;
- stronger register allocation;
- control-flow analysis;
- more global optimization;
- additional future backends.

The current roadmap and implementation priorities are documented in:

```text
docs/ROADMAP.md
```

---

# License

RBL is released under the **Apache License 2.0**.

The license permits use, modification, redistribution and commercial use subject to the terms of Apache-2.0.

See:

```text
LICENSE
```

for the complete license text.
