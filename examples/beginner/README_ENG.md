# RBL 0.7.2 - Beginner's Guide

> **Guide version: RBL 0.7.2.** This guide describes features implemented by the compiler and runtime in version **0.7.2**. It may become outdated in future RBL versions; always check the documentation and tests for the version you use.

---

## 1. What is RBL, and who is this guide for?

RBL (ReiwaBatLanguage) is a compiled programming language with its own frontend, assembly generation and runtime. In this version, programs are written in `.rbl` source files and executed through the CLI included in the repository.

This guide is intended for two audiences:

- **Programming beginners.** Start at Lesson 1 and follow the lessons in order. You will need to learn the basic ideas behind variables, conditions, loops, functions and collections.
- **People who already know Python or another language.** You can skim the first lessons and use the reference sections as a map of RBL syntax, features and differences.

### Can someone with Python knowledge learn RBL independently?

**Yes. Someone who already understands Python variables, conditions, loops, functions, lists and dictionaries should be able to learn basic RBL independently using this guide, the twenty examples and the linked documentation.** They do not need to relearn programming fundamentals; the main tasks are learning a different syntax and understanding several runtime differences.

However, “a little Python” can mean very different things. Someone who has only tried a few commands and does not yet understand functions, mutable objects or scope should follow the course from the beginning. Someone who has written small programs independently should have enough background to read and write small RBL programs after working through the examples and reference sections.

This does not mean that one README covers every implementation detail. For confident use, read the example sources, run and modify them, consult the syntax reference and test behavior with `rbl.py check` and `rbl.py run`.

### Important facts before you start

- RBL 0.7.2 is not Python with renamed keywords. Some concepts may look familiar, but blocks, variable declarations and parts of runtime semantics are different.
- The course examples are written in **RBL**. Python is only used to run the repository's `rbl.py` CLI tool; it is not the language used in the examples.
- Built-in functions and standard-library namespaces are available without `import`.
- User-defined `import`, a package manager and multi-file compilation are not available in this version.

## 2. Setup and running programs

This guide assumes that you have downloaded the RBL repository and run commands from its root directory. The CLI is the `rbl.py` file; it does not install a global `rbl` command on your system.

Check your environment:

**Windows PowerShell**

```powershell
python rbl.py doctor
python rbl.py version
```

**Linux / WSL**

```bash
python3 rbl.py doctor
python3 rbl.py version
```

`doctor` checks the selected backend and required tools. Linux requires GNU assembler and linker. Native Windows builds use MinGW-w64; WSL is also supported. See the [Windows and WSL guide](../../docs/eng/how-to/windows-toolchain.md) for details.

### Check and run your first program

```bash
python3 rbl.py check examples/beginner/01_hello_variables.rbl
python3 rbl.py run examples/beginner/01_hello_variables.rbl
```

On Windows, use `python` instead of `python3` if that is how Python is invoked on your system.

- `check` runs frontend, analysis and code generation, but does not invoke the assembler or linker and does not create an executable.
- `run` builds and executes the program.
- `build` builds an executable without running it.
- `asm` displays the generated assembly.

## 3. The twenty lessons

Open each `.rbl` file in an editor and read its comments. After each lesson, modify the source and run it again.

