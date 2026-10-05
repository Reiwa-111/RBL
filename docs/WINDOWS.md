# Native Windows backend

RBL Studio builds and runs RBL programs natively on Windows (7 through 11) with
no WSL involved. WSL is still supported and is used automatically when no native
toolchain is present.

Both targets share the lexer, parser, static analysis, optimizer, register
allocator and x86-64 code generator in `compiler/rblc_asm.c`. Only the object
format, the process entry/exit glue and the runtime implementation differ:

```text
RBL source
   |
   +-- linux   : ASM (ELF) -> GNU as --64 -> GNU ld + libc/libm -> ELF
   |
   +-- windows : ASM (PE)  -> GNU as --64 -> gcc + runtime -> .exe
```

## Requirements

| Component | Requirement |
|---|---|
| RBL source compiler | built from `compiler/rblc_asm.c` on first use |
| Windows assembler | GNU `as.exe` (x86-64 PE/COFF) |
| Windows linker driver | GNU `gcc.exe` (MinGW-w64), also builds the runtime object |
| Python | 3.8+ (use 3.9 for the IDE on Windows 7) |

The recommended toolchain is [MSYS2](https://www.msys2.org/) with the mingw-w64
GCC package. Two environments are supported:

* `C:\msys64\ucrt64` — UCRT based. Works on Windows 10/11.
* `C:\msys64\mingw64` — `msvcrt.dll` based. Use this for **Windows 7/8.1**,
  because `ucrtbase.dll` is only part of Windows 10 and later.

Install with:

```bash
# from an MSYS2 shell
pacman -S --needed mingw-w64-ucrt-x86_64-gcc     # Windows 10/11
pacman -S --needed mingw-w64-x86_64-gcc          # Windows 7/8.1
```

RBL Studio looks for the toolchain in this order:

1. `RBL_MINGW` environment variable (a path to the toolchain `bin` directory),
2. `C:\msys64\{ucrt64,mingw64,clang64,mingw32}\bin`, `C:\msys2\...`,
   `C:\mingw64\bin`, `C:\MinGW\bin`, `C:\ProgramData\mingw64\...\bin`,
3. anything named `gcc` and `as` on `PATH`.

Nothing has to be prebuilt: if `bin/win-x86_64/rblc-asm.exe` or
`bin/win-x86_64/rbl_runtime_win.o` are missing or older than their sources, they
are rebuilt automatically on the next `check` / `build` / `run`.

## Using it

```powershell
python rbl.py doctor                 # shows the detected toolchain and the active backend
python rbl.py run examples\test.rbl  # compile, assemble, link, run
python rbl.py build examples\test.rbl
python rbl.py check examples\test.rbl
python rbl.py test                   # regression suite over the active backend
python rbl.py bench                  # build/run/net benchmark over the active backend
```

Generated files land in `build\<program>\`: `*.s`, `*.o`, `*.exe`.

The IDE (`RBLStudio.cmd`) uses the same driver, so `F5` runs natively on Windows
as well. `Help -> Toolchain Doctor` shows the same information as `rbl doctor`.

## Selecting a backend

The setting `wsl_mode` in `Settings` (or the `RBL_TOOLCHAIN` environment
variable, which wins) controls the choice:

| Mode | Meaning |
|---|---|
| `auto` (default) | native Windows toolchain if one is found, otherwise WSL |
| `native` | Windows toolchain only (`never` / `off` are accepted aliases) |
| `wsl` | WSL only (`always` is an accepted alias) |

The environment override makes it possible to run the suite against both
backends without touching settings:

```powershell
$env:RBL_TOOLCHAIN = 'wsl';    python tests\run_tests.py
$env:RBL_TOOLCHAIN = 'native'; python tests\run_tests.py
```

## How the two targets differ

* **Value ABI is shared.** Every RBL function, on both targets, uses the System V
  convention: arguments in `rdi, rsi, rdx, rcx, r8, r9`, a value returned as
  `rax = tag`, `rdx = payload`, plus the same 24-byte local slots and the same
  optimized `fast_fn_N` / register-held loop paths. That is why one code
  generator serves both.
* **Runtime.** Linux uses the hand-written assembly runtime
  (`runtime/rbl_runtime.s`). Windows uses `runtime/rbl_runtime_win.c`, compiled
  by MinGW-w64 GCC with `__attribute__((sysv_abi))`, so the generated machine
  code calls it with exactly the same registers. No C code is generated for user
  programs on either target; on Windows the C compiler acts only as the linker
  driver and as the builder of the runtime object.
* **Entry and exit.** Linux emits `_start`; Windows emits `main` with the
  Microsoft x64 convention (16-byte stack alignment, 32 bytes of shadow space,
  no return to the CRT) and terminates through `rbl_process_exit`.
* **Output.** The Windows runtime puts `stdout`/`stderr` in binary mode and
  writes `LF` only, and opens text files in binary mode, so program output is
  byte-identical to the Linux backend. That is what makes the differential
  runner meaningful.

## Verification

`tests/differential.py` compiles and runs the whole test corpus through both
backends and compares exit code, stdout and stderr byte for byte:

```powershell
python tests\differential.py --a wsl --b native
```

Every program is fed a few real input lines, so `input()` exercises the
line-reading path and not only the EOF path. Programs whose output is
intentionally platform dependent (the random API, `rbl.fs.cwd()`) are compared on
the exit code only, and the runner says so in its output.

The same regression suite runs unchanged on both backends:

```powershell
$env:RBL_TOOLCHAIN = 'native'; python tests\run_tests.py
$env:RBL_TOOLCHAIN = 'wsl';    python tests\run_tests.py
```

Measured on the development machine (Windows 11, MSYS2 ucrt64, gcc 16.2.0), the
native backend removes the bridge from every build step and from every program
run — see `benchmarks/run_benchmarks.py`, which reports the start-up baseline next
to the raw numbers. Absolute values are machine-specific; the useful comparison
is native versus WSL on the same host.

## Not available on Windows

* `benchmarks/compare_all.py` (the 3-way RBL / CPython / hand-written-ASM
  comparison) needs a Linux host: the baseline programs issue Linux syscalls
  directly and are linked without a C runtime. Run it inside WSL.
  `benchmarks/run_benchmarks.py` (`rbl bench`) works on both backends and
  reports a start-up baseline so the numbers stay interpretable.
* Source-level debugging, line tables in generated PE images and the native
  Windows PE emission are unchanged in scope: RBL emits assembly, the system
  assembler and linker do the rest.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `No native Windows GNU toolchain was found` | install MSYS2 mingw-w64, or set `RBL_MINGW` to the toolchain `bin` directory, or set `wsl_mode` to `wsl` |
| `gcc` exits with status 1 and prints nothing | `cc1.exe` lives outside `bin` and finds its DLLs through `PATH`; `tools/rbltool.py` prepends the toolchain `bin` directory for you, but a manual build needs it on `PATH` too |
| The `.exe` asks for a missing DLL | the driver links with `-static`; if you link manually, do the same |
| Windows 7 refuses to start the `.exe` | build with the `mingw64` (msvcrt) environment, not `ucrt64` |
| Antivirus blocks a freshly built `.exe` | that is normal for unsigned local builds; add `build\` to the exclusion list |
| Build fails after editing `runtime/rbl_runtime_win.c` | the object is rebuilt automatically; force it with `set RBL_REBUILD_COMPILER=1` |
