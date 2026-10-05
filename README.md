# RBL Studio

RBL Studio is a native IDE and toolchain for **Reiwa Bat Language (RBL v0.7.0)**, keeping compatibility with the current Rust-language surface while adding a direct-ASM optimizer and a project CLI.

It is designed around the existing Rust implementation, but the execution path is now a **direct x86-64 assembly backend**.

The backend targets **two platforms from one code generator**: Linux x86-64 ELF (assembled with GNU `as` and linked with GNU `ld`) and native Windows x86-64 PE/COFF (assembled with GNU `as` and linked with MinGW-w64 `gcc`). WSL is supported as a fallback when the native Windows toolchain is unavailable or explicitly selected. See [docs/WINDOWS.md](docs/WINDOWS.md).

The important distinction from the previous prototype is:

```text
RBL source
   ↓
lexer → recursive-descent parser → AST
   ↓
static analysis → loop optimization → local register allocation
   ↓
direct x86-64 code generation
   ↓
.rbl → .s
   ↓
GNU as --64
   ↓
.o
   ↓
link + runtime
   ↓
native executable
```

There is **no generated C program and no GCC/Clang in the user-program build path on Linux**. On Windows, MinGW-w64 `gcc` is used as the native linker driver and to build the Windows runtime object; it does not receive generated C source from an RBL program.

The repository includes bundled compiler/runtime binaries, so normal use does not require rebuilding `rblc-asm`.

### Windows: native backend first, WSL as fallback

The Windows GUI is native Tkinter, and the toolchain is native-first too: RBL Studio finds a MinGW-w64 GNU toolchain (`gcc.exe` + `as.exe`, usually from MSYS2) and assembles/links the generated PE image locally. WSL is used only when no native toolchain is present, or when it is selected explicitly.

```powershell
python rbl.py doctor                  # toolchain + active backend
python rbl.py run examples\test.rbl   # compile -> as -> gcc -> .exe -> run
```

Selection is controlled by the `Windows toolchain` setting (`auto` / `native` / `wsl`) or by the `RBL_TOOLCHAIN` environment variable, which wins. Windows 7/8.1 should use the msvcrt-based `mingw64` environment; Windows 10/11 can use `ucrt64`. Full details, requirements and troubleshooting: [docs/WINDOWS.md](docs/WINDOWS.md).

When WSL is used, the distro is `Ubuntu-24.04` by default and can be overridden with `RBL_WSL_DISTRO` or the `wsl_distro` setting. Diagnose that path with:

```powershell
python tools\windows_wsl_smoke.py
```

## What is included

- RBL Studio editor built with Python + Tkinter, with no third-party Python dependencies.
- Python-IDLE-style text editor with tabs, line numbers, syntax highlighting, indentation helpers, undo/redo and scrolling.
- PyCharm-style bottom output area with `Run`, `Console`, and `Problems` tabs.
- `F5` one-key build + run.
- Built-in syntax checking and inline problem highlighting.
- Built-in 57-case core regression suite plus dedicated stdlib/input smoke tests.
- Built-in benchmark suite with build time, runtime, min/max and binary size.
- Four themes: Dark, Light, Nord, Monokai.
- Persistent settings for theme, font size, tab size, auto-check, auto-save, WSL mode and line numbers.
- Generated assembly viewer.
- Toolchain Doctor.
- `.rbl` file icon.
- Windows per-user file association installer/uninstaller.
- Linux MIME/desktop integration installer/uninstaller.
- Direct x86-64 ASM compiler and standalone assembly runtime.
- Native Windows PE output through MinGW-w64, with WSL as an automatic fallback.
- `tests/differential.py` for cross-backend regression comparison.
- Typed scalar fast-path optimizer for statically provable `int`/`bool` code.
- Loop-local register allocation using callee-saved x86-64 registers.
- Fast integer loops with register-held induction variables and hot locals.
- Direct integer-only function calls.
- Pure-ASM comparison benchmark under `benchmarks/compare_pure_asm.sh`.
- `rbl` project CLI with `new`, `init`, `check`, `build`, `run`, `asm`, `test`, `bench`, `fmt`, `doc`, `clean`, `doctor`, `env` and `version`.
- Original Rust implementation copied under `reference/Rust_v0.1/` for auditing.

