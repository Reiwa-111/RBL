# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## How-to: Windows, MinGW-w64 and WSL

RBL 0.7.2 can produce native x86-64 Windows executables (PE/COFF). On Windows, you can use a native MinGW-w64 toolchain or the WSL backend.

## Check toolchain availability

From PowerShell in the checkout root:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

MSYS2 is the recommended source of MinGW-w64. The driver checks `RBL_MINGW`, common MSYS2/MinGW installation directories, and tools available on `PATH`.

## Select a backend

The `RBL_TOOLCHAIN` environment variable takes precedence over the saved Studio setting:

```powershell
$env:RBL_TOOLCHAIN = "native"
python rbl.py doctor
```

Supported modes:

| Value | Behavior |
|---|---|
| `auto` | Prefer native MinGW-w64; use WSL if native tools are unavailable and WSL is configured. |
| `native` | Require the native Windows toolchain. |
| `wsl` | Use the configured WSL distribution. |

Set the MinGW-w64 `bin` directory through `RBL_MINGW`:

```powershell
$env:RBL_MINGW = "C:\msys64\ucrt64\bin"
$env:RBL_TOOLCHAIN = "native"
python rbl.py doctor
```

## Build through WSL

Make sure a WSL distribution is installed and provides Python, GNU `as`, GNU `ld`, and a C compiler when components need rebuilding. Select WSL with:

```powershell
$env:RBL_TOOLCHAIN = "wsl"
python rbl.py doctor
python rbl.py run examples\test.rbl
```

The default distribution is `Ubuntu-24.04`; the exact WSL configuration is controlled by RBL Studio/toolchain settings.

## Compare both backends

When both WSL and native MinGW-w64 are available:

```powershell
python tests\differential.py --a wsl --b native
```

The runner compares exit codes and, for deterministic cases, stdout/stderr byte-for-byte. Random output and the current working directory are platform-dependent and are not compared as fixed strings.

## Common issues

| Symptom | What to check |
|---|---|
| Native toolchain not found | Is MinGW-w64 installed? Does `RBL_MINGW` point to its `bin` directory? Are `gcc` and GNU `as` available? |
| WSL or path conversion fails | Is the selected distribution available? Does `wslpath` work? Is the project drive mounted? |
| The wrong backend is selected | Check `RBL_TOOLCHAIN` and the saved `wsl_mode` setting. |
| Compiler/runtime object is missing | Run `python rbl.py doctor`; if needed, run `python rbl.py bootstrap`. |
| IDE startup fails | First run `python tools/ide_doctor.py` and `python tools/ide_smoke.py`; inspect `ide_startup.log`. |

## Limitations

The fact that a MinGW path is detected does not mean every combination of Windows version, CRT and compiler version has been tested. The benchmark with handwritten Linux syscall programs cannot run as a native Windows benchmark; use WSL or Linux. See the [benchmarking guide](benchmarking.md).
