# RBL CLI tooling

RBL 0.7 adds a single-project command interface inspired by the workflow ideas people commonly use in Python, C/C++, and Rust ecosystems, while keeping RBL's own direct-ASM build pipeline.

## Commands

```text
rbl new NAME [--path DIR]   create a project
rbl init                    initialize the current directory
rbl check [FILE]            parse/type-check without linking
rbl build [FILE]            build native executable
rbl run [FILE]              build and run
rbl asm [FILE]              emit x86-64 assembly
rbl test                    run the semantic regression suite
rbl bench [--reps N]        run the 3-way benchmark
rbl fmt [FILE]              format source
rbl fmt [FILE] --check      verify formatting without changing files
rbl doc [FILE] -o FILE     generate a small API document
rbl clean                   remove the build directory
rbl doctor                  diagnose the toolchain
rbl env                     print RBL environment information
rbl version                 print compiler/tool version
```

The default project manifest is `rbl.toml`:

```toml
[package]
name = "hello"
version = "0.1.0"
entry = "src/main.rbl"
```

`rbl new hello` creates that layout plus a starter `src/main.rbl`.

## Pipeline

No C source is generated for user programs, and no C/C++ compiler takes part in code generation. Two targets share the same frontend, optimizer and code generator:

```text
RBL
 -> lexer/parser/AST
 -> static analysis
 -> loop optimization + local register allocation
 -> direct x86-64 ASM
    +-- linux   : GNU as -> GNU ld + libc/libm -> ELF executable
    +-- windows : GNU as -> MinGW-w64 gcc (linker driver) + rbl_runtime_win.o -> .exe
```

On Windows the C compiler is used for two things only: building the runtime object
`runtime/rbl_runtime_win.c` (C with `__attribute__((sysv_abi))`) and acting as the
linker driver that pulls in the C runtime. The generated instructions for the user
program are still pure assembly. See `docs/WINDOWS.md`.

A host C compiler is only a bootstrap dependency when rebuilding `rblc-asm` itself from `compiler/rblc_asm.c`.

## IDE launcher diagnostics

`tools/ide_doctor.py` checks Python, Tkinter, WSL/display state, `as`, `ld`, the bundled compiler and the IDE import path.

Linux launcher selection is robust to an inactive virtual environment: `RBLStudio.sh` prefers `.venv/bin/python3`, then falls back to `python3`.

Startup exceptions are written to `ide_startup.log` in the project directory.

### Windows bootstrap policy
Normal Windows use never requires rebuilding `rblc-asm` from `compiler/rblc_asm.c` when the bundled compiler exists. Use `rbl bootstrap` for an explicit rebuild.
