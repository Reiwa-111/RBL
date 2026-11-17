#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build/output/wx-ui-linux}"
if [[ "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
BUILD_DIR="$(mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && pwd -P)"
BUILD_NAME="$(basename "$BUILD_DIR")"
OUTPUT_DIR="$ROOT/build/output"
WORKER_TARGET_DIR="$OUTPUT_DIR/cargo-target-$BUILD_NAME"

# Default to the pinned app-local wxWidgets build. RBL_USE_SYSTEM_WX=1 enables
# offline distro builds using host wxWidgets; GTK remains a host dependency.
WX_SOURCE_DIR="$OUTPUT_DIR/wxwidgets-source-3.2.4/wxWidgets-3.2.4"
WX_PREFIX="$BUILD_DIR/wxwidgets-install"
WX_BUILD_DIR="$BUILD_DIR/wxwidgets-build"
mkdir -p "$BUILD_DIR/lib" "$BUILD_DIR/licenses/wxWidgets"
if [[ "${RBL_USE_SYSTEM_WX:-0}" == "1" ]]; then
  WX_CONFIG="$(command -v wx-config || true)"
  [[ -n "$WX_CONFIG" ]] || { echo 'RBL_USE_SYSTEM_WX=1 requires wx-config.' >&2; exit 1; }
  WX_VERSION="$("$WX_CONFIG" --version)"
  [[ "$WX_VERSION" == 3.2.* ]] || { echo "Unsupported system wxWidgets version: $WX_VERSION" >&2; exit 1; }
  WX_LIBS="$("$WX_CONFIG" --libs)"
  WX_LIBDIR="${WX_LIBS#*-L}"
  WX_LIBDIR="${WX_LIBDIR%% *}"
  [[ -d "$WX_LIBDIR" ]] || WX_LIBDIR="/usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)"
  cp -a "$WX_LIBDIR"/libwx*.so* "$BUILD_DIR/lib/"
  WX_PACKAGE="$(dpkg-query -S "$WX_LIBDIR/libwx_gtk3u_core-3.2.so.0" 2>/dev/null | head -n 1 | cut -d: -f1 || true)"
  if [[ -n "$WX_PACKAGE" ]]; then
    WX_COPYRIGHT="$(dpkg -L "$WX_PACKAGE" | grep '/copyright$' | head -n 1 || true)"
    [[ -z "$WX_COPYRIGHT" ]] || cp "$WX_COPYRIGHT" "$BUILD_DIR/licenses/wxWidgets/License.txt"
  fi
  WX_CMAKE_ARGS=()
  WX_RUNTIME_NOTE="System wxWidgets $WX_VERSION is used; GTK and other platform libraries are host dependencies."
else
python3 "$ROOT/tools/package_wxwidgets.py" \
  --cache "$ROOT/build/cache/wxwidgets-3.2.4" \
  --output "$OUTPUT_DIR/wxwidgets-source-3.2.4"
if [[ ! -x "$WX_PREFIX/bin/wx-config" ]]; then
  mkdir -p "$WX_BUILD_DIR"
  (
    cd "$WX_BUILD_DIR"
    "$WX_SOURCE_DIR/configure" \
      --prefix="$WX_PREFIX" \
      --with-gtk=3 \
      --enable-unicode \
      --enable-aui \
      --enable-stc \
      --enable-shared \
      --disable-webview \
      --disable-mediactrl \
      --with-libpng=builtin \
      --with-libjpeg=builtin \
      --with-libtiff=builtin \
      --with-expat=builtin \
      --with-zlib=builtin \
      --enable-optimise \
      --disable-debug
    make -j"$(nproc)"
    make install
  )
fi
WX_VERSION="$("$WX_PREFIX/bin/wx-config" --version)"
[[ "$WX_VERSION" == "3.2.4" ]] || { echo "Unexpected app-local wxWidgets version: $WX_VERSION" >&2; exit 1; }
cp -a "$WX_PREFIX/lib"/libwx*.so* "$BUILD_DIR/lib/"
cp "$WX_SOURCE_DIR/docs/licence.txt" "$BUILD_DIR/licenses/wxWidgets/License.txt"
cp "$ROOT/build/output/wxwidgets-source-3.2.4/wxwidgets-source.json" "$BUILD_DIR/licenses/wxWidgets/UPSTREAM.json"
WX_CONFIG="$WX_PREFIX/bin/wx-config"
WX_CMAKE_ARGS=(-DwxWidgets_CONFIG_EXECUTABLE="$WX_CONFIG" -DCMAKE_PREFIX_PATH="$WX_PREFIX")
WX_RUNTIME_NOTE="Pinned wxWidgets 3.2.4 is built from upstream source and bundled under lib/. GTK remains a host dependency."
fi

# Do not ship GNU Binutils on Linux: app-local LLVM MC/LLD provides the
# assembler/linker path without inheriting GNU ld's unresolved CVE review.
python3 "$ROOT/tools/package_llvm_toolchain.py" \
  --cache "$ROOT/build/cache/llvm-23.1.5" \
  --output "$BUILD_DIR/toolchain/linux-x86_64"

LD_LIBRARY_PATH="$BUILD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
cmake -S "$ROOT/rbl-ide/wx-ui" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SKIP_RPATH=ON \
  "${WX_CMAKE_ARGS[@]}"
LD_LIBRARY_PATH="$BUILD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  cmake --build "$BUILD_DIR" --parallel
cargo build --manifest-path "$ROOT/Cargo.toml" --locked --release --bin rbl-studio-worker --target-dir "$WORKER_TARGET_DIR"
cp "$WORKER_TARGET_DIR/release/rbl-studio-worker" "$BUILD_DIR/rbl-studio-worker"
mkdir -p "$BUILD_DIR/licenses/Scintilla"
cp "$ROOT/rbl-ide/wx-ui/licenses/Scintilla-LICENSE.txt" "$BUILD_DIR/licenses/Scintilla/License.txt"
mkdir -p "$BUILD_DIR/licenses/Rust"
cp -a "$ROOT/rbl-ide/wx-ui/licenses/Rust/." "$BUILD_DIR/licenses/Rust/"
cp "$ROOT/rbl-ide/wx-ui/RUST_DEPENDENCY_NOTICES.md" "$BUILD_DIR/licenses/Rust/THIRD_PARTY_NOTICES.md"

PYTHON_COMMAND=python3
"$PYTHON_COMMAND" "$ROOT/tools/package_python_runtime.py" --platform linux --output "$BUILD_DIR/runtime/python"
PYTHON_RUNTIME_NOTICE="App-local CPython 3.14.10 is included for Linux x86-64 glibc (minimum GLIBC_2.17). Its pinned runtime archive is used only as a build input and unpacked into the installed runtime/python directory. CPython PSF and python-build-standalone MPL license texts are included under runtime/python/licenses; the upstream package also retains pip/vendor notices. See runtime/python/UPSTREAM.md."

# Record the actual ELF ABI floor and prove the extracted Python runtime's
# standard-library/native-module dependencies before creating install payloads.
bash "$ROOT/tests/audit_linux_elf_dependencies.sh" "$BUILD_DIR" "$ROOT" > "$BUILD_DIR/LINUX_ABI_AUDIT.txt"
bash "$ROOT/tests/audit_python_runtime_elf.sh" "$BUILD_DIR/runtime/python/python-runtime.tar.gz" > "$BUILD_DIR/PYTHON_RUNTIME_ELF_AUDIT.txt"

cat > "$BUILD_DIR/THIRD_PARTY_NOTICES.txt" <<EOF
RBL Studio wxWidgets build - third-party runtime notices

${WX_RUNTIME_NOTE}
The wxWindows Library Licence 3.1 text is under licenses/wxWidgets/License.txt
for the pinned build. The GUI uses GTK 3 as its platform backend.
The native shell installer still requires the host GTK/display stack; the
recommended Flatpak package supplies that stack through org.gnome.Platform 51.
The native Linux payload was built and audited on Ubuntu 24.04: its current ELF
floor is GLIBC 2.39 and GLIBCXX 3.4.32 (see LINUX_ABI_AUDIT.txt). Older native
distributions need a rebuild against an older sysroot; this is not a universal
Linux binary baseline. The CPython/Tcl runtime import audit is recorded in
PYTHON_RUNTIME_ELF_AUDIT.txt.
The app-local CPython runtime retains CPython PSF, python-build-standalone
MPL-2.0 and bundled third-party notices. No separate corresponding-source
archive is required by the IDE build or installation path; the project source
is maintained in the GitHub repository.

Backend actions use app-local CPython 3.14.10 staged under runtime/python.
Linux assembler/linker tools are staged app-locally from the hash-pinned
ClangBuiltLinux LLVM 23.1.5 toolchain (Clang integrated assembler and LLD);
GNU Binutils are not bundled or required. The LLVM binary itself requires GLIBC
2.27 and host libstdc++ with GLIBCXX_3.4.22; the higher native application
floor is set by the Ubuntu 24.04-built wxWidgets libraries and IDE. The two
upstream Tcl/Tk shared libraries retain their pinned /tools/deps/lib RUNPATH
entries; they are not needed on the tested host, where all ELF dependencies
resolve and the tkinter module imports successfully. See
toolchain/linux-x86_64/UPSTREAM.md for hashes and licenses.
A C compiler is not needed for ordinary installed execution when compiler and
runtime artifacts are present.

The editor uses wxStyledTextCtrl, which includes Scintilla. Its upstream
copyright and permission notice is preserved at licenses/Scintilla/License.txt.

Rust worker crate dependencies and their exact Cargo.lock versions/licenses are
listed in licenses/Rust/THIRD_PARTY_NOTICES.md. Package-specific license texts
are included under licenses/Rust/<crate>-<version>/.

$PYTHON_RUNTIME_NOTICE

Release status: UNRELEASED. This inventory is informational, is not a legal
review, and does not establish binary redistribution readiness.
EOF

POINTER_DIR="$OUTPUT_DIR"
mkdir -p "$POINTER_DIR"
POINTER_TMP="$POINTER_DIR/wx-ui-current-linux.path.tmp"
printf '%s\n' "$BUILD_DIR" > "$POINTER_TMP"
mv -f -- "$POINTER_TMP" "$POINTER_DIR/wx-ui-current-linux.path"

echo "Built RBL Studio: $BUILD_DIR"

