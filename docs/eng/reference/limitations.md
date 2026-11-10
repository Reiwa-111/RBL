# last verified: v0.8.0 | 2026.11.09 | LANGUAGE CONTRACT RECHECK

## Reference: RBL 0.8.0 limitations

This list deliberately distinguishes implemented language behavior from proposed features. An omitted construct is not necessarily a design rejection; it means the current compiler/runtime does not promise support for it.

## Language and compiler

- x86-64 Linux ELF64 and x86-64 Windows PE/COFF only.
- One source file per program; no source `import`, modules or multi-file compilation.
- No global variables or constants.
- No exceptions, `try`/`catch`/`finally`, `raise` or `defer`.
- No lambda expressions or closures.
- No default, variadic or named arguments; no overload resolution.
- No async/await or task scheduler.
- No nullable type annotations, generic/container type expressions or comprehensive static type system.
- No named struct construction or separate struct runtime identity; structs use dictionary storage.
- No tuple destructuring, list slices or negative indexing.
- No general dictionary iteration in loops.
- No list concatenation with `+`.
- Logical `and` and `or` do not short-circuit.
- No full source-level debugger or complete Windows source-line mapping table.

## Standard library and runtime

- `rbl.string.upper/lower` handle ASCII, not Unicode case mapping.
- The pseudorandom generator is not cryptographically secure; Linux and Windows sequences differ.
- `rbl.time.now_ms()` uses different clock sources on Linux and Windows.
- `rbl.fs.cwd()` returns a platform-dependent path.
- The conservative collector may retain unreachable objects when stale pointer-shaped values remain in scanned stack memory; collection timing is not deterministic.
- The native Runtime ABI has no string pinning/root-registration API. Only pointer-shaped words in the scanned current-thread stack range and values reachable through containers are roots; native heap/global storage and other threads are not scanned.
- Runtime state is process-global and unsynchronized. Only the thread that entered generated RBL `main` may call Runtime helpers; concurrent native calls from other threads are unsupported.
- No general-purpose user FFI/plugin ABI exists.

## Projects and tooling

- No dependency/package manager or package registry.
- `rbl test` runs tests for the RBL checkout, not a universal test runner for each project.
- `rbl doc` generates a simple function listing using a text pattern; it is not a full API reference generator.
- The formatter normalizes block indentation; it does not perform full semantic formatting.

## Keep this separate from the roadmap

Proposed architecture and features are described in [Explanation: design](../explanation/design.md). They are not supported language features until implemented and covered by regression tests.
