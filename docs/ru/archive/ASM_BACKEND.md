# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Direct x86-64 assembly backend

## Overview

The compiler emits x86-64 assembly directly from its AST and static-analysis results. It does not generate C source for user programs.

```text
RBL -> lexer/parser/AST -> static analysis -> direct x86-64 ASM
    -> GNU as -> object file -> platform runtime + system libraries
```

## Targets

| Target | Object format | Assembler | Link step |
|---|---|---|---|
| Linux x86-64 | ELF64 | GNU `as --64` | GNU `ld` + libc/libm + assembly runtime object + container object |
| Windows x86-64 | PE/COFF | MinGW-w64 GNU `as --64` | MinGW-w64 `gcc` + Windows runtime object + container object |

The same compiler frontend and code generator serve both targets. Platform-specific entry/exit glue, runtime implementation and object-format directives differ.

## Runtime ABI

General RBL values are returned as:

```text
rax = value tag
rdx = value payload
```

Runtime calls use the System V AMD64 register convention for RBL arguments:

```text
rdi, rsi, rdx, rcx, r8, r9
```

Runtime helper arguments are passed in the System V AMD64 integer argument registers, following each helper's declared signature. User-defined RBL function calls use the compiler's generated argument-array convention; do not infer their call frame from a C function signature. The exact entry-point contracts are visible in `compiler/rblc_asm.c` and the runtime sources.

The Windows runtime is compiled with `__attribute__((sysv_abi))`, allowing generated instructions to call it using the same RBL runtime convention even though the executable uses the Windows platform entry point.

## Runtime components

- `runtime/rbl_runtime.s`: Linux scalar/runtime operations, diagnostics, strings and process support.
- `runtime/rbl_runtime_win.c`: Windows implementation of the platform runtime API.
- `runtime/rbl_containers.c`: shared C implementation for list, dictionary, tuple and container GC operations. It is compiled separately for each target and linked into both executables.

The C compiler is used to build the bootstrap compiler, the Windows runtime object and the shared container object. On Windows, MinGW-w64 GCC is also used as the linker driver. None of these steps means that RBL user code is translated to C.

## Value tags

The current language/runtime representation includes these values:

| Tag | Value |
|---:|---|
| 0 | `Unit` |
| 1 | `Int` (`i64`) |
| 2 | `Float` (IEEE-754 bits) |
| 3 | `String` (pointer payload) |
| 4 | `Bool` |
| 5 | `List` |
| 6 | `Dictionary` |
| 7 | `Tuple` |
| 8 | `null` |

A `struct` instance does not have a separate runtime tag in this version; it is represented using dictionary storage.

## Local slots and fast paths

Compiled functions use numbered 24-byte local slots:

```text
+0   bound flag
+8   value tag
+16  value payload
```

The general runtime value itself is a 16-byte `(tag, payload)` pair. Proven integer/boolean expressions and selected integer loops can bypass the general tagged representation. The optimization is conservative and falls back to tagged runtime operations whenever type or control-flow assumptions cannot be proven.

## Build examples

The project driver is the recommended interface:

```bash
python3 rbl.py build examples/test.rbl
python3 rbl.py run examples/test.rbl
python3 rbl.py asm examples/test.rbl
```

Low-level Linux steps are implemented by `tools/rbltool.py` and the bundled files under `bin/linux-x86_64/`. Do not link against `runtime/rbl_runtime.o` as an assumed current artifact: the supported runtime objects are the ones maintained under `bin/` and built by the toolchain driver.

## Check behavior

`rbl check FILE` invokes the compiler's `--check` mode. This runs source processing and assembly code generation into a temporary stream; it does not merely stop after parsing. It does not assemble or link an executable and does not leave an assembly output file.
