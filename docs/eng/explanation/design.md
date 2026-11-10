# last verified: v0.8.0 | 2026.11.09 | VERSION SCOPE RECHECK

## Explanation: design principles and future directions

This document records design decisions and separates them from the current language contract. Any capability marked as a proposal or roadmap item below is not considered available in RBL 0.8.0.

## Principles

- Keep the language core small and coherent; do not add syntax without defined semantics.
- Specify evaluation order, side effects, errors, platform differences and memory ownership alongside new capabilities.
- Do not promise APIs until implementations, tests and stable interfaces exist.
- Treat the regression suite as the executable contract for implemented behavior.
- Keep source-module loading separate from native extension loading for the IDE/compiler.

## Possible language extensions

Ideas under discussion include source modules/imports, multi-file compilation, globals/constants, exceptions, `defer`, nullable/container type expressions, slices, negative indexing, named struct construction, lambdas/closures, default/variadic/named arguments, overloads and async/await. Exact syntax and semantics may change; these ideas are not a specification of available features.

## Module and plugin boundaries

A future source import should include source code as part of the current program build. It should not implicitly load arbitrary code into the compiler or IDE process or mutate toolchain behavior.

Native extensions/plugins are a separate system. Before implementation, their ABI, permissions, lifecycle, compatibility/versioning, failure isolation and allowed extension points must be defined. A plugin must not be promised the ability to replace the parser or code generator without a formal stable interface.

## What a new feature specification needs

Before implementation, define:

1. Grammar and ambiguity-resolution rules.
2. Static and runtime validation.
3. Evaluation order and observable side effects.
4. Errors, diagnostics and exit codes.
5. Memory ownership and collector interaction.
6. Linux/Windows behavior and ABI constraints.
7. Regression tests and cross-backend differential tests.

## Engineering roadmap

The authoritative release sequence, task status and completion criteria are maintained in the [RBL roadmap](roadmap.md). This page records design principles and proposals, not a competing task list.