| # | Program | Topics |
|---:|---|---|
| 1 | [01_hello_variables.rbl](01_hello_variables.rbl) | `main`, output, comments, `set`, basic values and variables. |
| 2 | [02_calculator.rbl](02_calculator.rbl) | `input`, string-to-number conversion and arithmetic. Enter two non-zero integers. |
| 3 | [03_conditions.rbl](03_conditions.rbl) | Comparisons, `bool`, `if` / `elif` / `else`, `switch` and logical operators. |
| 4 | [04_loops.rbl](04_loops.rbl) | Ranges, `for`, accumulators, `break` and `continue`. |
| 5 | [05_functions.rbl](05_functions.rbl) | Function declarations, parameter types, calls and `return`. |
| 6 | [06_lists.rbl](06_lists.rbl) | Lists, zero-based indexing, element updates, `in` and iteration. |
| 7 | [07_dictionaries_tuples.rbl](07_dictionaries_tuples.rbl) | Dictionaries, keys and values, key membership and tuples. |
| 8 | [08_structs.rbl](08_structs.rbl) | `struct`, fields, positional construction and receiver methods. |
| 9 | [09_strings.rbl](09_strings.rbl) | Strings, concatenation, f-strings and `rbl.string` functions. |
| 10 | [10_standard_library.rbl](10_standard_library.rbl) | Examples of `rbl.math`, `rbl.random`, `rbl.fs`, `rbl.time` and global `clamp`. |
| 11 | [11_nested_loops.rbl](11_nested_loops.rbl) | Nested loops: the inner loop completes for each outer-loop iteration. |
| 12 | [12_condition_loops.rbl](12_condition_loops.rbl) | `for (condition)`, `for (true)` and stopping with `break`. |
| 13 | [13_nested_lists.rbl](13_nested_lists.rbl) | Lists of lists, chained indexing, nested element updates and double iteration. |
| 14 | [14_file_io.rbl](14_file_io.rbl) | `write_file`, `read_file`, `rbl.fs.exists` and `rbl.fs.delete`; creates and removes a file in `build/`. |
| 15 | [15_multiline_strings_escapes.rbl](15_multiline_strings_escapes.rbl) | Triple-quoted strings and `\\n`, `\\t`, `\\\\`, `\\"`, `\\xNN`, `\\uXXXX`, `\\UXXXXXXXX`. |
| 16 | [16_nested_break_continue.rbl](16_nested_break_continue.rbl) | `break` and `continue` in nested loops; nearest-loop behavior. |
| 17 | [17_recursion.rbl](17_recursion.rbl) | Recursive functions, base cases, factorial and recursive summation. |
| 18 | [18_nested_dictionaries.rbl](18_nested_dictionaries.rbl) | Nested dictionaries, chained keys and updating nested values. |
| 19 | [19_logging.rbl](19_logging.rbl) | `warn.log`, `error.log` and the difference between logging and terminating a program. |
| 20 | [20_math_time_filesystem.rbl](20_math_time_filesystem.rbl) | Additional math functions, `rbl.random.float`, `rbl.time.sleep_ms` and filesystem operations. |

### Input for Lesson 2

The program reads two lines from standard input. On Linux/WSL:

```bash
printf '12\n3\n' | python3 rbl.py run examples/beginner/02_calculator.rbl
```

For inputs `12` and `3`, the expected sum is `15`, difference `9`, product `36` and quotient `4`. Entering zero as the second number causes a division-by-zero runtime error; the example does not include a separate check for this case.

## 4. Program structure

A minimal program:

```rbl
func main() (
    print("Hello, RBL!")
)
```

- `func` declares a function.
- `main` is the entry point and must have no parameters.
- The parentheses after the function name contain its parameter list. It is empty here.
- The `(` and `)` around the body delimit a block. RBL does not use curly braces for blocks.
- `print(...)` writes values to the terminal.

A program can contain function and `struct` declarations. In RBL 0.7.2, one source file is compiled as one program.

### Comments

Both forms are single-line comments:

```rbl
// C++-style comment
# Python-style comment
```

Comments explain the code; they do not change program behavior.

## 5. Variables, types and values

### `set` and `let`

```rbl
set score = 10
let score = 15
let score += 5
let score *= 2
let score++
let score--
```

- `set name = value` **declares** a new variable.
- `let name = value` **updates** an existing variable.
- Declaring the same name with `set` twice is an error.
- Using `let` with an unknown name is an error.
- An ordinary update requires the new runtime value to have the same type as the current value.
- For a plain variable identifier, `let` supports `+=`, `-=`, `*=`, `/=`, `%=` and increment/decrement forms `name++`, `name--`, `++name`, `--name` (with `let`, e.g. `let ++name`). These shorthand forms do not work on list elements (`values[0]`) or struct fields (`point.x`); use an ordinary `let` assignment for those.

Unlike Python, RBL does not use `x = 5` to declare or update a variable. Use `set` or `let`.

### Types in RBL 0.7.2

