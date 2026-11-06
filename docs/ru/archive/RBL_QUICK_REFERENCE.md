# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL language quick reference — 0.7.1

This page documents implemented syntax in the current repository. Proposed syntax is kept separate in `SYNTAX_DECISIONS.md`.

## Program and functions

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    set result = add(2, 3)
    print(result)
)
```

Parentheses delimit blocks. The parameter and return-type syntax uses names followed by types, not colon annotations.

## Values and literals

Implemented scalar values: `int`, `float`, `string`, `bool`, `Unit` and `null`. Containers: list, dictionary and tuple. Struct declarations provide record-like construction and receiver-method syntax; see below.

```rbl
set decimal = 1.5
set exponent = 1.5e-3
set integer = 1_000_000
set hex = 0xFF
set binary = 0b1010
set octal = 0o755
set old_float_form = 10f
```

A leading zero does not imply octal: `017` is decimal seventeen. Octal uses the `0o` prefix. Both `//` and `#` start line comments.

Strings support `\n`, `\t`, `\r`, `\\`, `\"`, `\xNN`, `\uXXXX` and `\UXXXXXXXX`. Triple-quoted strings may span lines. NUL escapes are rejected because runtime strings are NUL-terminated.

Basic f-string interpolation is supported:

```rbl
set name = "RBL"
set answer = 42
print(f"{name}: {answer}")
print(f"literal braces: {{ and }}")
```

Format specifications such as `f"{value:.2f}"` are not implemented.

## Variables and assignment

- `set name = value` declares a new variable. A duplicate declaration is an error.
- `let name = value` rebinds an existing variable and requires the same runtime type.
- Compound assignment uses `let`: `let count += 1`, `let count *= 2`.
- Increment/decrement statements use `let count++` and `let count--`. They are statements, not general expressions.

## Operators

Arithmetic: `+`, `-`, `*`, `/`, `%`. Integer division and remainder truncate toward zero. For example, `-10 % 3` is `-1`. `%` is integer-only; use `rbl.math.fmod(a, b)` for floating-point remainder.

Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`. Logical operators: `and`, `or`, `not`; `!` is an alias for `not`.

`and` and `or` are **not short-circuiting**: both operands are evaluated.

`is` tests identity. Containers with equal contents can compare equal with `==` while remaining distinct under `is`.

## Conditions and loops

```rbl
if (value > 10) (
    print("large")
) elif (value == 10) (
    print("ten")
) else (
    print("small")
)

for (i in 0..5) (
    print(i)
)

for (condition) (
    // repeated while condition is true
)
```

`..` excludes the upper bound; `..=` includes it. `for (true)` is an infinite condition loop and normally exits with `break`. `break` exits the innermost loop; `continue` advances the innermost loop. Both are compile-time errors outside a loop.

Range/list iteration is supported. The current loop implementation does not define general dictionary iteration or tuple destructuring in loop headers.

Conditional expression:

```rbl
set label = "valid" if (ok) else "invalid"
```

Only the selected branch is evaluated. Keep the `if` on the same source line as the expression it qualifies.

`switch` uses `case` and `default` blocks:

```rbl
switch (code) (
    case 1 ( print("one") )
    case 2, 3 ( print("two or three") )
    case 4..=9 ( print("four through nine") )
    default ( print("other") )
)
```

The subject must be a variable or literal; expressions with side effects are rejected because the subject is checked for each case.

## Lists

```rbl
set values = [10, 20, 30]
print(values[0])
let values[1] = 25
print(len(values))
for (value in values) ( print(value) )
```

Lists can be nested and can contain mixed values, including other containers. Indexes must be integers and must be within bounds. Negative indexing and slicing are not part of the current implementation. `in` checks scalar membership in lists and tuples; the current implementation does not recursively search nested container elements for equivalent values. List concatenation with `+` is not implemented.

## Dictionaries

```rbl
set config = {"mode": "safe", "retries": 3}
print(config["mode"])
let config["retries"] = 4
print("mode" in config)
print(len(config))
```

The tested/documented key types are integers, floats and strings. A missing key evaluates to `Unit` (`()`), not `null`; use `in` when presence matters. Dictionary equality is deep and does not depend on key iteration order. Printed key order follows the hash-table layout and should not be treated as a stable ordering contract.

## Tuples

```rbl
set pair = (10, 20)
print(pair[0])
print(len(pair))
```

`(value)` is grouping, `()` is the empty tuple and `(a, b)` is a tuple. Tuples support indexing, length, printing and deep equality. Tuple elements cannot be assigned.

## Struct declarations

```rbl
struct Point (
    x int
    y int
)

func (p Point) move(dx int, dy int) (
    let p.x = p.x + dx
    let p.y = p.y + dy
)

set point = Point(3, 4)
point.move(1, 2)
print(point.x)
```

A struct must be declared before use. Its constructor takes one positional argument per declared field, in declaration order. In this version, a struct instance is stored as a dictionary keyed by field name and prints like a dictionary. Receiver methods are resolved by method name and lowered to ordinary function calls with the receiver as the first argument; the declared receiver type is not checked at runtime. There is no dedicated struct object tag or class system.

## `null` and `Unit`

`null` is a distinct value and prints as `null`. A function that returns without a value returns `Unit`, which prints as an empty value. They are not interchangeable.

## Current language omissions

Source imports, modules/multi-file compilation, globals/constants, exceptions, `defer`, lambdas/closures, default or variadic parameters, named arguments, overloads, async/await, nullable/container type expressions, destructuring, slices, negative indexing and f-string format specifications are not implemented in 0.7.1.
