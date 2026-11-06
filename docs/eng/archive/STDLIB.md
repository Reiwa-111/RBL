# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## RBL standard library — 0.7.1

The standard library is built into the compiler/runtime. Namespace names such as `rbl.math` are available without an `import` statement; source modules and package imports are not implemented.

## Global functions

| Function | Behavior |
|---|---|
| `print(...)` | Prints arguments separated by spaces and a newline; returns `Unit`. |
| `len(value)` | String UTF-8 code-point count; list/dictionary/tuple element count. |
| `input()` / `input(prompt)` | Reads one input line; the prompt form writes and flushes the prompt first. |
| `read_file(path)` / `write_file(path, content)` | Reads or writes text files. |
| `abs(x)`, `sqrt(x)` | Numeric absolute value and square root. |
| `min(x, ...)`, `max(x, ...)` | Require at least one numeric argument of a common type. |
| `int(x)`, `float(x)`, `str(x)` | Scalar conversions where supported. |
| `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp` | Math operations; trigonometric functions use radians. |
| `random_int(a, b)`, `random_float()`, `random_bool()` | Inclusive integer range `[a, b]`, a pseudorandom float in the range `[0, 1]`, and a pseudorandom boolean. |

The extended math operations `log10`, `log2`, `trunc`, `fmod`, `hypot`, `degrees`, `radians`, `clamp`, `sign`, `is_nan` and `is_inf` are also available as global aliases. Conversion behavior and invalid-input diagnostics are defined by the runtime implementation; do not assume arbitrary object-to-scalar conversion.

## Namespaces

### `rbl.io`

`input`, `prompt`, `read_file` and `write_file` are available through the I/O namespace. `rbl.io.prompt(text)` is the prompt form of input. Example:

```rbl
set text = rbl.io.read_file("notes.txt")
rbl.io.write_file("copy.txt", text)
```

### `rbl.math`

The namespace exposes scalar math operations including `abs`, `sqrt`, `pow`, `min`, `max`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`, `log10`, `log2`, `trunc`, `fmod`, `hypot`, `degrees`, `radians`, `clamp`, `sign`, `is_nan` and `is_inf`. The constants `rbl.math.pi` and `rbl.math.e` are values and are written without call parentheses.

`%` remains integer-only. `fmod` provides floating-point remainder using C `fmod` sign behavior. `clamp(x, low, high)` requires compatible numeric values. `sign(NaN)` currently returns zero.

### `rbl.string`

`len`, `upper`, `lower`, `trim`, `contains`, `starts_with` and `ends_with` are available. `upper` and `lower` currently perform ASCII case conversion; non-ASCII UTF-8 bytes are copied unchanged.

### `rbl.fs`

`exists`, `delete` and `cwd` provide basic filesystem operations. `delete` reports whether removal succeeded. `cwd` returns a host-specific path, so its exact output differs between operating systems.

### `rbl.time`

`now_ms` returns milliseconds, but its clock source differs by target: Linux uses `clock_gettime(CLOCK_REALTIME)` (wall-clock time), while Windows uses `GetTickCount64()` (monotonic milliseconds since boot). Do not compare the absolute values across targets or treat them as the same kind of clock. `sleep_ms` accepts a non-negative integer duration.

### `rbl.random`

`int`, `float` and `bool` provide pseudorandom values. Linux seeds and uses the C library `rand()` generator; Windows uses an internal glibc-range linear congruential generator because the Windows C library exposes a smaller `RAND_MAX`. Neither implementation is **cryptographically secure**, and random streams are not expected to match between targets.

## Logging methods

```rbl
warn.log("message")
error.log("message")
```

These print `[WARN]` and `[ERROR]` messages respectively. Unknown methods produce runtime errors after their arguments have been evaluated.

## Runtime implementation

The standard library is not implemented by one uniform backend. Linux scalar/runtime functions live primarily in `runtime/rbl_runtime.s`; Windows equivalents live in `runtime/rbl_runtime_win.c`; container operations and the container collector are shared in `runtime/rbl_containers.c`. All user programs still go through direct assembly code generation.