| Type / value | Meaning |
|---|---|
| `int` | Signed 64-bit integer. Checked integer operations report overflow errors. |
| `float` | IEEE-754 binary64 floating-point value. |
| `string` | UTF-8 string, represented internally as NUL-terminated. |
| `bool` | `true` or `false`. |
| `null` | The distinct `null` value. |
| `Unit` | Absence of a useful result; returned by a function with no return value. |
| list | Mutable indexed list. |
| dictionary | Mutable mapping of keys to values. |
| tuple | Ordered immutable sequence. |
| struct instance | A struct instance, represented as a dictionary of fields. |

RBL stores values dynamically, but function parameter types can be declared and are checked when a function is called. Ordinary local variables do not have annotations such as `x: int`; their type is determined by the value assigned with `set`.

`Unit` and `null` are not the same. An empty tuple `()` is also different from `Unit`.

### Numeric and Boolean literals

```rbl
set decimal = 17
set million = 1_000_000
set hexadecimal = 0xFF
set binary = 0b1010
set octal = 0o755
set fraction = 1.5
set scientific = 1.5e-3
set old_float_form = 10f
set enabled = true
set missing = null
```

A leading zero does not make a number octal: `017` is decimal. Octal literals require the `0o` prefix. Underscores can separate digits. The historical `f` suffix form is accepted for float literals.

## 6. Operators and expressions

### Arithmetic

| Operator | Operation |
|---|---|
| `+` | Addition; also concatenates two strings. |
| `-` | Subtraction. |
| `*` | Multiplication. |
| `/` | Two `int` operands produce an `int` truncated toward zero; two `float` operands produce a `float`. Mixed `int`/`float` operands are not supported. |
| `%` | Integer remainder; both operands must be `int`. |

```rbl
set sum = 7 + 3
set difference = 7 - 3
set product = 7 * 3
set quotient = 7 / 2
set float_quotient = 7.0 / 2.0
set remainder = 7 % 3
set greeting = "Hello, " + "RBL"
```

Integer division truncates toward zero: `7 / 2` produces `3`. If both operands are `float`, the result is also a `float`: `7.0 / 2.0` produces `3.5`. **Mixed `int`/`float` pairs are not supported**: `7.0 / 2` and `7 / 2.0` raise a runtime error. The remainder has the sign of the dividend: `-10 % 3` produces `-1`. For float remainder, use the global function `fmod(a, b)`. Division and remainder by zero raise a runtime error.

### Comparisons and logic

- Comparisons: `==`, `!=`, `<`, `>`, `<=`, `>=`.
- Logic: `and`, `or`, `not`; `!` is an alternative spelling of `not`.
- `==` checks equality; `is` checks value/object identity.

**Important:** `and` and `or` in RBL 0.7.2 do not use short-circuit evaluation. Both operands are evaluated. Do not put an expression on the right-hand side if it is unsafe to evaluate regardless of the left-hand result.

Conditional expressions are supported:

```rbl
set score = 12
set message = "allowed" if (score >= 10) else "denied"
```

Only the selected branch is evaluated. In this form, keep `if` on the same line.

## 7. Conditions and `switch`

```rbl
set temperature = 18

if (temperature >= 25) (
    print("Warm")
) elif (temperature >= 10) (
    print("Cool")
) else (
    print("Cold")
)
```

`if` checks the first condition; `elif` adds further checks; `else` runs when none of the previous conditions match.

`switch` selects a branch based on a value:

```rbl
set code = 2
switch (code) (
    case 1 ( print("One") )
    case 2, 3 ( print("Two or three") )
    case 4..=9 ( print("Four through nine") )
    default ( print("Another value") )
)
```

`case` supports comma-separated values and ranges. `default` is the fallback branch. The `switch` subject must be a variable or a literal; arbitrary expressions with side effects are not allowed there.

## 8. Loops

### Ranges

```rbl
for (i in 0..5) (
    print(i)
)

for (i in 0..=5) (
    print(i)
)
```

- `0..5` runs from `0` through `4`: the upper bound is exclusive.
- `0..=5` runs from `0` through `5`: the upper bound is inclusive.

### Iterating over a list

```rbl
set names = ["Ada", "Grace", "Edsger"]
for (name in names) (
    print(name)
)
```

