# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Tutorial: your first RBL program

This guided path introduces RBL 0.7.2. It assumes that you already have a repository checkout and run commands from its root directory. The CLI is a script in the repository; it is not installed as a global command.

## 1. Check the toolchain

Linux:

```bash
python3 rbl.py doctor
python3 rbl.py version
```

Windows PowerShell:

```powershell
python rbl.py doctor
python rbl.py version
```

`doctor` reports whether the selected backend and required tools are available. Linux requires GNU assembler and linker; native Windows builds use MinGW-w64. Windows can also use WSL. See the [Windows and WSL guide](../how-to/windows-toolchain.md) for details.

## 2. Create a project

From the repository root:

```bash
python3 rbl.py new hello
cd hello
```

In PowerShell, use `python` instead of `python3`.

The command creates `rbl.toml`, `src/main.rbl`, and the `tests/` and `benchmarks/` directories. The CLI remains in the original repository, so from the new project directory invoke it as `../rbl.py` (or `..\rbl.py` in PowerShell).

## 3. Inspect the program

The generated `src/main.rbl` contains:

```rbl
func main() (
    print("Hello from RBL!")
)
```

Functions are declared with `func`. The parentheses after the name contain the parameters; another pair of parentheses delimits the function body. `print` writes its arguments separated by spaces, followed by a newline.

## 4. Add a calculation and a loop

Replace the contents of `src/main.rbl` with:

```rbl
func square(value int) int (
    return value * value
)

func main() (
    set total = 0

    for (i in 1..=5) (
        let total += square(i)
    )

    print("sum of squares:", total)
)
```

This example introduces several language constructs:

- `set` declares a new variable. Declaring the same name twice in the same function environment is an error.
- `let` updates an existing variable.
- The parameter `value int` declares an `int` parameter; `int` after the parameter list is the return-type annotation.
- The range `1..=5` includes its upper bound. `1..5` excludes it.
- `return` exits the function and supplies a value.

## 5. Check and run the program

From the `hello` directory:

```bash
python3 ../rbl.py check
python3 ../rbl.py run
```

`check` runs the compiler pipeline through assembly generation, but does not create a linked executable. `run` builds the program and executes it. In PowerShell, use `python ..\rbl.py check` and `python ..\rbl.py run`.

Expected output:

```text
sum of squares: 55
```

## 6. Where to go next

- [Syntax reference](../reference/syntax.md) — values, operators, functions, loops and containers.
- [Standard library](../reference/standard-library.md) — I/O, mathematics, strings, files, time and pseudorandom values.
- [CLI reference](../reference/cli.md) — all available commands.
- [Project workflow](../how-to/project-workflow.md) — building, formatting and working with the manifest.

## Tutorial scope

RBL 0.7.2 is a small compiled language that builds one source file per program. It does not yet support `import`, user-defined modules, a package manager, exceptions, closures or async/await. The [limitations reference](../reference/limitations.md) lists the complete set of omissions.