## Platform model

The direct backend targets:

- x86-64
- Linux ELF64 with the System V AMD64 ABI
- Windows PE/COFF with the Microsoft x64 entry convention

The RBL value ABI, the local slots and every generated RBL function use the System V convention on **both** targets, so a single code generator and a single optimizer serve both. Differences are limited to the object format, the entry/exit glue and the runtime:

| | Linux | Windows |
|---|---|---|
| Object | ELF64 | PE/COFF |
| Assembler | GNU `as --64` | GNU `as --64` |
| Link | GNU `ld` + `libc`/`libm` | MinGW-w64 `gcc` (linker driver) |
| Runtime | `runtime/rbl_runtime.s` (hand-written asm) | `runtime/rbl_runtime_win.c` (C with `__attribute__((sysv_abi))`) |
| Entry | `_start` | `main` (Microsoft x64) |

No C source is generated for user programs on either target. On Windows the C compiler builds the runtime object and acts as the linker driver only.

This section deliberately avoids pretending that Windows is a bolt-on: the PE path is a first-class target that shares the frontend, the optimizer and the code generator with the ELF path, and both are checked against each other by `tests/differential.py`. A source-level debugger and line tables are still roadmap items.

## Start the IDE

### Windows

Run:

```text
RBLStudio.cmd
```

For `.rbl` double-click integration, run PowerShell as your normal user and execute:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\installer\install_windows.ps1
```

After that, `.rbl` files use the RBL Studio icon and open directly in the IDE.

For the Linux x86-64 backend on Windows, select the `wsl` backend or allow `auto` to fall back to WSL when the native toolchain is unavailable. The bundled `rblc-asm` and `rbl_runtime.o` are already present; only `as` and `ld` are needed in WSL for ordinary Linux builds.

A minimal WSL check is:

```bash
bash tools/setup_wsl.sh
```

If `binutils` is missing, install it in the selected WSL distro.

To remove the Windows association:

```powershell
.\installer\uninstall_windows.ps1
```

### Linux

Run:

```bash
./RBLStudio.sh
```

If the IDE ever fails to start, run the diagnostic directly:

```bash
python3 tools/ide_doctor.py
```

Startup failures are also written to `ide_startup.log`.

For `.rbl` double-click integration:

```bash
./installer/install_linux.sh
```

To remove the Linux integration:

```bash
./installer/uninstall_linux.sh
```

## IDE controls

| Key | Action |
|---|---|
| `Ctrl+N` | New RBL file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save |
| `F5` | Build + run |
| `Ctrl+B` | Build direct ASM |
| `Ctrl+Shift+B` | Syntax check |
| `Ctrl+T` | Run regression tests |
| `Ctrl+Shift+T` | Run benchmarks |

The Run button does not require a terminal command. It saves the source, compiles it to assembly, assembles, links, runs the executable and sends stdout/stderr to the bottom pane.

## Current block syntax

RBL 0.7 uses parentheses as the canonical block delimiter:

```rbl
func main() (
    if (x > 0) (
        print(x)
    )
    for (i in 0..10) (
        print(i)
    )
)
```

The parser still accepts `{ ... }` as legacy syntax, but new code and the formatter use parentheses.

## Standard library

RBL ships these global built-ins:

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

Namespaces:

```text
rbl.io
rbl.math
rbl.string
rbl.fs
rbl.time
rbl.random
```

`len` counts UTF-8 code points. File functions operate on text. `min/max` require all arguments to be numeric and of one common type.

See `docs/STDLIB.md` for the complete reference.

## Current language compatibility

The backend intentionally follows the behavior of the current Rust interpreter, including several details that are easy to accidentally change during a compiler rewrite:

- `set` checks duplicate declaration before evaluating its RHS.
- `let` evaluates its RHS before reporting an unknown variable or type mismatch.
- `and` and `or` evaluate both operands; they are not short-circuiting.
- Function arity is checked before argument expressions are evaluated.
- Function parameter type checks occur after argument evaluation.
- Duplicate function declarations use the last definition.
- Blocks share the function environment rather than creating a new variable map.
- A loop variable persists after a loop that actually executed.
- `0..n` is exclusive; `0..=n` is inclusive.
- `Unit` prints as an empty string, matching the original Rust `Display` behavior.
- Function return annotations are parsed but not dynamically enforced, matching the current implementation.
- Strings, booleans, integers, floats, functions, ranges, `warn.log` and `error.log` follow the documented v0.1 behavior.

## Direct ASM backend

The compiler returns every RBL expression as a small two-register value:

```text
rax = value tag
rdx = value payload
```

Tags:

```text
0 = Unit
1 = Int(i64)
2 = Float(f64 bits)
3 = String pointer
4 = Bool
```

Local variables are compile-time numbered slots:

```text
+0   bound flag
+8   tag
+16  payload
```

This removes the old interpreter hot path of repeated AST dispatch and `HashMap<String, Value>` lookups.

The runtime is also hand-written x86-64 assembly in `runtime/rbl_runtime.s` and calls libc only for basic process, I/O and memory facilities.

## Command-line toolchain

For day-to-day project work, use the `rbl` command. It is intentionally shaped like the workflows people know from Python tooling, C/C++ build tools and Cargo, but it still invokes RBL's direct-ASM backend:

```bash
./rbl new hello
cd hello
../RBLStudio/rbl check
../RBLStudio/rbl build
../RBLStudio/rbl run
../RBLStudio/rbl test
../RBLStudio/rbl bench --reps 5 --warmups 1
../RBLStudio/rbl fmt
../RBLStudio/rbl doc
../RBLStudio/rbl clean
```

The project manifest is `rbl.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"
entry = "src/main.rbl"
```

The lower-level IDE driver is still available as `tools/rbltool.py` for scripts and integration. Its terminal commands remain:

```bash
python3 tools/rbltool.py doctor
python3 tools/rbltool.py check examples/test.rbl
python3 tools/rbltool.py build examples/test.rbl
python3 tools/rbltool.py run examples/test.rbl
python3 tools/rbltool.py asm examples/test.rbl
```

The generated files are placed under `build/<program>/`:

```text
program.s
program.o
program
```

The low-level equivalent is:

```bash
bin/linux-x86_64/rblc-asm examples/test.rbl -S build/test/test.s
as --64 build/test/test.s -o build/test/test.o
ld build/test/test.o bin/linux-x86_64/rbl_runtime.o \
  -o build/test/test \
  -dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm
