# last verified: v0.8.1 | 2026.11.17 | UBUNTU 24.04 NATIVE AND FLATPAK SCOPE

## Linux runtime choices

**Current status (2026-11-17):** the 0.8.1 native Linux wxWidgets GUI/Rust-worker
build and installation through `installer/install_linux.sh` were verified on
Ubuntu 24.04 with system wxWidgets 3.2.4 (`RBL_USE_SYSTEM_WX=1`). The 0.8.1 tree
is prepared but not yet published. The local Flatpak payload was built and
installed; sandbox CLI `version`, `doctor`, `check` and `run` passed. Interactive
Flatpak GUI/display/portal acceptance and Flathub publication remain unverified.

RBL Studio provides two Linux GUI delivery paths:

| Delivery | Host GTK runtime | wxWidgets | Assembler/linker | Python |
|---|---|---|---|---|
| Native shell installer | GTK 3 runtime and X11/Wayland display stack | System wxWidgets 3.2.4 for the verified Ubuntu 24.04 build (`RBL_USE_SYSTEM_WX=1`); app-local mode remains available | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |
| Flatpak | Supplied by `org.gnome.Platform//51` inside the sandbox | App-local wxWidgets 3.2.4 | App-local Clang integrated assembler + LLD | App-local CPython 3.14.10 |

The verified native build uses system wxWidgets 3.2.4 and requires the host GTK 3
runtime plus a working X11 or Wayland display. The native installer also carries
the GUI's wxWidgets shared libraries in its application payload; GTK and the
display stack remain host runtime dependencies. The independent Flatpak path is
unfinished and must not be treated as an accepted delivery option. The CLI can
be used independently of the GUI.

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

The native installer stages the GUI's wxWidgets shared libraries and the LLVM
toolchain under the application directory. It does not need wxWidgets
development files, GNU Binutils, a system Python interpreter or a C compiler
for ordinary use. The host must provide GTK 3 runtime libraries and a working
X11 or Wayland session, as well as GLIBC >=2.39 and libstdc++ exposing
GLIBCXX_3.4.32 for the currently verified Ubuntu 24.04 payload. The verified
build selected the distro wxWidgets 3.2.4 installation with
`RBL_USE_SYSTEM_WX=1`; the alternative app-local wxWidgets build mode is separate.

Build and install:

```sh
RBL_USE_SYSTEM_WX=1 bash tools/build_wx_ide.sh build/linux-ubuntu2404
bash installer/install_linux.sh --build-dir build/linux-ubuntu2404
```

The exact installer arguments are shown by `bash installer/install_linux.sh --help`.

## Flatpak (local bundle and CLI acceptance passed; GUI acceptance pending)

Flatpak remains independent from the verified native Linux build/install path.
The 0.8.1 payload was prepared, a local bundle was built and installed in the
Ubuntu 24.04 WSL user installation, and sandbox CLI `version`, `doctor`, `check`
and `run` passed. The wx GUI was not launched in the sandbox, so interactive
display, file-dialog, portal and multi-monitor acceptance remain untested. This
is local development evidence, not a Flathub or public-release acceptance claim.

The Flatpak manifest declares `org.gnome.Platform//51`. Flatpak installs that
runtime automatically when the application is installed; GTK is provided by
the runtime rather than by the host distribution. The app bundle includes the
wxWidgets shared libraries, CPython runtime and LLVM tools.

Prepare and build a local bundle:

```sh
RBL_USE_SYSTEM_WX=1 bash tools/build_wx_ide.sh build/output/wx-ui-linux-0.8.1
python3 tools/prepare_flatpak_payload.py \
  --build-dir build/output/wx-ui-linux-0.8.1 \
  --output "$HOME/rblstudio-flatpak-payload"
ln -s "$HOME/rblstudio-flatpak-payload" build/flatpak-payload
flatpak install flathub org.gnome.Platform//51 org.gnome.Sdk//51
flatpak-builder --state-dir="$HOME/.cache/rblstudio-flatpak-builder" \
  --force-clean --repo="$HOME/rblstudio-flatpak-repo" \
  "$HOME/rblstudio-flatpak-build" packaging/flatpak/com.reiwa.rblstudio.yml
flatpak build-bundle "$HOME/rblstudio-flatpak-repo" "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak" \
  com.reiwa.rblstudio
flatpak install --user "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak"
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

The repository manifest is a local prebuilt-payload prototype, not a Flathub
submission. A public Flathub manifest must
build the application from source modules inside the SDK. The app ID
`com.reiwa.rblstudio` is provisional and must be checked against the publisher's
namespace before any public submission.

The prepared payload and Flatpak builder state/repository/build directories must
be staged on a case-sensitive Linux filesystem. The `build/flatpak-payload`
symlink lets the repository manifest reach the payload without placing its
case-distinct files on the Windows-mounted checkout filesystem.
Python's bundled terminfo tree contains case-distinct filenames, so WSL paths
under `/mnt/c`, `/mnt/q`, and other Windows-mounted drives are rejected by the
preparer. Use a directory under `$HOME` or another native Linux filesystem.
The payload includes `SHA256SUMS`, which the Flatpak build verifies before
copying files into `/app`.

## Third-party runtime licenses and binary build inputs

The native Linux build consumes pinned binary runtime/toolchain inputs and keeps
their upstream URLs, SHA-256 values and provenance records with the relevant
runtime/toolchain metadata. These internal build inputs are not corresponding-
source archives and are not release deliverables. GitHub Releases publish the
repository source tree; no separate corresponding-source archives are staged,
required or published as part of the RBL Studio release process.

| Component | Runtime license/notice | Binary input / upstream provenance |
|---|---|---|
| CPython 3.14.10 / python-build-standalone | CPython PSF and python-build-standalone MPL-2.0 texts; bundled pip/vendor notices | `runtime/python/python-runtime.tar.gz` is a pinned internal binary build input, verified by `runtime/python/python-runtime.sha256`; upstream URL, archive hash and license/provenance details are recorded in `runtime/python/UPSTREAM.md` and `rbl-python-runtime.json`. It is unpacked into the app-local runtime. |
| wxWidgets 3.2.4 | wxWindows Library Licence 3.1; system package copyright text in system-wx mode | Verified Ubuntu 24.04 build uses system wxWidgets 3.2.4 (`RBL_USE_SYSTEM_WX=1`); app-local mode is a separate build option. |
| Scintilla / wxStyledTextCtrl | Scintilla license notice at `licenses/Scintilla/License.txt` | Used through wxStyledTextCtrl; no separate corresponding-source archive is a release requirement. |
| Clang/LLD 23.1.5 | Apache-2.0 with LLVM Exceptions at `toolchain/linux-x86_64/LICENSE.TXT` | Pinned binary toolchain; upstream source commit, URL/signature evidence and binary SHA-256 are recorded in `UPSTREAM.md`, `manifest.json` and `provenance/`. |
| Rust IDE worker | Per-crate MIT/Apache and other declared licenses in `licenses/Rust/` | Built from the repository's Rust source and `Cargo.lock`; no separate corresponding-source archive is a release requirement. |
| GTK 3 / host display stack | Host distribution supplies GTK runtime and display integration | Runtime dependencies of the native GUI; not bundled by the native installer. |

The packaging pipeline verifies pinned binary archive sizes and SHA-256 values
and refuses to overwrite corrupt cache entries. The CPython runtime tarball is
an internal binary build input, distinct from a corresponding-source archive;
its upstream URL/hash provenance is retained even though the archive is not a
release deliverable. Included license texts and provenance records are
technical compliance evidence, not a legal opinion or formal legal sign-off.
