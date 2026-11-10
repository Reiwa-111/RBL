# last verified: v0.8.0 | 2026.11.09 | ARCHITECTURE GUIDE RECHECK

## Explanation: compiler and backend architecture

RBL 0.8.0 uses a modular C bootstrap compiler that emits x86-64 assembly directly from its syntax tree. User programs are not translated to C.

## From source to executable

```text
source.rbl
   │
   ▼
UTF-8 lexer → parser → AST → static analysis
                              │
                              ▼
                       scalar/loop fast paths
                              │
                              ▼
                    x86-64 assembly output
                              │
                              ▼
                       GNU assembler
                              │
                              ▼
                 linker + platform runtime
                              │
                              ▼
                       native executable
```

The compiler is split into independent C translation units with one private shared header:

| Module | Responsibility |
|---|---|
| `common.c` | Diagnostics, allocation helpers and shared string hashing |
| `lexer.c` | UTF-8 tokenization and token cleanup |
| `ast.c` | AST node/vector construction helpers |
| `parser.c` | Recursive-descent parser and compile-time struct declarations |
| `sema.c` | Variable/function indexes, type inference and fast-path eligibility analysis |
| `codegen.c` | Scalar/loop specialization and direct x86-64 assembly emission |
| `driver.c` | CLI, source loading and pipeline orchestration |
| `rblc_internal.h` | Private data layouts and cross-module declarations; not a public RBL API |

The compilation pipeline runs from common utilities through lexing/AST construction, parsing, semantic analysis, code generation and finally driver orchestration. In terms of source dependencies, the driver consumes the frontend and code generator; the parser consumes AST/common utilities; semantic analysis consumes AST/common utilities; code generation consumes semantic analysis/common utilities; lexer and AST helpers consume common utilities. This graph is acyclic. Fast-integer eligibility is owned by semantic analysis; it does not call back into the assembly emitter. The shared header exposes the cross-translation-unit declarations; implementation helpers are `static` and remain local to their owning module. Compiler build paths enable missing-prototype, strict-prototype, redundant-declaration and implicit-declaration diagnostics to keep that boundary enforced. The same frontend and code generator serve Linux ELF64 and Windows PE/COFF. There is no separate RBL IR, CFG/basic-block pipeline or whole-function liveness allocator in 0.8.0.

The Makefile compiles each module into its own object and links the objects; this enables parallel clean builds and recompilation of only the changed module. Make and the native/WSL bootstrap paths in `tools/rbltool.py` track compiler sources, the private header and the canonical `VERSION` value for rebuilds. The standalone Linux `tools/rebuild_compiler.sh` rebuilds the same seven compiler sources plus all four Linux Runtime objects (assembly, containers, ABI and formatting), and reads `VERSION` at invocation.

## Source locations and diagnostics

`SourcePos` carries the original file, zero-based UTF-8 byte offset and one-based line/column. The driver validates UTF-8 and rejects embedded NUL bytes before lexing. Tokens retain their source positions; AST expressions, statements, parameters and functions copy the originating position. The diagnostic context retains the original source buffer until the compiler exits, allowing `fatal_at()` to print a stage-labelled error, source line and caret. Columns count Unicode code points; tabs count as one source column while the excerpt expands them to four-cell tab stops. Caret padding approximates terminal cells for common wide CJK/emoji characters and zero-width combining marks; source coordinates are unaffected. LF, CRLF and bare CR are treated as line endings.

The driver labels lexer, parser, semantic-validation and code-generation phases. F-string interpolation is re-lexed with an origin offset into the original source; nested braces and delimiters are scanned before identifying a top-level format colon. After that separator, the static format-spec tail is scanned as literal spec syntax and validated independently. Formatted expressions lower to an internal helper call; the shared `runtime/rbl_format.c` implementation uses fixed conversion formats and Runtime-managed string allocation on both targets. Semantic validation currently checks loop-control placement, duplicate parameter names and the required `main` function. Code generation reports source-level builtin arity constraints at the call expression. This is not a general static type checker: runtime argument/type checks and runtime failures remain runtime behavior. See the [diagnostics reference](../reference/diagnostics.md) for the exact contract and focused negative tests.

