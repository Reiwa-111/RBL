# Maintainer build toolchain — host requirements and build instructions

This directory is documentation and provisioning metadata for building RBL
Studio itself. **The maintainer build toolchain is external to the repository
and the shipped application.** MSYS2, the host C/C++ build tools, Rust, CMake,
and desktop-development dependencies live on the maintainer's machine and are
listed here so IDE/worker builds are reproducible.

End users never need the maintainer build toolchain: release packages are
**prebuilt** and contain the IDE, Rust worker, app-local Python and the
compiler/runtime artifacts they need. A user does not compile RBL Studio from
source. The shipped compiler backend is a separate product runtime dependency:
the Windows app includes a pruned MinGW assembler/linker subset and Linux uses
app-local LLVM tools so users can compile RBL programs without installing a
developer toolchain.

> 0.8.0 source-tree inventory: `toolchain/windows-x86_64/` contains a 43-file,
> approximately 21.9 MB pruned MinGW runtime bundle (44 files on disk including
> its manifest; 47 files are tracked in the directory including notices/audits).
> It is the Windows backend for
> compiling user RBL programs in a source checkout and is also staged into the
> prebuilt application; it is **not** the maintainer toolchain for building the
> IDE and does not contain GCC's C frontend/headers. `bin/` similarly contains
> prebuilt compiler/runtime product inputs. Keep these runtime/product inputs
> distinct from host build dependencies; removing either requires a replacement
> that preserves clean-install user compilation and fresh package acceptance.

## 1. Host prerequisites

The exact pinned versions below are the versions used to produce the 0.8.0
Windows and Linux payloads. Newer patch versions are acceptable only after
re-running the full build and test matrix.

### Windows (x86-64)

| Component | Version used | Notes |
|---|---|---|
| Windows | 10/11 x86-64 | File-identity and Job Object APIs are used by the IDE/worker. |
| MSYS2 UCRT64 | current rolling install | <https://www.msys2.org/> — install to `C:\msys64`. |
| MinGW-w64 GCC | 16.2.0-4 | `mingw-w64-ucrt-x86_64-gcc`, `...-libgcc`, `...-libstdc++`. |
| GNU Binutils | 2.47-4 | `mingw-w64-ucrt-x86_64-binutils`; assembler and PE linker. |
| MinGW-w64 CRT / winpthreads | 14.0.0.r426.g4564ee4b5-1 | `...-crt`, `...-libwinpthread`, `...-winpthreads`. |
| wxWidgets | 3.2.11-1 | `...-wxwidgets3.2-common`, `...-common-libs`, `...-msw`, `...-msw-libs`. |
| zlib | 1.4-2 | `...-zlib`; the release DLL is hash-pinned in the build script. |
| zstd | 1.5.7-2 | `...-zstd`. |
| CMake | 4.4.4 | Must be on `PATH`. |
| Rust (GNU toolchain) | stable, verified with 1.97.1 | MSRV 1.85; target `x86_64-pc-windows-gnu`. |
| Python | 3.12 (3.9+ supported) | Used by the build/packaging scripts, not by end users. |
| Git | 2.48 | |

Install the UCRT64 packages from an **MSYS2 UCRT64** shell:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgcc \
  mingw-w64-ucrt-x86_64-libstdc++ mingw-w64-ucrt-x86_64-binutils \
  mingw-w64-ucrt-x86_64-crt mingw-w64-ucrt-x86_64-libwinpthread \
  mingw-w64-ucrt-x86_64-winpthreads mingw-w64-ucrt-x86_64-zlib \
  mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-wxwidgets3.2-common \
  mingw-w64-ucrt-x86_64-wxwidgets3.2-common-libs \
  mingw-w64-ucrt-x86_64-wxwidgets3.2-msw \
  mingw-w64-ucrt-x86_64-wxwidgets3.2-msw-libs \
  mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-libjpeg-turbo \
  mingw-w64-ucrt-x86_64-libtiff mingw-w64-ucrt-x86_64-libwebp \
  mingw-w64-ucrt-x86_64-pcre2 mingw-w64-ucrt-x86_64-xz \
  mingw-w64-ucrt-x86_64-brotli mingw-w64-ucrt-x86_64-jbigkit \
  mingw-w64-ucrt-x86_64-lerc mingw-w64-ucrt-x86_64-libdeflate \
  mingw-w64-ucrt-x86_64-windows-default-manifest
