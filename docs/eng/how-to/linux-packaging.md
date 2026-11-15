# last verified: v0.8.0 | 2026.11.12 | LINUX PORTABLE TOOLCHAIN AND FLATPAK

## Linux runtime choices

**Integration status (2026-11-12):** the app-local wxWidgets/IDE build, CTest
(11/11), native Linux installer acceptance, app-local LLVM provenance and
installed CLI/worker acceptance have passed on Ubuntu 24.04 WSL. Flatpak payload
preparation, 5,246 checksums and manifest parsing have passed; the GNOME runtime
bundle and sandbox acceptance are still pending. No GUI session was
launched and no released Linux package is claimed.

RBL Studio provides two Linux GUI delivery paths:

| Delivery | Host GTK runtime | wxWidgets | Assembler/linker | Python |
|---|---|---|---|---|
| Native shell installer | GTK 3 runtime and X11/Wayland display stack | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |
| Flatpak | Supplied by `org.gnome.Platform//51` inside the sandbox | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |

Neither delivery path requires host GNU Binutils (`as`/`ld`) or a system wxWidgets
installation. The Flatpak path also removes the host GTK runtime dependency.
Both still rely on the host kernel, graphics driver integration and display
server. The CLI can be used independently of the GUI.

The Linux LLVM toolchain is ClangBuiltLinux LLVM 23.1.5. `clang` is invoked in
integrated-assembler mode (`-x assembler`); `ld.lld` performs ELF linking. The
bundle is hash-pinned, contains the full Apache-2.0-with-LLVM-exception license
text, records its upstream source commit, and verifies the detached kernel.org
GPG signature over the decompressed tar payload. The key, signature and
checksum evidence are retained in `toolchain/linux-x86_64/provenance/` and the
staged files are covered by `SHA256SUMS`. Its binary requires GLIBC 2.27 or
newer and host `libstdc++` exposing `GLIBCXX_3.4.22`. The complete native
application has a higher measured floor: GLIBC 2.39 and GLIBCXX 3.4.32, set by
wxWidgets/IDE built on Ubuntu 24.04. Older native distributions require a
rebuild against an older sysroot. The actual audit is retained in
`LINUX_ABI_AUDIT.txt`; CPython native-module/import checks are in
`PYTHON_RUNTIME_ELF_AUDIT.txt`. The upstream python-build-standalone Tcl/Tk
libraries retain two `/tools/deps/lib` RUNPATH entries; the tested runtime
resolves all dependencies and imports `tkinter` without an external library
path. Packaged launchers set `RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1`, so a
missing bundled toolchain is an error rather than a silent fallback to host
GNU Binutils.

## Native shell installation

The native installer stages wxWidgets shared libraries and the LLVM toolchain
under the application directory. It does not need wxWidgets development files,
GNU Binutils, a system Python interpreter or a C compiler for ordinary use.
The host must provide GTK 3 runtime libraries and a working X11 or Wayland
session, as well as GLIBC >=2.39 and libstdc++ exposing GLIBCXX_3.4.32 for the
current native payload. Building wxWidgets from source additionally requires GTK 3 development
headers, a C/C++ compiler, CMake, Make and Cargo.

Build and install:

```sh
bash tools/build_wx_ide.sh build/portable-linux
bash installer/install_linux.sh --build-dir build/portable-linux
```

The exact installer arguments are shown by `bash installer/install_linux.sh --help`.

## Flatpak installation

The Flatpak manifest declares `org.gnome.Platform//51`. Flatpak installs that
runtime automatically when the application is installed; GTK is provided by
the runtime rather than by the host distribution. The app bundle includes the
wxWidgets shared libraries, CPython runtime and LLVM tools.

Prepare and build a local bundle:

