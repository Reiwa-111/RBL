# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Explanation: compiler and backend architecture

RBL 0.7.2 uses a C bootstrap compiler that emits x86-64 assembly directly from its syntax tree. User programs are not translated to C.

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

The frontend, AST, analysis and code generator currently live in `compiler/rblc_asm.c`. There is no separate RBL IR, CFG/basic-block pipeline or whole-function liveness allocator in this version.

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

## Runtime components and memory

- `runtime/rbl_runtime.s` — Linux scalar operations, strings, I/O, math and process support.
- `runtime/rbl_runtime_win.c` — Windows counterpart of the platform runtime.
- `runtime/rbl_containers.c` — shared list, dictionary, tuple and collector implementation.

The container runtime implements a conservative mark-and-sweep collector for list/tuple/dictionary objects: it tracks container allocations, searches the stack for candidate roots and traverses container references. This is not a complete tracing GC for every object. String allocations, including concatenation results and container-rendering strings, are not reclaimed by this collector and may remain allocated until process exit.

## Why there is no module import system

In this version, a program is compiled from a single source file. Built-in namespaces such as `rbl.math` and `rbl.io` are names recognized by the compiler/runtime, not module files and not an `import` mechanism. A package system and module resolution are not implemented.
