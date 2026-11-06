# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Reference: RBL 0.7.2 syntax

This page describes the syntax implemented by the current frontend. Examples target RBL 0.7.2 and its regression tests; proposals for future versions are not part of this specification.

## Programs and functions

A program contains function and `struct` declarations. The executable entry point is a zero-argument `main` function.

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    set answer = add(20, 22)
    print(answer)
)
```

A function declaration has the form `func name(parameters) ReturnType (` followed by its body and a closing `)`. The return-type annotation is optional; without one, a function that returns no value produces `Unit`. Parameter types follow parameter names. Parameter values are checked against the declared types when the function is called.

Recursive calls are supported. Functions are not overloaded by signature; when a function name is declared more than once, the last definition is used (covered by `duplicate_functions.rbl`). Return-type annotations are parsed but are not enforced by the runtime in 0.7.2; do not treat them as a statically verified return contract.

## Declaring and updating variables

```rbl
set count = 10
let count = 11
let count += 4
let count *= 2
let count++
let ++count
```

- `set name = expression` declares a new variable. A duplicate declaration is an error.
- `let name = expression` updates an existing variable. An unknown name is an error.
- Ordinary assignment requires the new runtime value to have the same type as the existing value.
- Compound assignment uses `let` with `+=`, `-=`, `*=`, `/=` or `%=`.
- Increment/decrement statements use `let name++`, `let name--`, `let ++name` or `let --name`; they are statements, not expressions.

Blocks do not introduce nested lexical scopes: blocks inside a function share that function's variable environment. `set` checks whether a name already exists before evaluating the right-hand side; `let` evaluates the RHS before checking that the name exists and that its runtime type is compatible. This difference is observable when the RHS has side effects or fails.

## Types and values

RBL stores dynamically typed runtime values, while also supporting checks for declared function parameter types. Struct instances are represented as dictionaries at runtime, not as distinct objects with a full class/type system.

| Type/value | Description |
|---|---|
| `int` | Signed 64-bit integer. Checked integer operations report a runtime error on overflow. |
| `float` | IEEE-754 binary64 floating-point value. |
| `string` | UTF-8 string represented internally as a NUL-terminated string. Embedded NUL escapes are rejected. |
| `bool` | `true` or `false`. |
| `Unit` | Absence of a useful result; a function that returns no value produces `Unit`. |
| `null` | A distinct value, not equal to `Unit`. |
| list | Mutable, indexed sequence of values. |
| dictionary | Mutable mapping from keys to values. |
| tuple | Ordered, immutable sequence of values. |
| struct instance | Record represented at runtime as a dictionary of fields; there is no separate struct runtime type. |

Local variables are declared without annotations: `set x = 1`. The `x: int` syntax for ordinary variables is not implemented.

## Literals

```rbl
set decimal = 17
set separated = 1_000_000
set hex = 0xFF
set binary = 0b1010
set octal = 0o755
set float_a = 1.5
set float_b = 1.5e-3
set legacy_float = 10f
set yes = true
set empty = null
set text = "RBL"
```

A leading zero does not imply octal: `017` is decimal seventeen. Octal requires the `0o` prefix. Numeric literals support underscore separators. The historical `f` suffix form is also accepted for float literals.

## Comments and strings

Both comment forms are single-line comments:

```rbl
// C-style line comment
# hash-style line comment
```

Strings support `\\n`, `\\t`, `\\r`, `\\\\`, `\\"`, `\\xNN`, `\\uXXXX` and `\\UXXXXXXXX` escape sequences. Triple-quoted strings can span multiple lines.

F-strings support basic interpolation and escaped braces:

```rbl
set name = "RBL"
set value = 42
print(f"{name}: {value}")
print(f"braces: {{ and }}")
```

Format specifications such as `f"{value:.2f}"` are not implemented. The `\\0` escape is rejected because runtime strings are NUL-terminated.

## Operators

### Arithmetic

| Operator | Meaning |
|---|---|
| `+` | Addition; also string concatenation. |
| `-` | Subtraction. |
| `*` | Multiplication. |
| `/` | Division. |
| `%` | Integer remainder; operands must be `int`. |

Integer division truncates toward zero. The remainder has the sign of the dividend: `-10 % 3 == -1`. For float remainder, use the global function `fmod(a, b)`. Division or remainder by zero raises a runtime error.