```sh
bash tools/build_wx_ide.sh build/portable-linux
python3 tools/prepare_flatpak_payload.py \
  --build-dir build/portable-linux \
  --output build/flatpak-payload
flatpak install flathub org.gnome.Platform//51 org.gnome.Sdk//51
flatpak-builder --force-clean --repo=build/flatpak-repo \
  build/flatpak-build packaging/flatpak/com.reiwa.rblstudio.yml
flatpak build-bundle build/flatpak-repo build/RBLStudio-0.8.0-linux-x86_64.flatpak \
  com.reiwa.rblstudio
flatpak install --user build/RBLStudio-0.8.0-linux-x86_64.flatpak
flatpak run com.reiwa.rblstudio
```

CLI commands can be invoked inside the sandbox with:

```sh
flatpak run --command=rbl com.reiwa.rblstudio version
flatpak run --command=rbl com.reiwa.rblstudio run /path/inside/home/example.rbl
```

The sandbox receives access to the user's home directory so projects can be
opened and saved. Wayland and X11 fallback sockets are enabled; DRI access is
enabled for graphics acceleration. The app does not request unrestricted host
filesystem or network access.

This repository manifest currently packages a locally prepared binary payload.
It is suitable for local/side-loaded Flatpak testing, but is not yet a Flathub
submission: a public Flathub manifest must build the application from source
modules inside the SDK. The app ID `com.reiwa.rblstudio` is provisional and must
be checked against the publisher's namespace before public submission.

The prepared payload must be staged on a case-sensitive Linux filesystem.
Python's bundled terminfo tree contains case-distinct filenames, so WSL paths
under `/mnt/c`, `/mnt/q`, and other Windows-mounted drives are rejected by the
preparer. Use a directory under `$HOME` or another native Linux filesystem.
The payload includes `SHA256SUMS`, which the Flatpak build verifies before
copying files into `/app`.

## Third-party component and source inventory

This table describes the Linux packaging pipeline, which stages the upstream
source archives under `source/` while building the Linux payload. The current
Windows checkout does not bundle those archives; upstream URLs, hashes and source
references are recorded in `runtime/python/rbl-python-runtime.json` and
`toolchain/windows-x86_64/SOURCE_CODE.md`.

| Component | Runtime license/notice | Corresponding source or provenance |
|---|---|---|
| CPython 3.14.10 | PSF license plus bundled pip/vendor notices | `source/Python-3.14.10.tar.xz`; runtime archive hash and upstream metadata in `runtime/python/` |
| python-build-standalone | MPL-2.0 license | `source/python-build-standalone-20261110.tar.gz`; tag commit and SHA-256 recorded in runtime metadata |
| wxWidgets 3.2.4 | wxWindows Library Licence 3.1 | `source/wxWidgets-3.2.4.tar.bz2`; license at `licenses/wxWidgets/License.txt` |
| Scintilla / wxStyledTextCtrl | Scintilla license notice | Included in the wxWidgets source tree; notice at `licenses/Scintilla/License.txt` |
| Clang/LLD 23.1.5 | Apache-2.0 with LLVM Exceptions | Full license at `toolchain/linux-x86_64/LICENSE.TXT`; source commit and binary SHA-256 in `UPSTREAM.md` and `manifest.json` |
| Rust IDE worker | Per-crate MIT/Apache and other declared licenses | `source/Rust-worker-source.tar.zst`; crate notices under `licenses/Rust/` |
| GTK and GNOME runtime | Licenses supplied by the GNOME Platform runtime | Installed separately by Flatpak; not redistributed inside the RBL application bundle |

The Windows MinGW-w64/GCC/Binutils dependency closure has its own notices and
corresponding-source archive, documented in
[`toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md`](../../../toolchain/windows-x86_64/SOURCE_ARCHIVE_AUDIT.md).

The packaging scripts verify pinned archive sizes and SHA-256 values and refuse
to overwrite corrupt cache entries. These controls and the included source and
license materials are technical compliance evidence, not a legal opinion or
formal legal sign-off. Public redistribution remains gated on owner/legal
review of the full dependency closure and the exact distribution procedure.