The `for (item in source)` form accepts a range or a list. General dictionary iteration is not available in 0.7.2.

### Condition loop

```rbl
set counter = 0
for (counter < 3) (
    print(counter)
    let counter++
)
```

`for (condition)` repeats its body while the condition is true. This is not Python's three-part `for` syntax.

### `break`, `continue` and nested loops

```rbl
for (row in 0..3) (
    for (column in 0..3) (
        if (column == 1) ( continue )
        if (row == 2) ( break )
        print(row, column)
    )
)
```

- `break` exits the nearest enclosing loop.
- `continue` skips the rest of the current iteration of the nearest loop.
- Nested loops are supported.
- `break` and `continue` outside a loop are errors.

A range-loop variable remains available after the loop if at least one iteration ran. If the range produces no iterations, the loop variable is not introduced. Blocks do not create separate local scopes.

## 9. Functions and recursion

```rbl
func add(a int, b int) int (
    return a + b
)

func greet(name string) (
    print("Hello,", name)
)

func main() (
    set result = add(20, 22)
    greet("RBL")
    print(result)
)
```

Declarations start with `func`. A parameter type follows its name: `a int`. An optional return type follows the closing parameter parenthesis: `) int (`. `return` returns a value. A function with no return value produces `Unit`.

Parameter types are checked at call time. Return-type annotations are parsed but are not enforced by the runtime as strict contracts in 0.7.2. Function overloading is not available. If a function name is declared more than once, the last definition is used. Named, optional and variadic parameters are not available.

### Recursion

Recursive calls are supported:

```rbl
func factorial(n int) int (
    if (n <= 1) (
        return 1
    )
    return n * factorial(n - 1)
)

func main() (
    print(factorial(5))
)
```

A recursive function needs a stopping condition. This example demonstrates syntax; it does not imply unlimited recursion depth.

## 10. Lists

```rbl
set values = [10, 20, 30]
print(values[0])
let values[1] = 25
print(len(values))
print(20 in values)
```

- Lists use square brackets: `[a, b, c]`.
- Indexing is zero-based.
- Lists are mutable; replace an element with `let list[index] = value`.
- Lists can be nested and can contain values of different types.
- `len(list)` returns the number of elements.
- `in` checks immediate scalar elements in a list/tuple. It does not recursively search nested containers. For strings, `in` checks whether a substring is present.
- `==` deeply compares container contents; dictionary equality does not depend on key order. `is` checks identity rather than equality.

Nested-list example:

```rbl
set matrix = [[1, 2], [3, 4]]
print(matrix[0][1])
```

Negative indexing, slicing and list concatenation with `+` are not implemented in 0.7.2.

## 11. Dictionaries and nested dictionaries

```rbl
set profile = {"name": "Mira", "level": 3}
print(profile["name"])
let profile["level"] = 4
print("level" in profile)
print(len(profile))
```

A dictionary stores key-value pairs. Supported key types are `int`, `float`, `string` and `bool` (`true` / `false`). The `in` operator checks whether a **key** exists, not whether a value exists.

```rbl
set settings = {
    "video": {"width": 1920, "height": 1080},
    "audio": {"volume": 80}
}
print(settings["video"]["width"])
```

Nested dictionaries can be read using chained indexing. General iteration over dictionary entries is not available.

Reading a missing key returns `Unit` (`()`), not `null`. Use `in` to check whether a key exists. Dictionaries are compared by contents regardless of key order. Printed dictionary order is not guaranteed.

## 12. Tuples

```rbl
set position = (12, 25)
print(position[0])
print(len(position))
```

- `(a, b)` creates a tuple.
- `()` is an empty tuple.
- `(value)` groups an expression; it is not a one-element tuple.
- Tuples support indexing, `len`, printing and deep equality.
- Tuple elements cannot be reassigned.

Tuple destructuring into variables is not implemented.

## 13. Structs and receiver methods

```rbl
struct Point (
    x int
    y int
)

func (p Point) move(dx int, dy int) (
    let p.x = p.x + dx
    let p.y = p.y + dy
)

func main() (
    set point = Point(3, 4)
    point.move(1, 2)
    print(point.x, point.y)
)
```

