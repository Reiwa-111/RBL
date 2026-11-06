# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

> Historical snapshot of the previous plan. The current release plan is the [RBL roadmap](../explanation/roadmap.md).

## RBL roadmap

This roadmap separates the current implementation from work that has not shipped. It is a project plan, not a promise of release dates. Current implementation details take precedence over older phase notes.

## Current baseline — 0.7.1

Implemented and present in the repository:

- Direct x86-64 assembly generation for Linux ELF64 and Windows PE/COFF.
- Shared frontend and code generator across the two targets.
- Platform runtime implementations plus the shared C container runtime.
- Scalar fast paths and conservative loop-local register allocation.
- Functions, recursion, conditions, range/condition loops, `break`, `continue`, `switch` and conditional expressions.
- Numeric literal extensions, comments, string escapes, multiline strings and basic f-string interpolation.
- Lists, dictionaries, tuples, `null`, `in`, `is` and record-like structs with receiver-method syntax.
- Built-in standard-library functions/namespaces, project CLI and RBL Studio.
- 59 direct regression cases, a separate standard-library/input smoke test and a cross-backend differential runner.
- A five-implementation Linux benchmark: RBL, C, C++, CPython and hand-written x86-64 assembly.

## Known limitations

- Only x86-64 Linux and Windows targets are implemented.
- The compiler frontend and code generator remain in one large C source file.
- There is no explicit RBL IR, CFG or global register allocator.
- `struct` values are dictionaries; receiver types are not runtime-checked.
- The container collector does not reclaim all strings.
- There is no source import system, multi-file compilation or package/dependency manager.
- Exceptions, `defer`, closures, lambda expressions, optional/default/variadic/named parameters, overloads and async/await are not implemented.
- There is no source-level debugger or generated Windows source line table.

## Next engineering priorities

### 1. Make the compiler maintainable

Split the current compiler implementation into well-defined lexer, parser/AST, analysis, code generation and driver modules without changing language behavior. Establish clear interfaces and keep the existing ABI stable during the refactor.

### 2. Introduce a compiler IR

Define an explicit RBL IR and validation rules, then add basic blocks and control-flow representation. Keep the AST frontend independent from machine-register decisions.

### 3. Build optimization infrastructure on the IR

Add liveness analysis and whole-function register allocation only after IR semantics and tests are stable. Constant folding, dead-code/dead-store elimination and common-subexpression elimination belong on this foundation.

### 4. Design source modules and extension boundaries

Keep source-level `import` separate from compiler/IDE plugin loading. A source import must load code as part of a program build; it must not implicitly mutate the compiler or Studio process. Define module identity, dependency tracking and ABI/version boundaries before implementing a package manager or native extension host.

### 5. Improve runtime memory semantics

Document and test string ownership/reclamation separately from container collection. Do not claim full garbage collection until strings and all heap-managed runtime objects have a defined and verified lifetime model.

## Later / exploratory work

Potential areas include package resolution, a compiler/Studio extension host, source debugging, richer standard-library modules, additional target architectures, a bytecode VM for development and a self-hosted frontend. These are exploratory until they have an accepted design, implementation and tests.

## How to update this roadmap

Move a feature to the implemented section only when the code exists on the release branch and a relevant regression or integration test covers it. Keep design proposals in `SYNTAX_DECISIONS.md`; keep historical implementation notes in the historical documents rather than adding them to the current roadmap.
