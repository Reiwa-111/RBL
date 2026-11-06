# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Windows x86-64 toolchain

RBL can produce native Windows x86-64 PE/COFF executables. In `auto` mode, the driver prefers a detected native MinGW-w64 toolchain; WSL is available as an alternative when configured.

## Toolchain requirements

- Python for the project driver and IDE.
- MinGW-w64 GCC and GNU `as` for native builds.
- The Windows compiler/runtime binaries may be built from the checked-in C sources when missing or stale.

MSYS2 is the recommended source for MinGW-w64 toolchains. The `ucrt64` environment targets current Windows releases; the `mingw64` (MSVCRT) environment is relevant for older Windows compatibility. Do not assume that every Windows release/toolchain combination has been tested merely because the driver can discover it.

## Toolchain discovery

The driver checks `RBL_MINGW`, common MSYS2/MinGW installation paths and then tools on `PATH`. The selected mode is controlled by the Studio `wsl_mode` setting or `RBL_TOOLCHAIN`:

| Value | Behavior |
|---|---|
| `auto` | Prefer native Windows tools; use WSL if native tools are unavailable and WSL is available. |
| `native` | Require native Windows tools. |
| `wsl` | Use the configured WSL distribution. |

`RBL_TOOLCHAIN` takes precedence over the saved setting. `RBL_MINGW` points to the toolchain `bin` directory. The default WSL distribution is `Ubuntu-24.04`.

## Build commands

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
python rbl.py build examples\test.rbl
python rbl.py check examples\test.rbl
python tests\run_tests.py
```

The IDE launcher uses the same toolchain driver, so `F5` follows the selected backend.

## Runtime differences

The Windows compiler emits PE-compatible assembly. The executable has Windows entry/exit glue, while RBL runtime calls retain the System V register convention. `runtime/rbl_runtime_win.c` uses `__attribute__((sysv_abi))`. `runtime/rbl_containers.c` is compiled as a separate object for Windows.

MinGW-w64 GCC is used to compile runtime objects and link the executable. RBL user code is still emitted as assembly; no C source is generated from the RBL program.

## Differential tests

On a Windows host with both WSL and native tools configured:

```powershell
python tests\differential.py --a wsl --b native
```

The runner compares executable status and, for deterministic cases, stdout/stderr byte-for-byte. Random output and the host-specific current working directory are compared by exit status only.

## Benchmark scope

`benchmarks/compare_all.py` is a Linux-host comparison because its hand-written assembly baselines use Linux syscalls. Run it from Linux or inside WSL. The portable `rbl bench` suite is available through either configured backend.

## Common issues

| Symptom | Check |
|---|---|
| Native GNU toolchain not found | Install MinGW-w64 or set `RBL_MINGW` to its `bin` directory. |
| WSL path conversion fails | Check that the configured distribution is installed and can run `wslpath`. |
| GCC exits without a useful message | Ensure the toolchain `bin` directory is on `PATH`; the driver configures this for its child processes. |
| Missing runtime DLL | Verify that the executable was linked with the expected MinGW runtime options. |
| Older Windows cannot run a UCRT executable | Build with an MSVCRT-based MinGW-w64 environment where required. |