### RBL-level example

The compiler split is not a source-language module system. RBL 0.8.0 programs still compile from one `.rbl` source file and cannot import the compiler's C modules. [`examples/compiler/lookup_demo.rbl`](../../../examples/compiler/lookup_demo.rbl) is a small runnable program that repeatedly uses local variables, functions and string values; it illustrates an ordinary workload handled by the private indexes without exposing those indexes to RBL code. See its [notes and expected output](../../../examples/compiler/README.md).

## Supported targets

| Target | Format | Platform runtime | Shared containers |
|---|---|---|---|
| Linux x86-64 | ELF64 | `runtime/rbl_runtime.s` | `runtime/rbl_containers.c` |
| Windows x86-64 | PE/COFF | `runtime/rbl_runtime_win.c` | `runtime/rbl_containers.c` |

The frontend and code generator are shared. Platform entry/exit glue, system calls, object-format directives, runtime and toolchain differ. Linux uses GNU `as`, GNU `ld`, system libraries and runtime objects. Windows uses MinGW-w64: GNU assembler and GCC as the linker driver.

## What is compiled with C

C is used for the bootstrap compiler, Windows scalar runtime and shared container runtime. The compiler may invoke GCC to build these components. This does not mean that `.rbl` source programs are translated to C: user code is generated as assembly.

## Runtime value representation

The general runtime value is a tag/payload pair:

| Tag | Value |
|---:|---|
| 0 | `Unit` |
| 1 | `Int` (i64) |
| 2 | `Float` (IEEE-754 binary64 bits) |
| 3 | `String` (pointer) |
| 4 | `Bool` |
| 5 | `List` |
| 6 | `Dictionary` |
| 7 | `Tuple` |
| 8 | `null` |

In the general runtime representation, `rax` carries the tag and `rdx` carries the payload. Some proven-safe integer/Boolean code paths can operate on unboxed machine values.

Runtime helper calls use the System V AMD64 argument registers: `rdi`, `rsi`, `rdx`, `rcx`, `r8` and `r9`. Windows runtime functions are exported with `sysv_abi` to preserve this contract for generated code despite the Windows PE entry point. User-defined RBL function calls use a compiler-generated argument-array convention; do not infer their stack frame from the ABI of an ordinary C function.

The compiler embeds its required Runtime ABI descriptor in each executable and calls the versioned `rbl_runtime_abi_check_v1` entry point before the RBL `main` function. The linker rejects a Runtime without the required major-version symbol; the startup handshake checks the descriptor's minor version, value/pointer layout, calling convention and feature bits. See the normative [Runtime ABI reference](../reference/runtime-abi.md).

## Runtime components and memory

- `runtime/rbl_runtime.s` — Linux scalar operations, strings, I/O, math and process support.
- `runtime/rbl_runtime_win.c` — Windows counterpart of the platform runtime.
- `runtime/rbl_containers.c` — shared list, dictionary, tuple and collector implementation.
- `runtime/rbl_abi.c` / `runtime/rbl_abi.h` — shared ABI descriptor, version compatibility check and contract constants.

The shared runtime implements a conservative, non-moving mark-and-sweep collector for containers and dynamic strings. It scans stack words up to the `rbp` captured from generated `main`, then traverses string/list/tuple/dictionary values stored in reachable containers. Static literals are outside the heap. Generated code calls `rbl_gc_safepoint` before statements that may allocate or call code with unknown allocation behavior, and before repeated loop-condition evaluation. Proven non-allocating integer/Boolean scalar statements omit redundant safe points; allocating operations and unknown calls retain them. Runtime allocators never collect in the middle of an expression. This keeps live locals and loop temporaries visible without compiler-wide liveness analysis. Because the collector is conservative, pointer-shaped stack data can retain unreachable objects longer than necessary.

## Why there is no module import system

In this version, a program is compiled from a single source file. Built-in namespaces such as `rbl.math` and `rbl.io` are names recognized by the compiler/runtime, not module files and not an `import` mechanism. A package system and module resolution are not implemented.