- `struct` declares a set of named fields.
- A struct must be declared before it is used.
- Its constructor takes one positional argument per field, in declaration order.
- A receiver method is declared as `func (receiver Type) name(...) (...)`.
- `point.move(...)` is lowered to an ordinary function call with `point` as the first argument.

This is **not a full class system**: a struct instance is represented as a dictionary at runtime. There is no distinct runtime struct identity, inheritance or full object system. Named field construction such as `Point(x: 1, y: 2)` is not available.

## 14. Strings

### Ordinary strings, concatenation and f-strings

```rbl
set language = "RBL"
set message = "Hello, " + language
print(message)
print(f"Welcome to {language}!")
print(f"Braces: {{ and }}")
```

`+` concatenates strings. F-strings support basic value interpolation and escaped braces using `{{` and `}}`.

Format specifications such as `f"{value:.2f}"` are not implemented.

### Escape sequences

Supported escapes:

| Escape | Meaning |
|---|---|
| `\\n` | Newline. |
| `\\t` | Tab. |
| `\\r` | Carriage return. |
| `\\\\` | Backslash. |
| `\\"` | Double quote. |
| `\\xNN` | Character encoded by two hexadecimal digits. |
| `\\uXXXX` | Unicode escape with four hexadecimal digits. |
| `\\UXXXXXXXX` | Unicode escape with eight hexadecimal digits. |

The `\\0` escape is rejected because runtime strings are NUL-terminated.

### Multiline strings

Triple quotes allow strings to span multiple source lines:

```rbl
set description = """This is the first line.
This is the second line.
This is the third line."""
print(description)
```

### `rbl.string` functions

| Function | Purpose |
|---|---|
| `rbl.string.len(text)` | Number of UTF-8 code points in the string. |
| `rbl.string.upper(text)` | Converts ASCII letters to uppercase. |
| `rbl.string.lower(text)` | Converts ASCII letters to lowercase. |
| `rbl.string.trim(text)` | Trims spaces, tabs, LF and CR from the ends. |
| `rbl.string.contains(text, part)` | Checks whether a substring is present. |
| `rbl.string.starts_with(text, prefix)` | Checks a prefix. |
| `rbl.string.ends_with(text, suffix)` | Checks a suffix. |

`upper` and `lower` only handle ASCII letters; they do not perform Unicode case conversion. Strings are UTF-8, and `len` counts Unicode code points rather than bytes.

## 15. Input, output and file operations

### Terminal I/O

```rbl
print("Enter your name:")
set name = input("Name: ")
print("Hello,", name)
```

- `print(value, ...)` writes arguments separated by spaces and appends a newline.
- `input()` reads one line from standard input.
- `input(prompt)` prints and flushes the prompt, then reads a line.
- `input` returns a `string`. Use `int(...)` or `float(...)` for numeric conversion.

### Files

```rbl
set contents = read_file("notes.txt")
write_file("copy.txt", contents)
```

Or use the namespace:

```rbl
set contents = rbl.io.read_file("notes.txt")
rbl.io.write_file("copy.txt", contents)
```

| Function | Purpose |
|---|---|
| `read_file(path)` / `rbl.io.read_file(path)` | Reads a text file. |
| `write_file(path, content)` / `rbl.io.write_file(path, content)` | Writes a text file. |
| `rbl.fs.exists(path)` | Checks whether a path exists. |
| `rbl.fs.delete(path)` | Deletes a file/path and returns a success indicator. |
| `rbl.fs.cwd()` | Returns the current working directory. |

Paths and the current directory depend on the operating system and how the program is launched. Do not expect `cwd()` to return the same string on Windows and Linux.

## 16. Standard library: complete name overview

Built-in functions are available globally. Namespace functions do not require `import`; this does not mean that RBL has a user-defined module system.

### Global built-ins