```

For the Windows target the same source is compiled with `--target win` and linked with the C runtime object:

```powershell
bin\win-x86_64\rblc-asm.exe examples\test.rbl -S build\test\test.s --target win
as --64 build\test\test.s -o build\test\test.o
gcc build\test\test.o bin\win-x86_64\rbl_runtime_win.o -o build\test\test.exe -static -lm
```

## Tests

Run the regression suite:

```bash
python3 tests/run_tests.py
```

The current suite contains **57 cases** covering arithmetic, precedence, floats, strings (escapes, multi-line text, interpolation), compound assignment and increments, `break`/`continue`, the unified `for (cond)` loop, the `a if (cond) else b` conditional expression, `switch`, lists (literals, indexing, element assignment, iteration, deep printing and equality), dictionaries (int/float/string keys, indexing, `in`, deep equality), tuples and structs (records with fields and receiver methods), `null` and `is`, the standard library, equality, comparisons, functions, recursion, argument timing, dynamic parameter checks, control flow, ranges, integer remainder, numeric literal forms, comments, Unicode identifiers, Unit values, logging and expected error cases.

Several cases are dedicated regressions for defects found during the code audit, including optimized-loop label reuse, recursive fast-int analysis, induction-variable overflow and runtime-internal range labels.

Every program is built and run with a timeout, so a program that regresses into an endless loop fails the suite instead of blocking it.

## Differential testing

The two backends share the frontend and the code generator but not the runtime, so they are checked against each other:

```bash
python3 tests/differential.py --a wsl --b native
```

This builds and runs `tests/cases`, `tests/stdlib_smoke.rbl`, `examples` and `benchmarks` through both toolchains and compares exit code, stdout and stderr. Every program receives a few real input lines, so `input()` exercises the line-reading path, not only the EOF path.

Programs whose output is intentionally platform dependent (the random API, `rbl.fs.cwd()`) are compared on the exit code only and reported as such.

## Benchmarks

Run the benchmark suite:

```bash
python3 benchmarks/run_benchmarks.py     # or: rbl bench
```

It reports `build` (compile + assemble + link), `run` (whole process execution) and `net = run - baseline`, where the baseline is an empty program measured the same way.

The `net` column exists because a small workload is dominated by process start-up: on Windows every run can also pay for the WSL bridge or the PE loader, which is why absolute `run` figures are only comparable on one machine and one backend. Absolute numbers from the release machine live in `benchmarks/RESULTS.md`.

The Linux-only 3-way comparison (RBL vs CPython vs hand-written ASM, `benchmarks/compare_all.py`) needs a Linux host because the baseline programs are Linux syscall programs; run it inside WSL.

## Code layout

```text
RBLStudio/
├── compiler/
│   └── rblc_asm.c       lexer + parser + AST + direct ASM backend
├── runtime/
│   ├── rbl_runtime.s    hand-written Linux assembly runtime
│   ├── rbl_containers.c
│   └── rbl_runtime_win.c
├── bin/
│   ├── linux-x86_64/
│   └── win-x86_64/
├── ide/
│   └── rbl_studio.py    IDE
├── tools/
│   └── rbltool.py       build/check/run/ASM driver
├── tests/               semantic regression suite
├── benchmarks/          speed benchmarks
├── examples/            example RBL programs
├── reference/Rust_v0.1/ original implementation snapshot
├── installer/           Windows/Linux file association integration
├── docs/                design, audit, IDE and backend docs
├── assets/              application icons/assets
└── artifacts/           checked-in build/reference artifacts
```

## Design documents

The requested design is written out explicitly in:

- `docs/QUESTIONS_ANSWERS_SOLUTION.md` — questions → answers → final architecture.
- `docs/CURRENT_IMPLEMENTATION.md` — current backend and semantic details.
- `docs/ASM_BACKEND.md` — ABI and assembly design.
- `docs/RUSSIAN_AUDIT.md` — detailed audit of the old Rust implementation.
- `docs/IDE_MANUAL.md` — editor/settings/workflow manual.
- `docs/TESTING.md` — regression and benchmark methodology.
- `docs/ROADMAP.md` — next compiler milestones.

## Rebuilding the compiler itself

Normal use does not require a C compiler because the repository ships a static `rblc-asm` binary.

For compiler development, `tools/rebuild_compiler.sh` rebuilds the bootstrap compiler and runtime. This uses a C compiler **only for the compiler implementation itself**; it never inserts C or GCC into the RBL user-program compilation pipeline on Linux.

## Current limitations

1. Two targets exist: Linux x86-64 ELF and Windows x86-64 PE/COFF. Other targets and other architectures are not implemented.
2. Dynamic values and strings still use a compact runtime representation rather than a fully optimized static type system.
3. There is no source-level debugger yet, and generated PE images currently carry no line tables.
4. The optimizer is conservative: dynamic values, strings and operations with uncertain types still use the tagged runtime ABI.
5. Register allocation is currently local to loops rather than a whole-function graph-coloring allocator.
6. Float printing uses C `%.17g` on both targets, which is not the shortest round-trip form used by the Rust reference (`0.1` prints as `0.10000000000000001`, NaN as `-nan`). Fixing that is an open item.
7. The next major architecture step is an explicit RBL IR plus CFG/liveness analysis.

## Optimizer and next compiler step

RBL 0.7 retains the loop-aware register allocation directly on the AST backend:

```text
AST → static analysis → loop optimization → local register allocation → x86-64 ASM
```

The next architecture step is a real intermediate representation:

```text
AST → RBL IR → CFG/basic blocks → liveness → register allocation → x86-64 ASM
```

That makes constant folding, dead-code elimination, register allocation and future additional backends possible without rewriting the language frontend.

## 3-way performance comparison

Use `./benchmarks/compare_pure_asm.sh` to compare the same workloads as RBL native ASM, CPython, and hand-written x86-64 ASM using high-resolution timing.

## Fixes and native Windows backend (current tree)

This tree contains the audit fixes, the native Windows target and the first step of the 0.8 language work:

- added the integer remainder operator `%` (integer-only, C/Rust sign rule) with implementation in both runtimes, three tests and differential coverage;
- added the 0.8 numeric literal forms (`0xFF`, `0b1010`, `0o755`, `1_000_000`, `1e42`, `1.5e-3`) and `#` line comments, with tests on both backends;
- added string escape sequences (`\n \t \r \\ \" \xNN \uXXXX \UXXXXXXXX`, NUL rejected) and multi-line `"""…"""` strings;
- added interpolated strings `f"…"` with `{expression}` parts and `{{`/`}}` (desugared to `str()` + string concatenation, so they reuse the existing runtime; format specs such as `{x:.2f}` are the next step);
- agreed syntax contract for the rest of 0.8 in [docs/SYNTAX_DECISIONS.md](docs/SYNTAX_DECISIONS.md) and the phase plan in [docs/ROADMAP.md](docs/ROADMAP.md);
- fixed label-id reuse after an optimized loop, which made valid programs fail with `symbol '.L_if_next_N' is already defined` from GNU `as`;
- fixed unbounded recursion in the fast-int analysis, which crashed the compiler with SIGSEGV on (mutually) recursive single-return `int` functions;
- fixed the optimized loop's missing induction-variable overflow check, which turned a required `integer overflow` error into an endless loop;
- fixed generated code referencing the runtime-internal label `.Lmsg_range_value`, which made programs with a range outside a `for` header fail to link;
- made label ids translation-unit-wide, so labels can never be reused inside a loop body or between a function and its fast variant;
- added `tests/differential.py`, which compares both runtime implementations;
- unified the runtime operand convention: `rbl_neg`/`rbl_not` now take `(tag in rdi, payload in rsi)` like every other entry point, so `-float` and `not bool` work on both backends;
- the Windows runtime truncates an input line at the first LF/CR exactly like the Linux runtime;
- the native toolchain driver prepends the toolchain `bin` directory to `PATH`, which `cc1.exe` needs to find its DLLs;
- console output is encoding-safe on Windows (UTF-8 console + `errors="replace"`), so non-ASCII paths and program output no longer crash `rbl check` / `rbl run`;
- added the native Windows PE backend (`--target win` in the compiler, `runtime/rbl_runtime_win.c`, MinGW-w64 discovery in `tools/rbltool.py`) and a start-up baseline in the benchmark suite;
- fixed the Windows input smoke test, which used to launch a Linux ELF natively;
- converted `examples/mega_test.rbl` to the canonical parenthesized syntax.

