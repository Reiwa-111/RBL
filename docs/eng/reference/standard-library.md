# last verified: v0.8.0 | 2026.11.09 | LANGUAGE CONTRACT RECHECK

## Reference: RBL 0.8.0 standard library

Built-in functions are implemented by the compiler/runtime. Namespace names can be used directly; no `import` statement is needed. This is not a user module system: source imports and third-party package loading are not available.

## Global functions

| Function | Purpose and contract |
|---|---|
| `print(value, ...)` | Prints arguments separated by one space, followed by a newline; returns `Unit`. |
| `len(value)` | UTF-8 code-point count for strings; element count for lists, dictionaries and tuples. |
| `input()` | Reads one line from standard input. |
| `input(prompt)` | Prints and flushes the prompt, then reads one line. |
| `read_file(path)` | Reads a text file. |
| `write_file(path, content)` | Writes text to a file. |
| `abs(x)`, `sqrt(x)` | Absolute value and square root. |
| `min(x, ...)`, `max(x, ...)` | Minimum/maximum of one or more numeric arguments of a common type. |
| `int(x)`, `float(x)`, `str(x)` | Supported runtime conversions of scalar values. |
| `pow`, `floor`, `ceil`, `round` | Power and rounding operations. |
| `sin`, `cos`, `tan`, `log`, `exp` | Math operations; trigonometric functions take radians. |
| `random_int(a, b)` | Pseudorandom integer in the inclusive interval `[a, b]`. |
| `random_float()` | Pseudorandom float in `[0, 1]`. |
| `random_bool()` | Pseudorandom Boolean value. |

Strings returned by `input()`, `read_file()` and conversions are Runtime-managed RBL values. RBL code must not free their payloads manually; their lifetime follows the [dynamic-string ownership model](../explanation/execution-model.md#dynamic-string-lifetime).

Global aliases also exist for `log10`, `log2`, `trunc`, `fmod`, `hypot`, `degrees`, `radians`, `clamp`, `sign`, `is_nan` and `is_inf`.

Conversions do not imply that every arbitrary container/object can be converted to every scalar type. Unsupported value types produce a runtime diagnostic.

## Namespace `rbl.io`

| Function | Purpose |
|---|---|
| `rbl.io.input()` | Reads a line from stdin. |
| `rbl.io.prompt(text)` | Prints a prompt and reads a line. |
| `rbl.io.read_file(path)` | Reads a text file. |
| `rbl.io.write_file(path, content)` | Writes a text file. |

Example:

```rbl
set text = rbl.io.read_file("notes.txt")
rbl.io.write_file("copy.txt", text)
```

## Namespace `rbl.math`

Implemented namespace methods: `abs`, `sqrt`, `pow`, `min`, `max`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`, `log10`, `log2`, `trunc`, `sign`, `is_nan` and `is_inf`.

The constants `rbl.math.pi` and `rbl.math.e` are values, not functions; do not add call parentheses.

Details:

- The global `fmod(a, b)` function performs floating-point remainder using C `fmod` behavior: the result has the sign of the dividend.
- The `%` operator is integer-only.
- The global `clamp(x, low, high)` function requires compatible numeric types; `rbl.math.clamp` is not implemented in 0.8.0.
- `hypot`, `degrees` and `radians` are available as global functions, but not as `rbl.math` methods.
- `sign(NaN)` returns `0` in the current implementation.
- `is_nan` and `is_inf` accept numeric values; integers are neither NaN nor infinity.

## Namespace `rbl.string`

| Function | Purpose |
|---|---|
| `rbl.string.len(text)` | String length in UTF-8 code points. |
| `rbl.string.upper(text)` | Converts ASCII letters to uppercase. |
| `rbl.string.lower(text)` | Converts ASCII letters to lowercase. |
| `rbl.string.trim(text)` | Removes spaces, tabs, LF and CR from both ends. |
| `rbl.string.contains(text, part)` | Checks for a substring. |
| `rbl.string.starts_with(text, prefix)` | Checks a prefix. |
| `rbl.string.ends_with(text, suffix)` | Checks a suffix. |

`upper` and `lower` do not perform Unicode case folding; UTF-8 bytes outside ASCII are copied unchanged.

## Namespace `rbl.fs`

| Function | Purpose |
|---|---|
| `rbl.fs.exists(path)` | Checks whether a path exists. |
| `rbl.fs.delete(path)` | Removes a file/path and returns whether removal succeeded. |
| `rbl.fs.cwd()` | Returns the current working directory. |

The path returned by `cwd()` depends on the host OS and must not be compared as an identical string between Windows and Linux.

## Namespace `rbl.time`

| Function | Purpose |
|---|---|
| `rbl.time.now_ms()` | Returns a millisecond value from the platform clock. |
| `rbl.time.sleep_ms(duration)` | Pauses execution for a non-negative number of milliseconds. |

The source of `now_ms()` differs by platform: Linux uses `clock_gettime(CLOCK_REALTIME)` (wall-clock time), while Windows uses `GetTickCount64()` (monotonic milliseconds since boot). Absolute results and clock semantics are therefore not comparable between platforms. For duration measurements, use an external monotonic benchmark timer.

## Namespace `rbl.random`

| Function | Purpose |
|---|---|
| `rbl.random.int(min, max)` | Pseudorandom integer from an inclusive interval. |
| `rbl.random.float()` | Pseudorandom float in `[0, 1]`. |
| `rbl.random.bool()` | Pseudorandom Boolean. |

Global aliases `random_int`, `random_float` and `random_bool` are also available. The generator is not cryptographically secure. Linux uses the C library `rand()`; Windows uses an internal linear congruential generator with a 31-bit output range. Sequences are not expected to match across platforms and must not be used for cryptography, tokens or secrets.

## Logging methods

```rbl
warn.log("message")
error.log("message")
```

These print messages with `[WARN]` and `[ERROR]` prefixes. An unknown method raises a runtime error after evaluating its arguments.

## Platform implementation

| Component | Source / purpose |
|---|---|
| Linux scalar runtime | `runtime/rbl_runtime.s` |
| Windows scalar runtime | `runtime/rbl_runtime_win.c` |
| Lists, dictionaries, tuples and collector | `runtime/rbl_containers.c` |

A shared language API does not imply that every system-observable value is identical across platforms: `cwd`, the clock source and pseudorandom sequences are intentionally platform-specific.
