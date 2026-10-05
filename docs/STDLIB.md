# RBL 0.7 standard library

RBL 0.7 ships a small built-in standard library implemented in the native x86-64 runtime. There is no `import` statement yet: namespaces such as `rbl.math` are available directly.

## Global functions

| Function | Result | Notes |
|---|---|---|
| `print(...)` | `Unit` | Prints arguments left-to-right, separated by spaces. |
| `len(s)` | `int` | UTF-8 code-point count for strings. |
| `input()` | `string` | Reads one line from stdin. |
| `input(prompt)` | `string` | Prints a string prompt, flushes stdout, then reads one line. |
| `read_file(path)` | `string` | Reads a text file. |
| `write_file(path, content)` | `Unit` | Writes/replaces a text file. |
| `abs(x)` | numeric | Integer or float absolute value. |
| `sqrt(x)` | `float` | Square root of a non-negative integer/float. |
| `min(...)` | numeric | At least one homogeneous numeric argument. |
| `max(...)` | numeric | At least one homogeneous numeric argument. |
| `int(x)` | `int` | Converts int/bool/float/string when valid. |
| `float(x)` | `float` | Converts int/bool/float/string when valid. |
| `str(x)` | `string` | Converts scalar values to text. |
| `pow(a,b)` | `float` | Numeric exponentiation. |
| `floor(x)` | `float` | Floor using libm. |
| `ceil(x)` | `float` | Ceiling using libm. |
| `round(x)` | `float` | C/libm rounding semantics. |
| `sin(x)` | `float` | Sine in radians. |
| `cos(x)` | `float` | Cosine in radians. |
| `tan(x)` | `float` | Tangent in radians. |
| `log(x)` | `float` | Natural logarithm. |
| `exp(x)` | `float` | Natural exponential. |
| `random_int(a,b)` | `int` | Pseudorandom integer in `[a,b]`. |
| `random_float()` | `float` | Pseudorandom value approximately in `[0,1]`. |
| `random_bool()` | `bool` | Pseudorandom boolean. |

## `rbl.io`

```rbl
set name = rbl.io.input("Name: ")
set text = rbl.io.read_file("notes.txt")
rbl.io.write_file("copy.txt", text)
```

Aliases `input`, `read_file` and `write_file` remain available globally.

## `rbl.math`

```rbl
print(rbl.math.sqrt(81))
print(rbl.math.pow(2, 8))
print(rbl.math.floor(3.9))
print(rbl.math.ceil(3.1))
print(rbl.math.round(3.6))
print(rbl.math.sin(0.0))
print(rbl.math.cos(0.0))
print(rbl.math.min(8, 2, 4))
print(rbl.math.max(8, 2, 4))
```

Also available: `abs`, `tan`, `log`, `exp`.

### Additions in the 0.8 math set

| Function | Result | Notes |
|---|---|---|
| `log10(x)` | `float` | base-10 logarithm |
| `log2(x)` | `float` | base-2 logarithm |
| `trunc(x)` | `float` | drop the fractional part (towards zero) |
| `fmod(a, b)` | `float` | C `fmod`: the result takes the sign of the dividend. This is the only float remainder, because `%` is integer-only |
| `hypot(a, b)` | `float` | `sqrt(a*a + b*b)` without intermediate overflow |
| `degrees(x)` | `float` | radians → degrees (rewritten to `float(x) * 57.29577951308232`) |
| `radians(x)` | `float` | degrees → radians (rewritten to `float(x) * 0.017453292519943295`) |
| `clamp(x, lo, hi)` | numeric | rewritten to `min(max(x, lo), hi)`, so all three arguments must be of one numeric type and each is evaluated once |
| `sign(x)` | `int` | `-1`, `0` or `1`; NaN counts as `0` |
| `is_nan(x)` | `bool` | true for NaN (integer arguments are finite, so false) |
| `is_inf(x)` | `bool` | true for ±infinity |
| `rbl.math.pi` | `float` | 3.141592653589793 — a qualified name **without** parentheses is a value |
| `rbl.math.e` | `float` | 2.718281828459045 |

All of them are available both as globals (`log10(1000.0)`) and through the namespace (`rbl.math.log10(1000.0)`); arguments may be `int` or `float` and are converted to `double`, like the other math functions.

Note: `%` remains integer-only by language decision — `-10 % 3` is `-1` (C/Rust sign rule), and `float % int` is a runtime error; use `fmod` for floats.

## `rbl.string`

```rbl
print(rbl.string.len("hello"))
print(rbl.string.upper("hello"))
print(rbl.string.lower("RBL"))
print(rbl.string.trim("   text   "))
print(rbl.string.contains("hello world", "world"))
print(rbl.string.starts_with("hello", "he"))
print(rbl.string.ends_with("hello", "lo"))
```

`upper/lower` currently perform ASCII case conversion; non-ASCII UTF-8 bytes are copied unchanged.

## `rbl.fs`

```rbl
print(rbl.fs.exists("file.txt"))
print(rbl.fs.cwd())
print(rbl.fs.delete("file.txt"))
```

`delete` returns `true` when the file removal succeeds.

## `rbl.time`

```rbl
set started = rbl.time.now_ms()
rbl.time.sleep_ms(10)
print(rbl.time.now_ms() - started)
```

`now_ms()` uses the system realtime clock and returns integer milliseconds.

## `rbl.random`

`rbl.random` is the standard namespace for pseudorandom helpers.

```rbl
print(rbl.random.int(1, 100))
print(rbl.random.float())
print(rbl.random.bool())

```

The current implementation is a lightweight libc `rand()` based generator, not a cryptographically secure RNG.

## Logging

Existing logging remains unchanged:

```rbl
warn.log("warning")
error.log("error")
```

## Runtime model

All of these functions are linked from the hand-written assembly runtime. They do not introduce a C-code generation stage. The user-program pipeline remains:

```text
RBL -> direct x86-64 ASM -> GNU as -> GNU ld + libc/libm -> ELF
```
