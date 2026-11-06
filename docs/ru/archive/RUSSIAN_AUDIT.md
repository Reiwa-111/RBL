# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Historical audit: Rust v0.1 and early native prototype

**Historical document.** This file describes the original Rust AST interpreter and an intermediate native prototype. It is retained for architectural history and is not a specification of the current compiler. For current behavior, use [`CURRENT_IMPLEMENTATION.md`](CURRENT_IMPLEMENTATION.md) and [`RBL_QUICK_REFERENCE.md`](RBL_QUICK_REFERENCE.md).

## Original Rust implementation

The original implementation consisted of a UTF-8 lexer, recursive-descent parser, AST definitions, interpreter and command-line entry point. Programs were parsed and then executed by recursively evaluating AST nodes. Variables were stored in a hash-map environment. Runtime values included integers, floats, strings, booleans and `Unit`.

The original language used `set` for declaration and `let` for rebinding. The evaluation order of those operations, function arity checks, parameter checks, non-short-circuit boolean operators and shared block environment became compatibility constraints for the native implementation.

## Intermediate native prototype

An intermediate prototype generated C temporaries and invoked GCC/Clang. That design was replaced. The current compiler emits x86-64 assembly directly; it does not generate C source for RBL user programs.

## Current relevance

The historical implementation is useful as a semantic reference, but current behavior must be checked against the C compiler, platform runtimes and regression tests. The historical Rust interpreter is not currently run automatically as a differential oracle. The current differential runner compares the Linux and Windows backend paths.
