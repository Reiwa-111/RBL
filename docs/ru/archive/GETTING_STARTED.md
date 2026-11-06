# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Getting started

This tutorial builds and runs an existing example, creates a project, and runs that project through the repository's CLI driver. Commands assume that you have a checkout of RBL. The CLI is a repository script, not a globally installed package command.

## Prerequisites

- Python 3.8 or newer.
- Tkinter only if you plan to use RBL Studio.
- Linux x86-64: GNU `as`, GNU `ld`, libc/libm, and a C compiler (`cc`) when the bundled compiler or shared container object must be rebuilt.
- Native Windows x86-64: MinGW-w64 with GNU `as` and GCC. Alternatively, configure WSL and select the WSL backend.

The repository includes prebuilt compiler and runtime objects. The driver may rebuild missing or stale artifacts from the checked-in sources, which is when the C compiler is required. See `WINDOWS.md` for native Windows and WSL selection details.

## Run an included example

Linux, from the repository root:

```bash
python3 rbl.py doctor
python3 rbl.py run examples/test.rbl
```

Windows PowerShell, from the repository root:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

`doctor` reports the selected toolchain and the tools available to the driver. `run` compiles, assembles, links and executes the selected source file.

## Create and run a project

The following Linux example creates `hello/` inside the RBL checkout. The final command deliberately invokes `../rbl.py`: the new project does not contain its own copy of the RBL driver.

```bash
python3 rbl.py new hello
cd hello
python3 ../rbl.py check
python3 ../rbl.py run
```

On Windows PowerShell, run the equivalent commands from the checkout root:

```powershell
python rbl.py new hello
Set-Location hello
python ..\rbl.py check
python ..\rbl.py run
```

A new project contains `rbl.toml`, `src/main.rbl`, `tests/`, `benchmarks/` and a starter README. The manifest currently supports `name`, `version` and `entry` under `[package]`. It does not resolve dependencies or source imports.

To work in a project outside the checkout, call the same `rbl.py` through its absolute path. The driver resolves the manifest and source relative to the current project directory.

## Run the checks

From the RBL repository root:

```bash
python3 tests/run_tests.py
python3 tests/test_stdlib.py
```

The first command runs the 59 direct regression cases. The second runs the separate standard-library and input smoke checks. On Windows, a true Linux-versus-native differential run requires both WSL and the native MinGW-w64 toolchain; see `TESTING.md`.

## Open RBL Studio

Linux:

```bash
bash RBLStudio.sh
```

Windows:

```powershell
.\RBLStudio.cmd
```

RBL Studio is optional. The CLI and compiler can be used without the GUI. See `IDE_MANUAL.md` for shortcuts and `GUI_TROUBLESHOOTING.md` for startup diagnostics.
