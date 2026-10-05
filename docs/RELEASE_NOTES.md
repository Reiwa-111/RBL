# RBL Studio 0.7.0

## Native Windows backend

- The compiler gained `--target win`: identical frontend, optimizer and code generator, PE/COFF output, Microsoft x64 entry glue (`main` instead of `_start`), `.rdata` for read-only data, no ELF-only directives.
- `--check` now validates the whole pipeline (front end *and* back end) instead of stopping after parsing, so it no longer answers `OK` for programs that fail during code generation — for example `break`/`continue` outside a loop. It still writes no output file.
- New runtime `runtime/rbl_runtime_win.c`, compiled by MinGW-w64 GCC with `__attribute__((sysv_abi))` so the generated code calls it with the same registers as on Linux. No C is generated for user programs; GCC acts as the linker driver and builds the runtime object.
- `tools/rbltool.py` discovers a native MinGW-w64 toolchain (MSYS2 `ucrt64`/`mingw64`, `RBL_MINGW`, or `PATH`) and prefers it over WSL. `rbl doctor` reports the active backend. Selection: `wsl_mode` setting or the `RBL_TOOLCHAIN` environment variable (`auto` / `native` / `wsl`).
- Windows 7/8.1 should use the msvcrt-based `mingw64` environment; Windows 10/11 can use `ucrt64`. See `docs/WINDOWS.md`.
- Output is byte-identical to the Linux backend (binary-mode stdio, LF only).

## Audit fixes

- Optimized loops no longer lose label-id uniqueness: the parent label counter is synced after the loop body, which used to make valid programs fail with `symbol '.L_if_next_N' is already defined`.
- The fast-int analysis now terminates on (mutually) recursive single-return `int` functions; it used to overflow the compiler's stack and crash with SIGSEGV.
- The optimized loop now checks the induction-variable increment exactly like the tagged loop, so an inclusive range ending at `INT64_MAX` reports `integer overflow` instead of looping forever. The check is only emitted when it is reachable, so ordinary loops stay unchecked.
- Generated code no longer references runtime-internal labels: `range is only valid in for` is now emitted from the compiler's own string pool. Before this, every program with a range outside a `for` header failed to link with `undefined reference to '.Lmsg_range_value'`.
- Label ids are now allocated from a single translation-unit-wide sequence instead of per-function bases, so a label can never be reused inside a loop body or between a function and its fast variant.
- `tests/test_stdlib.py` no longer launches a Linux ELF directly on Windows.
- `rbl test` / `rbl bench` work on both backends; every program runs with a timeout; `benchmarks/compare_all.py` refuses to run on Windows with an explanation (it needs Linux syscall baselines).
- `examples/mega_test.rbl` was converted to the canonical parenthesized syntax; it no longer compiled after v0.7 made parentheses canonical.

## Runtime parity fixes (found by the differential runner)

- `rbl_neg` and `rbl_not` were the only runtime entry points that took their operand in `rax`/`rdx`; every other function takes `(tag in rdi, payload in rsi)`. The compiler now passes the operand normally and the Linux runtime reads it from `rdi`/`rsi`, so both runtimes use one convention. Before this, `-float` and `not bool` failed on the Windows runtime with `invalid operand for -`.
- The Windows runtime now truncates a read input line at the first LF or CR, exactly like `rbl_trim_newline` in the Linux runtime. Before this, `input()` appended a spurious blank line to `print(input(...))` output.
- `tools/rbltool.py` prepends the toolchain `bin` directory to `PATH` for every native toolchain child. `cc1.exe` lives under `lib/gcc/...` and resolves its DLLs through `PATH`, so without this the C compiler exited with status 1 and no diagnostic whenever MSYS2 was not already on `PATH`.
- Console output is encoding-safe on Windows: the tool sets the console to UTF-8 and reconfigures its streams with `errors="replace"`. Previously any non-ASCII path or program output raised `UnicodeEncodeError` and killed `rbl check` / `rbl run` under a legacy code page (cp866/cp1251).

## Testing