| Name | Purpose |
|---|---|
| `print(value, ...)` | Prints values separated by spaces, followed by a newline. |
| `len(value)` | String length in UTF-8 code points or item count for list/dictionary/tuple. |
| `input()` / `input(prompt)` | Reads one line from stdin, with or without a prompt. |
| `read_file(path)` | Reads a text file. |
| `write_file(path, content)` | Writes a text file. |
| `abs(x)` | Absolute value. |
| `sqrt(x)` | Square root. |
| `min(x, ...)`, `max(x, ...)` | Minimum/maximum of numeric arguments of a common type. |
| `int(x)`, `float(x)`, `str(x)` | Conversions for supported scalar values. |
| `pow`, `floor`, `ceil`, `round` | Power and rounding operations. |
| `sin`, `cos`, `tan` | Trigonometric functions; arguments are in radians. |
| `log`, `log10`, `log2`, `exp` | Logarithms and exponential. |
| `random_int(a, b)` | Pseudorandom integer in an inclusive range. |
| `random_float()` | Pseudorandom float in `[0, 1]`. |
| `random_bool()` | Pseudorandom Boolean. |
| `trunc`, `fmod`, `hypot` | Truncation, float remainder and hypotenuse. |
| `degrees`, `radians` | Degree/radian conversion. |
| `clamp`, `sign` | Clamp a numeric value to a range and return its sign. |
| `is_nan`, `is_inf` | Check for NaN and infinity. |

### Namespace `rbl.math`

