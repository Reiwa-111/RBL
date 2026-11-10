# last verified: v0.8.0 | 2026.11.10 | BUNDLED WINDOWS TOOLCHAIN

## How-to: Windows, MinGW-w64 and WSL

RBL 0.8.0 can produce native x86-64 Windows executables (PE/COFF). On Windows, you can use a native MinGW-w64 toolchain or the WSL backend.

## Check toolchain availability

From PowerShell in the checkout root:

```powershell
python rbl.py doctor
python rbl.py run examples\test.rbl
```

The portable Windows bundle first looks for its minimal private toolchain at `toolchain/windows-x86_64/msys64/ucrt64/`. This is not a full MSYS2 installation: it contains only the GCC driver, GNU assembler, linker, required startup/static libraries and recursively verified DLLs. If the bundle is absent, the driver checks `RBL_MINGW`, common MSYS2/MinGW installation directories, and tools available on `PATH`.

RBL's normal Windows user-program path does not need a C compiler frontend: release bundles contain the RBL compiler and prebuilt Windows Runtime objects. The native build path assembles generated `.s` files with GNU `as` and links the resulting object plus Runtime objects through the GCC driver. RBL disables the GCC linker plugin (`-fno-use-linker-plugin`): RBL does not produce LTO objects, so the LTO plugin and `lto-wrapper` are not shipped. A full C toolchain (including `cc1.exe` and headers) is needed only by developers rebuilding the bootstrap compiler/Runtime (`python rbl.py bootstrap`). Normal commands do not rebuild shipped binaries based on file timestamps; if a required artifact is missing, a developer toolchain is required.

For a developer rebuild, explicitly select a full external MinGW-w64 installation so it takes precedence over the bundled runtime toolchain:

```powershell
$env:RBL_MINGW = "C:\msys64\ucrt64\bin"
python rbl.py bootstrap
Remove-Item Env:RBL_MINGW
```

Build or verify the bundle from an installed MSYS2 UCRT64 root:

```powershell
python tools\package_windows_toolchain.py --source C:\msys64\ucrt64
python tools\package_windows_toolchain.py --check
```

The script writes a SHA-256 manifest and fails if a linker input or non-system DLL dependency is unresolved. The tested UCRT64 bundle is about 21 MiB; its exact size is recorded in the manifest.

The bundle is not relicensed under Apache-2.0. Review the [third-party notices](../../../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md) for exact package versions and license metadata, the [corresponding-source inventory](../../../toolchain/windows-x86_64/SOURCE_CODE.md), and the [source archive audit](../../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md). Twenty exact-version MSYS2 source archives (9 toolchain and 11 wx/runtime dependencies) have been downloaded and signature-verified; upstream hashes, the CRT/winpthreads git commit and the default-manifest source hash were checked. The archives remain in the local build audit directory and are not part of the runtime bundle or Git snapshot. A separate source-material bundle must accompany a binary distribution, or an applicable written offer must be legally reviewed. Binutils is 2.47-4; the linker emits PE/COFF and readelf is omitted, but BFD recognizes ELF input vectors. A source-level review confirmed `cache_bwrite` reachability during ordinary PE output writes; the exact vulnerable write-size condition remains unresolved for the default RBL workflow. This is a release blocker, not a finding of known exploitation. This inventory is not a legal certification.
The Rust worker has a separate 28-crate active default-feature Cargo dependency/license inventory and corresponding-source archive; the updated wx build scripts include it in the build payload and installers verify and retain it. See the [Rust worker notices](../../../native/wx-ui/RUST_DEPENDENCY_NOTICES.md). The optional legacy-egui feature has a separate dependency graph and is not part of the shipped worker.

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
| IDE startup fails | Check that the wx GUI executable and adjacent `rbl-studio-worker.exe` are present. Rebuild the native frontend with `tools\build_wx_ide.ps1`; backend tasks additionally require Python 3.9+ and the selected GNU toolchain. The legacy egui GUI is a separate developer-only fallback (`RBL_STUDIO_ALLOW_LEGACY=1`). |

## Limitations

The fact that a MinGW path is detected does not mean every combination of Windows version, CRT and compiler version has been tested. The benchmark with handwritten Linux syscall programs cannot run as a native Windows benchmark; use WSL or Linux. See the [benchmarking guide](benchmarking.md).