- 53-case regression suite, including six new regressions for the defects above, three feature tests for the integer remainder operator, and tests for the 0.8 numeric literal forms, `#` comments, string escapes, multi-line strings, interpolated strings, compound assignment, `break`/`continue`, the unified `for (cond)` loop, the `a if (cond) else b` conditional expression, `switch`, lists and the complete 0.8 math addition.
- `null` and `is` (phase 2.1): `null` is its own value (tag 8) with its own printing, so `print(null)` shows `null` while a function that returns nothing still prints an empty string — the two are no longer the same value. It works as a dictionary value, a list element, and compares with `==`/`!=`. `is` is identity (same tag and payload, so the same container), which `==` deliberately is not: `[1, 2] == [1, 2]` is true while two separate lists are not `is` each other.
- Memory (phase 2.3): a conservative mark-and-sweep collector for containers, in the shared C runtime. Roots are the machine stack — any word that equals a live container pointer — plus everything reachable from it, so temporaries held only in registers or on the stack are safe and the code generator needs no ownership analysis. Collection triggers on an allocation threshold that adapts to the live set. Verified by a 1M-allocation loop that must finish under `ulimit -v 122880` (about 320 MB of garbage); the same program without a collector cannot.
- Five-way benchmark (`benchmarks/compare_all.py`, medians on this host, 3 runs, process start included): `sum` 100M additions — RBL 76.9 ms, C -O2 2.29 ms, C++ -O2 2.74 ms, hand-ASM 23.2 ms, CPython 6337 ms; `calls` 10M calls — 19.3 / 2.28 / 2.27 / 12.2 / 672 ms; `branches` 100M iterations — 94.2 / 33.7 / 33.3 / 43.7 / 5051 ms. So RBL sits 1.6–3.3× from hand-written assembly and 35–82× ahead of CPython, while C/C++ keep a large lead.
- Tuples and structs (phase 2.5): `(1, 2, 3)` and `()` with indexing, `len`, iteration, printing `(1, (2, 3))` and deep equality (tuples are immutable: assigning to an element is an error). `struct Name (field type …)` declares a record whose instances are dictionaries keyed by field name, so `Point(3, 4)`, `p.x`, `let p.x = 30`, `"x" in p` and `p == Point(3, 4)` all reuse the container runtime. Receiver methods `func (p Point) move(dx int) ( … )` are called as `p.move(dx)`, which resolves to `move(p, dx)` — the receiver is passed by reference, so the caller sees field changes.
- Dictionaries (phase 2.4): `{k: v}` literals with `int`/`float`/`string` keys, `d[k]`, `d[k] = v` / `let d[k] = v`, `len(d)`, `k in d` (membership for dict keys, list values and substrings), deep printing and order-insensitive deep equality. Indexing is dispatched at runtime (`rbl_index_get`/`rbl_index_set`), so the compiler needs no type knowledge for `a[i]`.
- Lists (phase 2): `[1, 2]`, `a[i]`, `a[i] = v` / `let a[i] = v`, `len(a)`, `for (x in list)`, deep printing (`[[1, 2], [3]]`) and deep equality. The container runtime is written once in `runtime/rbl_containers.c` and linked into both the ELF and the PE build.
- New `tests/differential.py`: compiles and runs the whole corpus through both backends and compares exit code, stdout and stderr byte for byte.
- Benchmark suite now reports `build`, `run` and `net = run - baseline`, where the baseline is an empty program, so small workloads are not confused with process/WSL start-up cost.

## Standard library expansion

- Added global `len`, `input`, `read_file`, `write_file`, `abs`, `sqrt`, `min`, `max`, `int`, `float`, `str`.
- Added global math aliases: `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`.
- Added global random aliases: `random_int`, `random_float`, `random_bool`.
- Added namespaces `rbl.io`, `rbl.math`, `rbl.string`, `rbl.fs`, `rbl.time`, `rbl.random`.
- Removed accidental `rbl.randome` alias; `rbl.random` is the only supported random namespace.
- Standard library is implemented in the hand-written assembly runtime; user programs still never pass through C.

## Syntax

- Parenthesized blocks are canonical: `func ... ( ... )`, `if (condition) ( ... )`, `for (header) ( ... )`.
- Formatter, IDE indentation and examples emit parentheses.
- Legacy `{ ... }` blocks are accepted only for source compatibility.
