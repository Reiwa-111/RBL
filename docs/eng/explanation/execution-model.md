# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Explanation: execution model and semantics

This page explains some less obvious properties of RBL 0.7.2. For the exact expression forms, see the [syntax reference](../reference/syntax.md).

## Functions, arguments and types

RBL has dynamic runtime values and limited checking of declared parameter types. Parameter types are checked when a function is called. A return-type annotation is parsed, but the current runtime does not verify the value actually returned.

Function arity is checked before argument expressions are evaluated. The arguments are then evaluated, after which parameter types are checked against the resulting values. Therefore, an arity error and a parameter-type error can have different observable side effects.

## Variables and evaluation order

`set` and `let` intentionally have different evaluation order:

- `set` first checks that the name has not already been declared, then evaluates the RHS.
- `let` evaluates the RHS first, then checks that the name exists and that its runtime type is compatible with the assignment.

As a result, the RHS of an invalid `let` can run, while a duplicate `set` fails before evaluating its RHS.

## Scope

Condition and loop blocks do not create separate lexical scopes: local bindings belong to the function's shared environment. A range-loop variable remains bound after the loop if at least one iteration ran; when the loop executes zero times, no binding is created.

## Boolean evaluation

`and` and `or` evaluate both operands. They do not use short-circuit evaluation. The right-hand expression can execute even when the logical result is already determined by the left-hand operand. Use an `if` expression or an `if` block when conditional evaluation is required.

## `Unit` versus `null`

`Unit` represents the absence of a useful result and prints as an empty value. `null` is a separate runtime value and prints literally as `null`. Their comparison and behavior differ.

A missing dictionary key returns `Unit`, not `null`; use `in` to check whether the key exists.

## Structs

In 0.7.2, `struct` is syntax for declaring field names and receiver methods, not a full object system. A constructed instance is stored as a dictionary. A call such as `instance.method(args)` is lowered to an ordinary function call with the receiver as its first argument. The receiver type does not guarantee a dedicated runtime class identity.

## Platform-dependent observations

The current working directory, clock source and pseudorandom sequences depend on the platform. Differential tests should compare deterministic semantics rather than require byte-identical values for these platform-specific results.
