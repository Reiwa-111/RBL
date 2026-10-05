# RBL optimizer v0.7

The previous direct backend already had typed integer/bool fast paths. v0.7 retains the conservative loop-local register allocator.

## Register policy

For ordinary RBL functions, the backend preserves the System V callee-saved registers it uses. Fast loops may allocate up to two integer locals:

```text
r14 — loop induction variable when safe
r15 — one frequently used integer local/accumulator
```

The allocator is intentionally conservative. A loop variable is kept in a register only when the loop body does not reassign that variable and the loop contains no nested `for`. Bound integer locals are loaded once before the loop and spilled once after it.

## What this removes

The old fast loop repeatedly performed:

```text
load stack -> arithmetic -> store stack
```

The optimized loop keeps the hot integer state in registers and only materializes language-visible variables when entering/leaving the optimized region.

Function calls remain ABI-safe because the selected loop registers are callee-saved.

## Safety fallback

Dynamic values, strings, floats, uncertain typing, nested loops and loops containing unsupported control-flow shapes continue through the existing conservative backend path. Semantic behavior is therefore not traded for optimization.

## Current limits

This is not a global graph-coloring allocator yet. It is a local, loop-aware allocator deliberately added before a full RBL IR. The next architecture step is:

```text
AST -> RBL IR -> CFG/basic blocks -> liveness -> register allocation -> ASM
```

That will enable more registers, better expression scheduling, common-subexpression elimination, constant folding and dead-store elimination without making the frontend understand machine registers directly.

## v0.7 additions

The optimizer remains compatible with the new parenthesized block syntax and the standard-library built-ins. Built-ins use direct runtime entry points; they do not fall back to an interpreter.