### Comparisons and logic

Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`.

Logical operators: `and`, `or`, `not`; `!` is an alternative spelling of `not`.

`and` and `or` are **not short-circuiting**: both operands are evaluated. The right-hand expression can therefore fail or produce side effects even when the left-hand value already determines the Boolean result.

`is` checks identity, while `==` checks equality. Two distinct containers with equal contents can compare equal with `==` but not be identical under `is`.

### Conditional expression

```rbl
set message = "ok" if (valid) else "invalid"
```

Only the selected branch is evaluated. Keep `if` on the same source line as the expression it qualifies.

## Conditions and loops

```rbl
if (score >= 90) (
    print("high")
) elif (score >= 60) (
    print("medium")
) else (
    print("low")
)
```

Range loops:

```rbl
for (i in 0..5) (
    print(i)
)

for (i in 0..=5) (
    print(i)
)
```

`..` excludes the upper bound; `..=` includes it. Iteration over a list and condition loops are also supported:

```rbl
for (item in items) (
    print(item)
)

for (running) (
    if (done) ( break )
)
```

`break` exits the innermost loop; `continue` advances to its next iteration. Both are compile-time errors outside a loop. `for (condition)` repeats its body while the condition is true.

Nested loops are supported. `for (x in ...)` accepts a range or a list. A range-loop variable remains bound after a loop if at least one iteration ran; no binding is introduced when the loop executes zero times. General dictionary iteration and tuple destructuring in loop headers are not implemented.

## `switch`

```rbl
switch (code) (
    case 1 ( print("one") )
    case 2, 3 ( print("two or three") )
    case 4..=9 ( print("range") )
    default ( print("other") )
)
```

`case` supports comma-separated values and ranges. `default` provides the fallback branch. The subject must be a variable or a literal; arbitrary expressions with side effects are rejected because the subject is checked during case matching.

## Lists

```rbl
set values = [10, 20, 30]
print(values[0])
let values[1] = 25
print(len(values))
print(20 in values)
```

Indexing is zero-based. An index must be an integer within the list bounds. Lists are mutable, can be nested and can contain mixed value types. The `in` operator checks immediate scalar elements in lists and tuples; nested lists, dictionaries and tuples are skipped, so membership is not recursive. For dictionaries, `key in dictionary` checks whether a key exists. When the right-hand operand is a string, `in` checks for a substring; `rbl.string.contains(text, part)` is also available.

Negative indexing, slicing and list concatenation with `+` are not implemented.

## Dictionaries

```rbl
set config = {"mode": "safe", "retries": 3}
print(config["mode"])
let config["retries"] = 4
print("mode" in config)
print(len(config))
```

Tested key types are `int`, `float` and `string`. A missing key returns `Unit` (`()`), not `null`; use `in` to test whether the key exists. Dictionary equality is deep and independent of key order. Printed key order follows the hash-table layout and is not a stable ordering contract.

## Tuples

```rbl
set pair = (10, 20)
print(pair[0])
print(len(pair))
```

`(value)` groups an expression, `()` creates an empty tuple, and `(a, b)` creates a tuple. Tuples support indexing, length, printing and deep equality. Tuple elements cannot be reassigned. An empty tuple is not the same as `Unit`, which is returned by a function with no value.

## Structs and receiver methods

```rbl
struct Point (
    x int
    y int
)

func (p Point) move(dx int, dy int) (
    let p.x = p.x + dx
    let p.y = p.y + dy
)

set p = Point(3, 4)
p.move(1, 2)
print(p.x)
```

A struct must be declared before use. Its constructor takes one positional argument per field, in declaration order. An instance is a dictionary keyed by field names and prints like a dictionary. A receiver method call is lowered to an ordinary function call with the receiver as the first argument. The receiver type does not provide a distinct runtime class and is not enforced as object identity.

## I/O and the standard library

Global built-ins and namespace functions are available without `import`. See the [standard-library reference](standard-library.md) for signatures and platform details. Source imports and user-defined modules are not available in this version.

## Common errors and semantic boundaries

- Duplicate `set` declarations and `let` assignments to unknown names are errors.
- Passing a value that does not match a declared parameter type is an error.
- `%` with a float is invalid; use the global `fmod` function.
- Non-integer or out-of-range list indices are errors.
- `break`/`continue` outside loops and ranges outside `for` headers are errors.
- `and`/`or` evaluate both operands.
- `null` and `Unit` are distinct values.
- A missing dictionary key evaluates to `Unit`, not `null`.
- Function arity is checked before argument expressions are evaluated; parameter types are checked after argument evaluation.

## Not implemented

See the [complete limitations list](limitations.md): imports, multi-file compilation, globals/constants, exceptions, `defer`, lambdas/closures, named/default/variadic arguments, overloads, async/await, destructuring, slices, negative indexing and f-string format specifications.
