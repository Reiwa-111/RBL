# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Language design decisions and proposals

**Status:** design notes, not a specification of RBL 0.7.1. Implemented syntax is documented in `RBL_QUICK_REFERENCE.md`. A proposal listed here must not be assumed to compile.

## Design principles

- Keep source imports separate from loading native compiler/IDE extensions.
- Make runtime and compiler boundaries explicit before promising third-party extension APIs.
- Prefer a small, coherent core language over adding syntax without defined semantics.
- Specify evaluation order, ownership, errors and platform behavior alongside syntax.
- Treat the regression suite as the executable contract for implemented behavior.

## Candidate language features not implemented in 0.7.1

The following are proposals retained from design discussions. Their exact syntax and semantics may change:

- Source modules: `import module`, `from module import name` and path-based imports, with multi-file compilation.
- Global variables and constants.
- Exceptions (`try`/`catch`/`finally`, `raise`) and deferred cleanup (`defer`).
- Nullable type annotations such as `int?` and `Point?`, plus stronger container types and type expressions such as `[]int` and `{string: int}`.
- List slicing, negative indexing and richer container methods.
- Named struct construction and dedicated runtime struct identity.
- Lambda expressions and closures with a documented capture model.
- Default, variadic and named function arguments; overload resolution.
- Async functions, `await` and a task scheduler.
- F-string format specifications.

## Module and extension boundary

A future source import should resolve a source module as part of the current program build. It should not load arbitrary code into the running compiler or IDE and should not mutate compiler behavior. Native extensions/plugins are a separate mechanism and require their own ABI, permissions, lifecycle, compatibility and failure-isolation design.

A future plugin system may expose controlled extension points for Studio UI/themes, compiler passes or runtime providers. It is premature to promise that plugins can replace arbitrary parser, code generator or runtime internals until the corresponding interfaces and stability guarantees exist.

## Decisions still requiring a formal specification

Before implementation, each feature needs a short specification covering:

1. Grammar and ambiguity rules.
2. Static/runtime validation rules.
3. Evaluation order and observable side effects.
4. Error behavior and diagnostics.
5. Memory ownership and garbage-collector interaction.
6. Linux/Windows parity.
7. Regression and differential tests.

## Relationship to the roadmap

The compiler IR, module system, plugin boundaries, improved memory management and async runtime are tracked in `ROADMAP.md`. This document records design direction; it does not assign release dates.
