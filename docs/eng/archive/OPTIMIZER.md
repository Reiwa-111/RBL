# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL optimizer

The optimizer in `compiler/rblc_asm.c` is conservative and operates on the AST and its static-analysis results. There is no explicit RBL IR, CFG, liveness analysis or whole-function register allocator in the current implementation.

## Scalar fast paths

When the compiler can prove the relevant values are integers or booleans, selected expressions and function bodies can use unboxed machine values rather than the general tagged `rax`/`rdx` representation. Supported fast paths include integer arithmetic/comparisons, boolean expressions, eligible integer-only functions and selected integer loops.

If a value, binding or control-flow path is uncertain, code generation falls back to the tagged runtime path. This conservative fallback is part of the correctness strategy.

## Loop-local register allocation

The current allocator is loop-aware and local, not global. It can keep selected hot integer state in callee-saved registers for eligible loops. The implementation uses a limited set of registers (including `r14` for an eligible induction variable and `r15` for a selected hot integer local/accumulator) and preserves the required callee-saved state.

Eligibility is restricted. Reassigned induction variables, nested loops, dynamic values, uncertain types and unsupported control-flow shapes use the general path.

## Not implemented

The compiler does not yet provide global graph-coloring allocation, explicit basic blocks, liveness analysis, general constant folding, dead-store elimination or common-subexpression elimination as an IR optimization pipeline.

## Planned architecture

The intended next compiler stage is:

```text
AST -> RBL IR -> CFG/basic blocks -> liveness -> register allocation -> ASM
```

This is a roadmap item, not current architecture. See `ROADMAP.md`.
