# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: project workflow

## Create a new project

From the repository checkout root:

```bash
python3 rbl.py new my_app
```

Choose another destination:

```bash
python3 rbl.py new my_app --path ../my_app
```

The target directory must be missing or empty. A new project contains:

```text
my_app/
├── rbl.toml
├── README.md
├── src/
│   └── main.rbl
├── tests/
└── benchmarks/
```

## Initialize an existing directory

```bash
python3 /path/to/RBLStudio/rbl.py init
```

This creates `rbl.toml`, `src/`, `tests/`, `benchmarks/` and a starter `src/main.rbl` when they do not already exist. An existing manifest is never overwritten.

## Build and run

Run commands from the project directory and refer to `rbl.py` in the RBL checkout:

```bash
python3 /path/to/RBLStudio/rbl.py check
python3 /path/to/RBLStudio/rbl.py build
python3 /path/to/RBLStudio/rbl.py run
python3 /path/to/RBLStudio/rbl.py asm
```

You can pass a source path explicitly after the command. Without one, the CLI uses `package.entry` from `rbl.toml`, defaulting to `src/main.rbl`. If that file does not exist, the driver also checks for `main.rbl` in the project root.

## Format a source file

```bash
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl
python3 /path/to/RBLStudio/rbl.py fmt src/main.rbl --check
```

The formatter normalizes block indentation based on parentheses at the beginning/end of lines. It is not a configurable full-language formatter and does not enforce every style convention.

## Generate a simple function listing

```bash
python3 /path/to/RBLStudio/rbl.py doc src/main.rbl -o target/api.md
```

`rbl doc` is a simple function-declaration listing that extracts names, parameters and return annotations using a text pattern. It is not a full documentation generator, type analyzer or standard-library documentation generator.

## Clean build outputs

```bash
python3 /path/to/RBLStudio/rbl.py clean
```

This removes `build/` from the current project directory. Do not run it from a directory whose `build/` contents you need to keep.

## Inspect the environment

```bash
python3 /path/to/RBLStudio/rbl.py doctor
python3 /path/to/RBLStudio/rbl.py env
```

`doctor` checks toolchain availability. `env` prints the RBL installation root, current project directory and Python executable.

## What does `rbl test` do?

`rbl test` runs the regression suite and separate standard-library checks from the RBL checkout. It is not a universal test runner for arbitrary projects and does not automatically execute files under `my_app/tests/`.
