# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Explanation: compiler optimizations

RBL 0.7.2 uses conservative optimizations integrated into AST analysis and code generation. It does not have a separate high-level optimizer pipeline over an IR.

## Scalar fast paths

When analysis proves that an expression or local variable has an integer/Boolean type, the compiler can use unboxed machine values instead of the general tagged representation (`rax` tag plus `rdx` payload). This reduces tag handling and runtime helper calls.

Fast paths cover supported integer arithmetic and comparisons, Boolean operations, selected integer-only functions and eligible integer loops. When a type, binding or control-flow path is uncertain, code generation falls back to the general tagged runtime path.

## Loop-local register allocation

For a restricted class of loops, the compiler can keep hot integer values in callee-saved registers. The implementation is local and specialized; for example, `r14` can hold an eligible induction variable and `r15` a selected hot integer local/accumulator. Register preservation must follow the ABI.

The fast-loop path has strict eligibility rules. Reassigned induction variables, nested loops, dynamic values and complex/unsupported control flow fall back to ordinary code generation.

## What the optimizer does not do

The current implementation does not provide a general:

- RBL IR with explicit basic blocks;
- CFG and data-flow/liveness analysis;
- global graph-coloring register allocation;
- constant-folding pipeline;
- IR-level dead-store/dead-code elimination;
- common-subexpression-elimination pipeline.

Do not confuse specialized fast paths with a complete global optimizer. An architecture of AST → IR → CFG → liveness → register allocation is a planned direction, not a feature of release 0.7.2.