```

Then:

```powershell
rustup toolchain install stable-x86_64-pc-windows-gnu
rustup target add x86_64-pc-windows-gnu
```

### Linux (x86-64)

| Component | Version used | Notes |
|---|---|---|
| Ubuntu | 24.04 LTS | Defines the native payload ABI floor (GLIBC 2.39, GLIBCXX 3.4.32). |
| GCC/Clang host compiler | distribution | Only for building the bootstrap compiler and Runtime objects. |
| CMake | 4.4.4 | |
| Ninja or GNU Make | distribution | CMake generator. |
| GTK 3 dev + wxWidgets dev | wxWidgets 3.2.4 (`libwxgtk3.2-dev`) | Native GUI development build; release builds build wxWidgets 3.2.4 from source. |
| Rust | stable, verified with 1.99.1 | MSRV 1.85. |
| Python | 3.12 | Build scripts only. |
| Flatpak / flatpak-builder | current | Only for the Flatpak package (GNOME Platform 51). |

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
  libgtk-3-dev libwxgtk3.2-dev libbz2-dev liblzma-dev zlib1g-dev \
  python3 cargo rustc git
```

## 2. Pinned build artifacts

These downloads are **build-time only**. They are cached locally under
`build/cache/` and never committed or shipped as source-checkout contents. The
release payload contains only the built/staged result.

| Artifact | Version | URL | SHA-256 |
|---|---|---|---|
| ClangBuiltLinux LLVM (Linux assembler/linker) | 23.1.5 | <https://cdn.kernel.org/pub/tools/llvm/files/llvm-23.1.5-x86_64.tar.xz> | `b69433120ad62420d789f601c58a5472873c724be088c99a70b72fd30ca4af3c` |
| wxWidgets source | 3.2.4 | <https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.4/wxWidgets-3.2.4.tar.bz2> | `0640e1ab716db5af2ecb7389dbef6138d7679261fbff730d23845ba838ca133e` |
| CPython Windows embeddable | 3.14.10 | <https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip> | `90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159` |
| CPython source | 3.14.10 | <https://www.python.org/ftp/python/3.14.10/Python-3.14.10.tar.xz> | `de414a57119abef52784d8993e341d53aed635fc2b05c2607d9f98a2c5b72032` |
| python-build-standalone (Linux runtime) | 20261110 | <https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.14.10%2B20261110-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz> | `c2ff2e97422f5d3896a16e0317fd212ba977bbf2688d93a6f7a03d817506bd30` |
| python-build-standalone source | 20261110 | <https://github.com/astral-sh/python-build-standalone/archive/refs/tags/20261110.tar.gz> | `63b8166347616283ee6d3ed449e957bc71b65368bc60945d21ab957e6fcaee49` |

MSYS2 packages are authenticated by the MSYS2 GPG keyring (`pacman`), not by a
hand-copied hash table. Exact package versions and their source-archive URLs are
recorded in [`requirements.json`](requirements.json) and, for the legacy bundle,
in `windows-x86_64/msys64/ucrt64/rbl-toolchain-manifest.json`.

The `tools/` downloaders re-verify every cached archive on every use and fail
closed on a hash, size or signature mismatch. Do not replace a pinned version
without re-running the full build and test matrix and updating
`requirements.json` and this table.

## 3. Build outputs and cache

The checkout keeps disposable state out of Git and separates it by purpose:

| Path | Contents | Clean command |
|---|---|---|
| `build/cache/sources/` | compiled user programs (per-source identity directory) | `make clean-cache` / `rbl.py clean` |
| `build/cache/<component>/` | downloaded upstream archives (wxWidgets, LLVM, Python) | `make clean-cache` removes only `sources/`; remove component dirs manually if needed |
| `build/output/` | bootstrap compiler, wx UI builds, Cargo targets, extracted wxWidgets source, Rust link libs, launcher pointer | `make clean-build` / `make clean` |
| `build/source-material/` | corresponding-source evidence archives | never removed by clean targets |
| `bin/` | ready-to-use compiler/runtime product objects | rebuilt by `make compiler runtime` |
| `target/` | Cargo development output | not part of release packaging |

`RBL_CACHE_DIR` and `RBL_OUTPUT_DIR` override the two roots; `RBL_BUILD_DIR`
remains the common base (defaults to `build/`, or the user cache directory in a
Flatpak/installed context).

## 4. Build the desktop application

Windows (PowerShell from the checkout root):

```powershell
./tools/build_wx_ide.ps1
```

Linux:

```bash
./tools/build_wx_ide.sh
```

Both scripts build the GUI and the Rust worker and stage the result under
`build/output/wx-ui-<platform>/`. The launchers read
`build/output/wx-ui-current-<platform>.path` in a developer checkout.

Build the language bootstrap compiler and Runtime objects when their sources
changed:

```bash
make compiler runtime        # Linux host
python rbl.py bootstrap      # Windows/WSL, explicit compiler rebuild
```

## 5. Produce the prebuilt package

A released copy is assembled from an **explicit allowlist** of product files
(`installer/install_windows.ps1`, `installer/install_linux.sh` and
`tools/prepare_flatpak_payload.py`). Never zip a build tree directly: CMake and
wxWidgets build directories contain absolute developer paths and test logs that
must not be distributed.

For a portable package, use the installer's portable staging mode. It stages
the same allowlist but omits the install manifest, uninstaller and
registry/XDG integration, so the result contains no machine-specific state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/install_windows.ps1 `
  -InstallDir build/output/stage/RBLStudio-0.8.0-windows-x86_64 `
  -Portable -BuildDir build/output/wx-ui-win
```

```bash
bash installer/install_linux.sh \
  --install-dir "$HOME/rbl-stage/RBLStudio-0.8.0-linux-x86_64" \
  --portable --build-dir build/output/wx-ui-linux
```

Then create the transport archive and hashes:

```bash
python tools/package_release.py --platform windows \
  --staged build/output/stage/RBLStudio-0.8.0-windows-x86_64
python tools/package_release.py --platform linux \
  --staged "$HOME/rbl-stage/RBLStudio-0.8.0-linux-x86_64"
```

The packager refuses trees that still contain install state, CMake build
directories, caches or debug artifacts. It writes the archive, `SHA256SUMS`,
per-archive JSON metadata and a `-LOCAL_CANDIDATE.txt` note; it never publishes
or uploads anything.

Acceptance for a prebuilt package:

1. Launch on a clean machine with no Python, compiler, MSYS2 or RBL environment
   variables.
2. `RBLStudio.sh` / `RBLStudio.cmd` / `RBLStudio.vbs` find the GUI and worker as
   siblings of the launcher; no `RBL_STUDIO_WORKER`, `RBL_BUILD_DIR` pointer or
   rebuild is required.
3. Check, Build, Run and Format work through the bundled worker and app-local
   Python.
4. Corresponding-source archives and notices are present and hash-verified by
   the installer.

The shipped `toolchain/windows-x86_64` and `toolchain/linux-x86_64` directories
are **product runtime closures** (assembler, linker, startup libraries) needed
to compile RBL user programs on a clean machine. They are not the project build
toolchain: MSYS2, GCC, Rust, CMake and the build Python remain host
prerequisites described in section 1 and are never installed by the package.

## 6. Licensing

The repository's Apache-2.0 license does not relicense third-party toolchain
components. See `windows-x86_64/THIRD_PARTY_NOTICES.md`, `windows-x86_64/SOURCE_CODE.md`,
`rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md`, `rbl-ide/wx-ui/RUST_DEPENDENCY_NOTICES.md`
and the payload `THIRD_PARTY_NOTICES.txt`. This is an engineering inventory, not
legal advice.
