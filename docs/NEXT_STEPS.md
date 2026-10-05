# Next low-level steps

> Historical note: this file was written for the intermediate v0.2 prototype that
> generated C and fed it to GCC. Items 3 and 8 below describe that prototype. The
> current backend emits x86-64 assembly directly for both targets (Linux ELF and
> native Windows PE); the corresponding open work is the explicit RBL IR and CFG
> described in `docs/ROADMAP.md` (v0.8).

The current native backend is deliberately a first native milestone rather than the final RBL compiler architecture.

1. Replace the linear function-name dispatch chain with a generated string hash table.
2. Add source spans to every AST node and propagate them to generated runtime errors.
3. Lower to a compact RBL bytecode/SSA-like IR before C codegen. That makes a future direct x86-64 backend much easier.
4. Add an arena allocator for strings and AST/codegen storage.
5. Add constant folding and dead-branch elimination while preserving runtime error timing.
6. Add direct-call lowering for statically known valid user functions while keeping dynamic resolver behavior for unresolved/unreachable calls.
7. Add fuzzing for lexer/parser and semantic differential testing against a frozen reference interpreter.
8. Add a real x86-64 backend after the IR stabilizes; at that point the assembly code becomes the final code-generation layer instead of a hand-written replacement for the whole parser.
