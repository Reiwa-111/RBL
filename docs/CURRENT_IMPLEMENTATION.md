# Current implementation

This project intentionally preserves the original Rust v0.1 language semantics while changing the execution model.

## Reference implementation

The original Rust files are copied under `reference/Rust_v0.1/src/` for auditability.

## Frontend

`compiler/rblc_asm.c` contains the lexer, recursive-descent parser and AST copied/adapted from the current implementation. It emits **assembly only**.

## Target backend

The direct backend performs compile-time variable slot numbering and emits x86-64 instructions and calls into the runtime.

There are two targets, selected with `--target linux|win`:

- **Linux x86-64 ELF** — calls `runtime/rbl_runtime.s`, linked with GNU `ld` against libc/libm.
- **Windows x86-64 PE** — calls `runtime/rbl_runtime_win.c` (C compiled with `__attribute__((sysv_abi))`), assembled with GNU `as` and linked by MinGW-w64 `gcc`.

The RBL value ABI, the local slots, the optimizer decisions and every generated function are identical on both targets; only the entry/exit glue and a few assembler directives differ.

## Static optimization layer

Before emitting generic tagged-runtime operations, the backend performs a conservative per-function static analysis. Proven `int` and `bool` expressions can use unboxed scalar values directly. The current fast path covers integer arithmetic, integer comparisons, boolean logic, simple integer-only function bodies, and integer loops whose bodies contain only statically safe scalar operations.

The fast path intentionally falls back to the dynamic ABI whenever a value's type or binding state is uncertain. That preserves the original semantics for dynamic values, strings, diagnostics and unusual control-flow cases.

## Runtime

The Linux runtime implements the dynamic `Value` model and diagnostics in assembly. Strings are heap-allocated during concatenation and currently live until process exit; this mirrors the first native milestone and keeps ownership machinery out of the backend.

The Windows runtime implements the same API in C with `__attribute__((sysv_abi))`, so the generated code calls it through the same registers. It puts `stdout`/`stderr` into binary mode and writes LF only, so program output is byte-identical to the Linux runtime; `tests/differential.py` enforces that.

## Known defects fixed in this tree

- label-id reuse after an optimized loop (duplicate `.L` symbols rejected by `as`);
- unbounded recursion in the fast-int analysis for (mutually) recursive single-return `int` functions (compiler SIGSEGV);
- the optimized loop had no induction-variable overflow check (endless loop instead of `integer overflow`).

## Supported language surface

The current syntax and behavior are the same as documented in `docs/RBL_QUICK_REFERENCE.md`.

## Important semantic details

- `set` duplicate check happens before RHS evaluation.
- `let` RHS evaluates before checking existence/type.
- `and` / `or` evaluate both operands.
- user-call arity is checked before argument evaluation.
- parameter types are checked after argument evaluation.
- function return type declarations are parsed but not enforced, matching the original interpreter.
- blocks share the function's environment; they do not create nested variable maps.
- loop variables persist after a loop that executed.
- `Unit` displays as an empty string, matching the Rust `Display` implementation.
- duplicate function declarations resolve to the last definition.
