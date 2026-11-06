# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Current implementation

This page describes the implementation present in the repository version `0.7.1`. It is not a statement that every planned language feature is available.

## Compiler

The compiler is implemented in `compiler/rblc_asm.c`. The file currently contains the UTF-8 lexer, recursive-descent parser, AST definitions, static analysis, optimization decisions and assembly code generator. It is a C bootstrap compiler; it does not emit C source for user programs.

The user-program pipeline is:

```text
source.rbl -> lexer -> parser -> AST -> static analysis
            -> loop/scalar optimization -> x86-64 assembly
            -> assembler -> linker + runtime -> executable
```

There is no separate RBL IR, control-flow graph or whole-function liveness pipeline yet.

## Targets and runtimes

| Target | Generated object format | Platform runtime | Container runtime |
|---|---|---|---|
| Linux x86-64 | ELF64 | `runtime/rbl_runtime.s` | `runtime/rbl_containers.c` |
| Windows x86-64 | PE/COFF | `runtime/rbl_runtime_win.c` | `runtime/rbl_containers.c` |

Both targets use the same frontend and assembly code generator. The runtime entry points follow the compiler's shared calling convention. The Windows C runtime uses `__attribute__((sysv_abi))` for calls from generated code.

## Runtime values and containers

General values use a tag/payload pair. The current container runtime defines tags for `Unit`, `Int`, `Float`, `String`, `Bool`, `List`, `Dictionary` and `Tuple`; `null` is a distinct language value with tag 8 in the compiler/platform runtime. See `ASM_BACKEND.md` for the ABI summary.

Lists and tuples share an underlying growable sequence representation. Dictionaries use an open-addressed hash table. `struct` declarations are currently compiler-side metadata: constructed instances are dictionaries keyed by field name. There is no dedicated runtime struct tag or class/object model. Receiver methods are lowered to ordinary functions with the receiver as the first argument; the receiver's declared type is not enforced at runtime.

## Memory management

`runtime/rbl_containers.c` contains a conservative mark-and-sweep collector for list, tuple and dictionary objects. It tracks allocated container objects, scans the active machine stack for candidate object pointers, traces container references and reclaims unreachable container storage.

This is not a complete managed-memory solution. String allocations, including strings created while rendering containers and concatenating strings, are not reclaimed by this container collector and can remain allocated until process exit. Do not describe RBL 0.7.1 as reclaiming every heap allocation.

## Implemented language areas

The current source and regression suite cover:

- scalar literals, arithmetic, comparisons and boolean operations;
- variable declaration/rebinding with `set` and `let`;
- functions, typed parameters, returns and recursion;
- `if`/`elif`/`else`, range loops, condition loops, `break` and `continue`;
- conditional expressions and `switch`;
- numeric literal extensions, comments, escapes, multiline strings and basic f-string interpolation;
- lists, dictionaries, tuples, `null`, membership and identity comparison;
- record-like structs and receiver-method syntax;
- built-in standard-library functions and namespaces.

The complete implemented syntax is documented in `RBL_QUICK_REFERENCE.md`; standard-library behavior is in `STDLIB.md`.

## Important language semantics

- `and` and `or` evaluate both operands; they are not short-circuit operators.
- Function arity is checked before argument expressions are evaluated. Parameter type checks happen after argument evaluation.
- Function return-type annotations are parsed but are not enforced by the runtime.
- `set` checks for an existing binding before evaluating its right-hand side. `let` evaluates its right-hand side before checking the existing binding and its runtime type.
- Blocks share the function's environment; they do not create nested lexical scopes.
- A range-loop variable remains bound after a loop that ran at least once. A variable for a loop with zero iterations is not introduced.
- A missing dictionary key evaluates to `Unit` (`()`); use `in` to test whether a key exists.

## Not implemented

The current implementation does not provide source imports or multi-file compilation, a package manager, exceptions, `defer`, global variables/constants, closures/lambdas, default or variadic parameters, named arguments, overload resolution, async/await, a bytecode VM, a source debugger, a dedicated IR/CFG, or non-x86-64 targets. F-string format specifications such as `f"{value:.2f}"` are not implemented.
