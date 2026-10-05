# RBL Quick Reference — v0.7

RBL 0.7 keeps the current Rust-era language semantics but makes parentheses the canonical delimiter for blocks.

## Program structure

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    set x = 10
    let x = 20

    if (x > 10) (
        print("big")
    ) elif (x == 10) (
        print("exact")
    ) else (
        print("small")
    )

    for (i in 0..=5) (
        print(i)
    )

    print(add(2, 3))
)
```

## Values

`int`, `float` (`10f`), `string`, `bool`, and `Unit`.

Numeric literals: `42`, `0xFF`, `0b1010`, `0o755`, `1_000_000`, `1.5`, `10f`, `1e42`, `1.5e-3`. A leading zero without a prefix stays decimal (`017` is seventeen); octal is always `0o17`.

Comments run to the end of the line and start with `//` or `#`.

String literals use `"…"` with escapes `\n \t \r \\ \" \xNN \uXXXX \UXXXXXXXX`; `"""…"""` spans multiple lines (escapes still apply). A NUL byte (`\0`, `\x00`) is rejected because RBL strings are NUL-terminated C strings.

`print(...)` evaluates all arguments left-to-right. Unit prints as an empty field.

## Operators

Arithmetic: `+ - * / %`. Integer `/` and `%` truncate towards zero, so `%` takes the sign of the dividend (`-10 % 3` is `-1`) and `(a / b) * b + a % b == a` holds. `%` is integer-only: `float % int` is a runtime error, use `rbl.math.fmod(a, b)` for floats. Integer overflow, division by zero and `INT64_MIN % -1` are runtime errors.

Strings: `+` concatenates. Comparisons: `== != < > <= >=`. Booleans: `and or not` (`!` is an alias for `not`).

## Assignment

`set` declares a variable. Duplicate declaration is an error.

`let` rebinds an existing variable of the same runtime type; its right-hand side is evaluated first.

## Control flow

Conditions use their own parentheses and the body uses a second pair:

```rbl
if (x > 0) (
    print("positive")
)

for (i in 0..10) (
    print(i)
)
```

Ranges: `0..5` excludes `5`; `0..=5` includes `5`.

The loop variable remains bound after a loop that executed at least once.

`break` and `continue` are statements inside a `for` loop (no labels): `break` leaves the innermost loop, `continue` jumps to its increment. A loop whose body contains either of them stops using the optimized integer path (the tagged loop path handles them). Using them outside a loop is a compile error.

`for` has two forms. `for (i in 0..5)` iterates a range. `for (cond)` is a general loop that repeats while the condition is true: `set i = 0` … `for (i < 3) ( print(i)  let i += 1 )`. `for (true) ( … )` repeats until a `break`. The condition must be a `bool` at runtime — anything else is a `bool required` error, exactly as in `if`. Both forms share the same `break`/`continue` targets.

Conditional expression: `a if (cond) else b` (Python style, lowest precedence). Only the taken branch runs, so `x if (ok) else risky()` never calls `risky()` when `ok` is true. The `if` must be on the same line as the expression it qualifies — otherwise it is read as the start of the next statement.

`null` is a value of its own: `print(null)` shows `null`, and a function that returns nothing still prints an empty string, so the two are different. `null` works as a dictionary value or list element and compares with `==`/`!=`. Identity uses `is`: two separately built lists are `==` but not `is`; `xs is xs` is true.

Tuples and structs (phase 2.5):
```rbl
set t = (1, 2, 3)         // tuple; (x) is still grouping, () is the empty tuple
print(t[1])  print(len(t))  print((1, 2) == (1, 2))   // 2, 3, true
// tuples are immutable: let t[0] = 9 is an error

struct Point (
    x int
    y int
)

func (p Point) move(dx int, dy int) (   // receiver method
    let p.x = p.x + dx
    let p.y = p.y + dy
)

set p = Point(3, 4)
print(p.x)                // 3 — field read; also `p.x = 5` / `let p.x = 5`
print("x" in p)           // true — the instance is a record keyed by field name
p.move(1, 2)              // receiver is passed by reference: p.x is now 4
print(p)                  // {x: 4, y: 6} — a record prints as a dictionary
```
A struct must be declared before use; the constructor takes exactly one argument per field, in declaration order. The receiver's declared type is documentation — methods are resolved by name, so `p.move(1, 2)` is `move(p, 1, 2)`.

Dictionaries (phase 2):
```rbl
set d = {"a": 1, "b": 2}   // keys may be int, float or string; {} is empty
print(d["a"])              // 1
print(d["missing"])        // () — an empty value; use `in` to test membership
let d["c"] = 3             // element assignment; also written as d["c"] = 3
print(len(d))              // 3
print("a" in d)            // true  — membership for dictionary keys
print(1 in [1, 2, 3])      // true  — membership for list values
print("ell" in "hello")    // true  — substring test for strings
print(d)                   // {..}, in hash order (identical on both backends)
print({"x": 1} == {"x": 1})  // deep equality, key order does not matter
```

Lists (phase 2):
```rbl
set xs = [1, 2, 3]        // literal, may be nested and may hold mixed types
print(xs[0])              // indexing
let xs[1] = 20            // element assignment; also written as xs[1] = 20
print(len(xs))            // number of elements
for (v in xs) ( print(v) )  // iterate; break/continue work as usual
print([[1, 2], [3]])      // prints [[1, 2], [3]]
print([1, 2] == [1, 2])   // deep equality: true
```
Out-of-range indexing is a runtime error (`list index N is out of range`), and `for (x in ...)` accepts a range or a list (anything else reports `for (x in ...) expects a range or a list`). Lists are not freed yet (roadmap block 2.3), and list concatenation is not implemented — use indexing to modify elements.


```rbl
switch (x) (
    case 1 ( print("one") )
    case 2, 3 ( print("two or three") )
    case 4..=9 ( print("four to nine") )
    default ( print("other") )
)
```
The subject must be a variable or a literal (it is re-compared for each `case`, so calls with side effects are rejected). `switch` is a contextual keyword: it still works as an ordinary identifier outside statement position.

## Boolean operators

`and` and `or` are not short-circuiting. Both operands are evaluated.

## Standard library

Global functions:

```text
len(string)
input() / input(prompt)
read_file(path) / write_file(path, content)
abs(x) / sqrt(x)
min(x, ...) / max(x, ...)
int(x) / float(x) / str(x)
pow(a, b) / floor(x) / ceil(x) / round(x)
sin(x) / cos(x) / tan(x) / log(x) / exp(x)
random_int(a, b) / random_float() / random_bool()
```

Namespaced APIs:

```text
rbl.io.input / prompt / read_file / write_file
rbl.math.abs / sqrt / pow / min / max / floor / ceil / round / sin / cos / tan / log / exp
rbl.string.len / upper / lower / trim / contains / starts_with / ends_with
rbl.fs.exists / delete / cwd
rbl.time.now_ms / sleep_ms
rbl.random.int / float / bool
```

`len` counts UTF-8 code points. `read_file`/`write_file` operate on text strings. `min/max` require numeric arguments of one common type. `upper/lower` currently perform ASCII case conversion. The random API is not cryptographically secure.

Conversions supported by `int`, `float` and `str` cover the scalar values used by RBL: integers, floats, booleans and strings where meaningful.

## Methods

`warn.log(...)` prints `[WARN] ...`.

`error.log(...)` prints `[ERROR] ...`.

Unknown methods are runtime errors after their arguments have been evaluated.

## Legacy syntax

`{ ... }` is still accepted for compatibility with older RBL programs, but new code should use `( ... )` for blocks.