Implemented namespace methods: `abs`, `sqrt`, `pow`, `min`, `max`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`, `log10`, `log2`, `trunc`, `sign`, `is_nan`, `is_inf`.

Constants: `rbl.math.pi` and `rbl.math.e` are values, not functions; they are available only through the `rbl.math` namespace, not as global names. Do not add parentheses.

Important: `fmod`, `hypot`, `degrees`, `radians` and `clamp` are available as global functions, but not as `rbl.math` methods in the 0.7.2 implementation. In particular, `rbl.math.clamp(...)` produces an unknown-method runtime error; use `clamp(...)`. Some entries in the separate standard-library reference list more namespace methods than the implementation actually provides; this guide lists the implemented set.

### Namespace `rbl.io`

- `rbl.io.input()`
- `rbl.io.prompt(text)`
- `rbl.io.read_file(path)`
- `rbl.io.write_file(path, content)`

### Namespace `rbl.random`

- `rbl.random.int(min, max)` — inclusive range.
- `rbl.random.float()` — value in `[0, 1]`.
- `rbl.random.bool()` — random Boolean value.

### Namespace `rbl.fs`

- `rbl.fs.exists(path)`
- `rbl.fs.delete(path)`
- `rbl.fs.cwd()`

### Namespace `rbl.time`

- `rbl.time.now_ms()` — millisecond clock value for the current platform.
- `rbl.time.sleep_ms(duration)` — pauses for a non-negative number of milliseconds.

**Platform caveat:** `now_ms()` is implemented differently. Linux uses `CLOCK_REALTIME` (wall-clock time), while Windows uses `GetTickCount64()` (monotonic time since boot). The values and clock semantics are not comparable across platforms. Do not use this API as a portable absolute timestamp or a universal benchmark timer.

### Logging

```rbl
warn.log("Warning message")
error.log("Error message")
```

Messages are printed with `[WARN]` and `[ERROR]` prefixes. These are the two logging methods in the documented API, not a fully configurable logging framework.

### Random numbers: an important limitation

The generator is pseudorandom and **not cryptographically secure**. Linux uses the C library `rand()`; Windows uses an internal linear congruential generator. Sequences differ across platforms. Do not use these functions for passwords, tokens, keys or other secrets.

## 17. Project CLI commands

Commands are invoked through `python rbl.py ...` or `python3 rbl.py ...` from the repository.

| Command | Purpose |
|---|---|
| `version` | Prints the version from the root `VERSION` file. |
| `doctor` | Checks the toolchain. |
| `bootstrap` | Forces a rebuild of bootstrap compiler/runtime artifacts. |
| `env` | Prints RBL/project root paths and the Python executable. |
| `new NAME [--path DIR]` | Creates a project. |
| `init` | Initializes the current directory. |
| `check [FILE]` | Runs frontend/analysis/codegen checks. |
| `build [FILE]` | Compiles and links an executable. |
| `run [FILE]` | Builds and runs a program. |
| `asm [FILE]` | Displays generated assembly. |
| `test` | Runs the RBL checkout's regression suite and stdlib smoke test. |
| `bench` | Runs the RBL-only benchmark. |
| `bench3way [--reps N] [--warmups N]` | Compares implementations; available on Linux/WSL. The name is retained for compatibility. |
| `diff [--a MODE] [--b MODE] [PATH ...]` | Compares backends. |
| `fmt [FILE] [--check]` | Formats a file or checks formatting. |
| `doc [FILE] [-o FILE]` | Generates a simple function-declaration table, not full API documentation. |
| `clean` | Removes `build/` in the current directory. |

For several commands, the file argument is optional. The CLI uses `package.entry` from the manifest, then `src/main.rbl` by default; the documented fallback also checks for `main.rbl` in the project root. See the [CLI reference](../../docs/eng/reference/cli.md).

Example project creation:

```bash
python3 rbl.py new hello
cd hello
python3 ../rbl.py check
python3 ../rbl.py run
```

## 18. What is NOT implemented in RBL 0.7.2

This section is important: do not assume that a Python or another language feature exists in RBL unless it is implemented by the current frontend/runtime.

| Feature | Status in 0.7.2 |
|---|---|
| `import`, user modules, multi-file compilation | Not available. One source file per program. |
| Global variables and constants | Not available. |
| Exceptions: `try`, `catch`, `finally`, `raise` | Not available. |
| `defer` | Not available. |
| Lambdas and closures | Not available. |
| Named, default and variadic arguments | Not available. |
| Function overloading | Not available. |
| Async/await and task scheduler | Not available. |
| Generics and an advanced static type system | Not available. |
| Nullable type annotations | Not available. |
| Full classes, inheritance and distinct runtime struct identity | Not available. |
| Named struct construction | Not available. |
| Tuple destructuring | Not available. |
| List slicing and negative indexing | Not available. |
| General dictionary iteration in `for` | Not available. |
| List concatenation with `+` | Not available. |
| F-string format specifications | Not available. |
| Short-circuit `and` / `or` | Not available: both operands are evaluated. |
| Universal FFI/plugin ABI | Not available. |
| Dependency/package manager and package registry | Not available. |
| Full source-level debugger | Not available. |

Also keep runtime limitations in mind: `upper/lower` only handle ASCII; the PRNG is not cryptographically secure; `cwd` and the clock source are platform-dependent; strings allocated by current runtime paths are not individually freed before process exit.

See the [RBL 0.7.2 limitations reference](../../docs/eng/reference/limitations.md) for the full list.

## 19. Feature map: where to study additional topics

The twenty lessons are an introduction, not an exhaustive list of RBL features. The table below lists additional features implemented in RBL 0.7.2 and where to study them.

| Feature | Where to study next |
|---|---|
| Nested loops | Lesson 11; the Loops section. |
| `for (condition)` and `for (true)` | Lesson 12; the Condition loop section. |
| Nested lists | Lesson 13; the Lists section. |
| File operations | Lesson 14; Input/output and file operations, `rbl.io`. |
| Triple-quoted strings and escape sequences | Lesson 15; the Strings section. |
| Complex `break` / `continue` combinations in nested loops | Lesson 16; both statements affect the nearest loop. |
| Recursion | Lesson 17; Functions and recursion. |
| Nested dictionaries | Lesson 18; the Dictionaries section. |
| Logging | Lesson 19; `warn.log` and `error.log`. |
| Additional math functions, time and filesystem | Lesson 20; `rbl.math`, global math functions, `rbl.time` and `rbl.fs`. |
| Conditional expressions | After Lesson 3; Comparisons and logic. |
| `switch` with multiple values and ranges | After Lesson 3; Conditions and `switch`. |
| `..` and `..=` | After Lesson 4; Ranges. |
| Compound assignment and `++` / `--` | After Lesson 1; Variables. |
| Tuples and the empty tuple | After Lesson 7; Tuples. |
| Structs and receiver methods | After Lesson 8; Structs. |
| Identity `is` versus equality `==` | After Lesson 3; Comparisons and logic. |
| Numeric bases and scientific notation | After Lesson 1; Literals. |
| Math functions and constants | After Lesson 10; `rbl.math`. |
| Pseudorandom values | After Lesson 10; `rbl.random` and PRNG limitations. |
| Time and delays | After Lesson 10; `rbl.time`. |
| Path checks, deletion and current directory | After Lesson 10; `rbl.fs`. |
| `rbl.string` functions | After Lesson 9; Strings. |
| `int`, `float`, `str` conversions | After Lesson 2; Global built-ins. |
| NaN/Inf checks and `sign` | After Lesson 10; `rbl.math`. |
| `clamp`, `hypot`, `fmod`, `degrees`, `radians` | After Lesson 10; global built-ins (without the `rbl.math` prefix). |

## 20. Can a Python developer learn RBL independently?

Practical assessment:

| Background | Expected outcome |
|---|---|
| Has only seen Python and knows a few commands | Can read simple examples with guidance, but needs to learn programming fundamentals separately. |
| Has written small scripts using variables, `if`, loops and functions | Should be able to learn basic RBL from the twenty lessons and this guide. |
| Confidently uses Python lists, dictionaries, functions, files and debugging | Should be able to learn most of RBL 0.7.2 independently; the main work is adapting to syntax and runtime differences. |
| Experienced Python developer | Should be able to read the language quickly, but must not assume that RBL supports every familiar Python feature. |

### The most important differences from Python

| Python | RBL 0.7.2 |
|---|---|
| `x = 10` | `set x = 10` |
| `x = 20` to update | `let x = 20` |
| `def main():` | `func main() (` followed by `)` |
| Indentation defines blocks | Parentheses define blocks; indentation alone does not create a scope. |
| `for i in range(5):` | `for (i in 0..5) (` |
| `while condition:` | `for (condition) (` |
| `elif` | `elif` |
| `dict` | Similar dictionary, but no general `for key, value in dict.items()`. |
| `None` | `null` and a distinct `Unit` value. |
| Short-circuit `and` / `or` | `and` / `or` evaluate both operands. |
| `f"{x:.2f}"` | Basic f-string interpolation without format specifications. |
| `import module` | No user-defined source imports; built-in namespaces are directly available. |

**Conclusion:** someone with practical Python experience is a suitable audience for this guide. They should be able to understand RBL syntax and read the examples independently. To write non-trivial code, however, they must learn RBL's limitations rather than mechanically translating Python code.

## 21. Recommended self-study path

1. Complete Lessons 1–20 and run every file.
2. Rewrite at least one example from each lesson without copying the source.
3. Work through additional topics in the feature map.
4. Read the [complete syntax reference](../../docs/eng/reference/syntax.md).
5. Read the [standard-library reference](../../docs/eng/reference/standard-library.md).
6. Study the [language limitations](../../docs/eng/reference/limitations.md) so you do not waste time looking for unsupported constructs.
7. Explore the other [RBL examples](../README.md), especially the algorithms and simulations.
8. Use `check` for validation and `run` to execute your programs.

### Small exercises

1. Modify Lesson 1: declare three more variables of different types.
2. Add a `%` remainder calculation to Lesson 2; do not divide by zero.
3. Add a `case` branch to Lesson 3 and test several values.
4. Add a nested loop to Lesson 4 and calculate a sum.
5. Write a recursive function with a stopping condition in Lesson 5.
6. Create a list of lists in Lesson 6 and read one inner element.
7. Create a dictionary containing another dictionary in Lesson 7.
8. Add a field to the `struct` in Lesson 8 and update it with `let`.
9. Try a triple-quoted string and several escape sequences in Lesson 9.
10. Explore the standard library in Lesson 10, then complete Lessons 11–20, each of which focuses on a separate language feature.

## 22. Documentation links

- [RBL 0.7.2 syntax](../../docs/eng/reference/syntax.md)
- [RBL 0.7.2 standard library](../../docs/eng/reference/standard-library.md)
- [RBL 0.7.2 limitations](../../docs/eng/reference/limitations.md)
- [CLI reference](../../docs/eng/reference/cli.md)
- [Project workflow](../../docs/eng/how-to/project-workflow.md)
- [Project workflow](../../docs/eng/how-to/project-workflow.md)
- [Testing guide](../../docs/eng/how-to/testing.md)
- [All examples](../README.md)

\n
