# Direct ASM backend

## Target

- x86-64
- ELF64
- Linux
- System V AMD64 ABI
- Intel syntax accepted by GNU `as`

## RBL value ABI

Every RBL expression result is returned as:

```text
rax = type tag
rdx = payload
```

Tags:

```text
0 = Unit
1 = Int(i64)
2 = Float(f64 bits)
3 = String(char*)
4 = Bool
```

Function calls use:

```text
rdi = Value* args
rsi = argc
```

and return the same `rax/rdx` pair.

## Local slots

Every compiled function receives numbered slots. Each slot is 24 bytes:

```text
+0   bound flag
+8   value tag
+16  value payload
```

This replaces the old `HashMap<String, Value>` hot path with compile-time-resolved stack offsets.

## Evaluation order

The compiler emits nested evaluation explicitly. For a binary operation:

```text
emit(left)
spill rax/rdx
emit(right)
restore left + move right into ABI registers
call runtime helper
```

This preserves left-to-right evaluation even for non-short-circuit `and`/`or`.

## Function semantics

Function name resolution follows the old `HashMap` behavior: the last duplicate declaration wins. Arity is checked before argument expressions are evaluated. Parameter type checks happen in the callee after arguments have already been evaluated.

## Scalar fast path

The backend can prove some regions are statically scalar and bypass the tagged `rax/rdx` ABI:

```text
RBL int expression → raw i64 in rax
RBL bool expression → 0/1 in rax
int-only function → fast_fn_N with System V integer arguments
int range loop → raw counter/bound instead of rbl_expect_int + rbl_inc_i64 per iteration
```

Overflow checks remain explicit and route to the assembly runtime's error helpers. If the compiler cannot prove a region safe, it falls back to the normal tagged-value path.

## Runtime

`runtime/rbl_runtime.s` contains arithmetic, comparisons, type checks, printing, string concatenation, loop overflow checks, diagnostics and process exit. It calls libc only for basic I/O/memory primitives (`printf`, `fprintf`, `malloc`, `strlen`, `memcpy`, `strcmp`, `exit`).

No generated RBL program contains C source.

## Build

```bash
python3 tools/rbltool.py build examples/test.rbl
python3 tools/rbltool.py run examples/test.rbl
python3 tools/rbltool.py asm examples/test.rbl
```

Equivalent low-level sequence (the runtime object that ships in the archive is `bin/linux-x86_64/rbl_runtime.o`; `runtime/rbl_runtime.o` is a stale pre-stdlib leftover and is not link-compatible with current output):

```bash
bin/linux-x86_64/rblc-asm examples/test.rbl -S build/test/test.s
bin/linux-x86_64/rblc-asm examples/test.rbl --check

`--check` runs the **whole** pipeline — front end *and* back end — and writes no file, so it also reports diagnostics that only appear while generating code (for example `break`/`continue` outside a loop, which the parser alone cannot see). Exit status is `0` for a valid program and `1` for any diagnostic; on success it prints `OK: <file>`. The generated assembly goes to a throwaway stream (`tmpfile()`), so nothing is left behind on either target.
as --64 build/test/test.s -o build/test/test.o
ld build/test/test.o bin/linux-x86_64/rbl_runtime.o \\
  -o build/test/test \\
  -dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc -lm
```

## Windows target

The same compiler emits PE-compatible assembly with `--target win`. The RBL value ABI, the local slots and every generated function stay System V; only the entry/exit glue changes (`main` with the Microsoft x64 convention instead of `_start`), the ELF-only directives (`.type`, `.note.GNU-stack`) are omitted, and read-only data goes to `.rdata,"dr"`.

The Windows runtime is `runtime/rbl_runtime_win.c`, compiled by MinGW-w64 GCC with `__attribute__((sysv_abi))` so that the generated instructions call it through the same registers. The C compiler is used as the linker driver, not as a code generator for RBL programs:

```powershell
bin\\win-x86_64\\rblc-asm.exe examples\\test.rbl -S build\\test\\test.s --target win
as --64 build\\test\\test.s -o build\\test\\test.o
gcc build\\test\\test.o bin\\win-x86_64\\rbl_runtime_win.o -o build\\test\\test.exe -static -lm
```
