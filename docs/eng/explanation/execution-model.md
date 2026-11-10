# last verified: v0.8.0 | 2026.11.09 | LANGUAGE CONTRACT RECHECK

## Explanation: execution model and semantics

This page explains some less obvious properties of the RBL 0.8.0 language and runtime. Bounded f-string format specifications are documented in the [syntax reference](../reference/syntax.md).

## Functions, arguments and types

RBL has dynamic runtime values and limited checking of declared parameter types. Parameter types are checked when a function is called. A return-type annotation is parsed, but the current runtime does not verify the value actually returned.

Function arity is checked before argument expressions are evaluated. The arguments are then evaluated, after which parameter types are checked against the resulting values. Therefore, an arity error and a parameter-type error can have different observable side effects.

## Variables and evaluation order

`set` and `let` intentionally have different evaluation order:

- `set` first checks that the name has not already been declared, then evaluates the RHS.
- `let` evaluates the RHS first, then checks that the name exists and that its runtime type is compatible with the assignment.

As a result, the RHS of an invalid `let` can run, while a duplicate `set` fails before evaluating its RHS.

## Dynamic string lifetime

String values use shared, immutable-by-language, NUL-terminated byte buffers. String literals and Runtime constants have static storage; strings produced by concatenation, input/file operations, conversions, string methods and container formatting are Runtime-owned heap allocations. Assigning or passing a string copies its tagged pointer pair, not its bytes, so aliases refer to the same allocation.

Dynamic strings and containers are reclaimed by a conservative, non-moving mark-and-sweep collector. A value is retained when its exact base pointer appears as a pointer-shaped machine word anywhere in the scanned stack range, or when it is reachable through a live list, tuple or dictionary. The stack scan does not validate tags. The compiler emits collection safe points before statements that may allocate or call code with unknown allocation behavior, and at loop-condition checks. Proven non-allocating integer/Boolean scalar statements omit redundant checks. A proven non-allocating fast integer/Boolean loop also omits per-iteration safe points; its enclosing `for` statement still checks before entry, and the next potentially allocating statement checks again. Runtime allocation helpers do not collect during expression evaluation, so temporary operands and return values remain valid until the expression finishes. The collector may retain unreachable allocations when stale pointer-shaped data remains on the stack; reclamation is not deterministic at a particular safe point.

RBL code must not manually free string payloads. The collector scans the current thread's stack range up to the frame pointer captured from generated `main`, so a materialized pointer in a native C stack local inside that range is conservatively treated as a root while the frame is active. Native heap/global storage and other threads are not scanned. This stack behavior is not a pinning or registration API; native code must not retain a managed pointer only in unscanned storage or after all corresponding RBL/stack roots cease to be live. Runtime state is process-global and unsynchronized: 0.8.0 supports only the thread that entered generated `main`, not concurrent calls from native worker threads.

## Scope

Condition and loop blocks do not create separate lexical scopes: local bindings belong to the function's shared environment. A range-loop variable remains bound after the loop if at least one iteration ran; when the loop executes zero times, no binding is created.

## Boolean evaluation

`and` and `or` evaluate both operands. They do not use short-circuit evaluation. The right-hand expression can execute even when the logical result is already determined by the left-hand operand. Use an `if` expression or an `if` block when conditional evaluation is required.

## `Unit` versus `null`

`Unit` represents the absence of a useful result and prints as an empty value. `null` is a separate runtime value and prints literally as `null`. Their comparison and behavior differ.

A missing dictionary key returns `Unit`, not `null`; use `in` to check whether the key exists.

## Structs

In 0.8.0, `struct` is syntax for declaring field names and receiver methods, not a full object system. A constructed instance is stored as a dictionary. A call such as `instance.method(args)` is lowered to an ordinary function call with the receiver as its first argument. The receiver type does not guarantee a dedicated runtime class identity.

## Platform-dependent observations

The current working directory, clock source and pseudorandom sequences depend on the platform. Differential tests should compare deterministic semantics rather than require byte-identical values for these platform-specific results.
