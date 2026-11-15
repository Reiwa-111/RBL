#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build/output/wx-ui-linux}"
if [[ "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
BUILD_DIR="$(mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && pwd -P)"
BUILD_NAME="$(basename "$BUILD_DIR")"
OUTPUT_DIR="$ROOT/build/output"
WORKER_TARGET_DIR="$OUTPUT_DIR/cargo-target-$BUILD_NAME"

# Build the exact wxWidgets source revision into the payload instead of relying
# on a system wxWidgets runtime. GTK remains a host dependency for the native
# installer; the Flatpak distribution supplies GTK through GNOME Platform.
WX_SOURCE_DIR="$OUTPUT_DIR/wxwidgets-source-3.2.4/wxWidgets-3.2.4"
WX_SOURCE_ARCHIVE="$ROOT/build/cache/wxwidgets-3.2.4/wxWidgets-3.2.4.tar.bz2"
WX_PREFIX="$BUILD_DIR/wxwidgets-install"
WX_BUILD_DIR="$BUILD_DIR/wxwidgets-build"
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
mkdir -p "$BUILD_DIR/lib" "$BUILD_DIR/licenses/wxWidgets" "$BUILD_DIR/source"
cp -a "$WX_PREFIX/lib"/libwx*.so* "$BUILD_DIR/lib/"
cp "$WX_SOURCE_DIR/docs/licence.txt" "$BUILD_DIR/licenses/wxWidgets/License.txt"
cp "$ROOT/build/output/wxwidgets-source-3.2.4/wxwidgets-source.json" "$BUILD_DIR/licenses/wxWidgets/UPSTREAM.json"
cp "$WX_SOURCE_ARCHIVE" "$BUILD_DIR/source/wxWidgets-3.2.4.tar.bz2"
WX_SOURCE_HASH="$(sha256sum "$BUILD_DIR/source/wxWidgets-3.2.4.tar.bz2" | awk '{print $1}')"
[[ "$WX_SOURCE_HASH" == "0640e1ab716db5af2ecb7389dbef6138d7679261fbff730d23845ba838ca133e" ]] || {
  echo "wxWidgets corresponding-source archive hash mismatch: $WX_SOURCE_HASH" >&2; exit 1;
}
printf '%s  %s\n' "$WX_SOURCE_HASH" 'wxWidgets-3.2.4.tar.bz2' > "$BUILD_DIR/source/wxWidgets-3.2.4.sha256"

# Do not ship GNU Binutils on Linux: app-local LLVM MC/LLD provides the
# assembler/linker path without inheriting GNU ld's unresolved CVE review.
python3 "$ROOT/tools/package_llvm_toolchain.py" \
  --cache "$ROOT/build/cache/llvm-23.1.5" \
  --output "$BUILD_DIR/toolchain/linux-x86_64"

PATH="$WX_PREFIX/bin:$PATH" \
LD_LIBRARY_PATH="$WX_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
PKG_CONFIG_PATH="$WX_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
cmake -S "$ROOT/rbl-ide/wx-ui" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SKIP_RPATH=ON \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_PREFIX/bin/wx-config" \
  -DCMAKE_PREFIX_PATH="$WX_PREFIX"
PATH="$WX_PREFIX/bin:$PATH" LD_LIBRARY_PATH="$BUILD_DIR/lib:$WX_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  cmake --build "$BUILD_DIR" --parallel
cargo build --manifest-path "$ROOT/Cargo.toml" --locked --release --bin rbl-studio-worker --target-dir "$WORKER_TARGET_DIR"
cp "$WORKER_TARGET_DIR/release/rbl-studio-worker" "$BUILD_DIR/rbl-studio-worker"
mkdir -p "$BUILD_DIR/licenses/Scintilla"
cp "$ROOT/rbl-ide/wx-ui/licenses/Scintilla-LICENSE.txt" "$BUILD_DIR/licenses/Scintilla/License.txt"
mkdir -p "$BUILD_DIR/licenses/Rust"
cp -a "$ROOT/rbl-ide/wx-ui/licenses/Rust/." "$BUILD_DIR/licenses/Rust/"
cp "$ROOT/rbl-ide/wx-ui/RUST_DEPENDENCY_NOTICES.md" "$BUILD_DIR/licenses/Rust/THIRD_PARTY_NOTICES.md"

RUST_SOURCE_ARCHIVE="$(find "$ROOT/build/source-material" -maxdepth 1 -type f -name 'RBL-0.8.0-Rust-Worker-Source-Materials-*.tar.zst' -printf '%T@ %f\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2- || true)"
[[ -n "$RUST_SOURCE_ARCHIVE" ]] || { echo "Required Rust worker corresponding-source archive is missing under build/source-material" >&2; exit 1; }
ZSTD_COMMAND="$(command -v zstd || true)"
PYTHON_COMMAND=python3
if [[ -n "$ZSTD_COMMAND" ]]; then
  # The notice/source archive describes the union of both shipped worker
  # targets. Verify that full closure even when building only the Linux binary.
  "$PYTHON_COMMAND" "$ROOT/tools/verify_rust_source_bundle.py" --root "$ROOT" --archive "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE" --zstd "$ZSTD_COMMAND"
else
  # Ubuntu/WSL images may ship libzstd without the command-line frontend.
  "$PYTHON_COMMAND" "$ROOT/tools/verify_rust_source_bundle.py" --root "$ROOT" --archive "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE"
fi
mkdir -p "$BUILD_DIR/source"
cp -- "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE" "$BUILD_DIR/source/Rust-worker-source.tar.zst"
RUST_SOURCE_HASH="$(sha256sum "$BUILD_DIR/source/Rust-worker-source.tar.zst" | awk '{print $1}')"
printf '%s  %s\n' "$RUST_SOURCE_HASH" 'Rust-worker-source.tar.zst' > "$BUILD_DIR/source/Rust-worker-source.sha256"
RUST_SOURCE_NOTICE="Rust worker corresponding-source archive is included at source/Rust-worker-source.tar.zst (SHA-256: $RUST_SOURCE_HASH). Verify it with source/Rust-worker-source.sha256."

"$PYTHON_COMMAND" "$ROOT/tools/package_python_runtime.py" --platform linux --output "$BUILD_DIR/runtime/python"
cp -a "$BUILD_DIR/runtime/python/source/." "$BUILD_DIR/source/"
PYTHON_RUNTIME_NOTICE="App-local CPython 3.14.10 is included for Linux x86-64 glibc (minimum GLIBC_2.17). The verified upstream archive is staged at runtime/python/python-runtime.tar.gz and unpacked into the installed runtime/python directory. CPython PSF and python-build-standalone MPL license texts are included under runtime/python/licenses; the upstream package also retains pip/vendor notices. See runtime/python/UPSTREAM.md."

# Record the actual ELF ABI floor and prove the extracted Python runtime's
# standard-library/native-module dependencies before creating install payloads.
bash "$ROOT/tests/audit_linux_elf_dependencies.sh" "$BUILD_DIR" "$ROOT" > "$BUILD_DIR/LINUX_ABI_AUDIT.txt"
bash "$ROOT/tests/audit_python_runtime_elf.sh" "$BUILD_DIR/runtime/python/python-runtime.tar.gz" > "$BUILD_DIR/PYTHON_RUNTIME_ELF_AUDIT.txt"

cat > "$BUILD_DIR/THIRD_PARTY_NOTICES.txt" <<EOF
RBL Studio wxWidgets build - third-party runtime notices

wxWidgets 3.2.4 is built from the pinned upstream source archive and its shared
libraries are included under lib/. The exact source archive and checksum are
included under source/. Its wxWindows Library Licence 3.1 text is under
licenses/wxWidgets/License.txt. The GUI uses GTK 3 as its platform backend.
The native shell installer still requires the host GTK/display stack; the
recommended Flatpak package supplies that stack through org.gnome.Platform 51.
The native Linux payload was built and audited on Ubuntu 24.04: its current ELF
floor is GLIBC 2.39 and GLIBCXX 3.4.32 (see LINUX_ABI_AUDIT.txt). Older native
distributions need a rebuild against an older sysroot; this is not a universal
Linux binary baseline. The CPython/Tcl runtime import audit is recorded in
PYTHON_RUNTIME_ELF_AUDIT.txt.
CPython 3.14.10 and python-build-standalone source archives are included under
source/ with SHA-256 sidecars; the runtime keeps CPython PSF, python-build-
standalone MPL-2.0 and bundled third-party notices.

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

$RUST_SOURCE_NOTICE

$PYTHON_RUNTIME_NOTICE

Release status: UNRELEASED. This inventory is informational, is not a legal
review, and does not establish binary redistribution readiness.
EOF

POINTER_DIR="$OUTPUT_DIR"
mkdir -p "$POINTER_DIR"
POINTER_TMP="$POINTER_DIR/wx-ui-current-linux.path.tmp"
printf '%s\n' "$BUILD_DIR" > "$POINTER_TMP"
mv -f -- "$POINTER_TMP" "$POINTER_DIR/wx-ui-current-linux.path"

echo "Built RBL Studio prototype: $BUILD_DIR"