## Release v0.7.0 update

The release fixes the IDE launcher path, adds `tools/ide_doctor.py` and a startup log, adds the `rbl` project CLI, and introduces loop-local register allocation. The benchmark suite compares RBL native ASM, CPython, and hand-written x86-64 ASM with nanosecond-resolution timing via `perf_counter_ns()`. Use `./benchmarks/compare_pure_asm.sh`.

## GUI troubleshooting (WSLg)

The GUI launcher now initializes Tk from inside the event loop to avoid a WSLg/XWayland startup race. It also handles Ctrl+C cleanly.

Normal:

```bash
./RBLStudio.sh
```

Debug:

```bash
./RBLStudio.sh --debug
```

Minimal GUI probe:

```bash
python3 tools/gui_probe.py
```

Safe mode (does not construct the full IDE):

```bash
./RBLStudio.sh --safe
```

### Windows -> WSL path bridge

When WSL is selected, Windows F5 uses `Ubuntu-24.04` and normalizes host paths to `Q:/...` before calling `wslpath`; this avoids WSL command-line backslash stripping. The bundled `rblc-asm` is used as-is during normal F5.

To explicitly rebuild the compiler/runtime, run:

```text
rbl bootstrap
```

or set:

```text
RBL_REBUILD_COMPILER=1
```

`int(x)`, `float(x)`, and `str(x)` convert supported scalar values.

Blocks use parentheses: `func main() ( ... )`, `if condition ( ... )`, `for i in 0..10 ( ... )`. Curly braces remain accepted as legacy syntax but are no longer canonical.
