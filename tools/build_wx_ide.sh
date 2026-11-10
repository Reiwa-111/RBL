#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/build/wx-ui-linux}"
if [[ "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
BUILD_DIR="$(mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && pwd -P)"
BUILD_NAME="$(basename "$BUILD_DIR")"
WORKER_TARGET_DIR="$ROOT/build/cargo-target-$BUILD_NAME"

cmake -S "$ROOT/native/wx-ui" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel
cargo build --manifest-path "$ROOT/Cargo.toml" --release --bin rbl-studio-worker --target-dir "$WORKER_TARGET_DIR"
cp "$WORKER_TARGET_DIR/release/rbl-studio-worker" "$BUILD_DIR/rbl-studio-worker"
mkdir -p "$BUILD_DIR/licenses/Scintilla"
cp "$ROOT/native/wx-ui/licenses/Scintilla-LICENSE.txt" "$BUILD_DIR/licenses/Scintilla/License.txt"
mkdir -p "$BUILD_DIR/licenses/Rust"
cp -a "$ROOT/native/wx-ui/licenses/Rust/." "$BUILD_DIR/licenses/Rust/"
cp "$ROOT/native/wx-ui/RUST_DEPENDENCY_NOTICES.md" "$BUILD_DIR/licenses/Rust/THIRD_PARTY_NOTICES.md"

RUST_SOURCE_ARCHIVE="$(find "$ROOT/build/source-material" -maxdepth 1 -type f -name 'RBL-0.8.0-Rust-Worker-Source-Materials-*.tar.zst' -printf '%T@ %f\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2- || true)"
[[ -n "$RUST_SOURCE_ARCHIVE" ]] || { echo "Required Rust worker corresponding-source archive is missing under build/source-material" >&2; exit 1; }
ZSTD_COMMAND="$(command -v zstd || true)"
PYTHON_COMMAND=python3
if [[ -n "$ZSTD_COMMAND" ]]; then
  "$PYTHON_COMMAND" "$ROOT/tools/verify_rust_source_bundle.py" --root "$ROOT" --archive "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE" --zstd "$ZSTD_COMMAND" --target x86_64-unknown-linux-gnu
else
  # Ubuntu/WSL images may ship libzstd without the command-line frontend.
  "$PYTHON_COMMAND" "$ROOT/tools/verify_rust_source_bundle.py" --root "$ROOT" --archive "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE"
fi
mkdir -p "$BUILD_DIR/source"
cp -- "$ROOT/build/source-material/$RUST_SOURCE_ARCHIVE" "$BUILD_DIR/source/Rust-worker-source.tar.zst"
RUST_SOURCE_HASH="$(sha256sum "$BUILD_DIR/source/Rust-worker-source.tar.zst" | awk '{print $1}')"
printf '%s  %s\n' "$RUST_SOURCE_HASH" 'Rust-worker-source.tar.zst' > "$BUILD_DIR/source/Rust-worker-source.sha256"
RUST_SOURCE_NOTICE="Rust worker corresponding-source archive is included at source/Rust-worker-source.tar.zst (SHA-256: $RUST_SOURCE_HASH). Verify it with source/Rust-worker-source.sha256."

cat > "$BUILD_DIR/THIRD_PARTY_NOTICES.txt" <<EOF
RBL Studio wxWidgets build - third-party runtime notices

This build dynamically links against the system wxWidgets 3.2 runtime and its
platform dependencies (GTK and the selected display backend). Those runtime
libraries are not bundled by this build script. Consult the corresponding
distribution packages for their license texts and notices. Backend actions
also require Python 3.9 or newer and GNU Binutils (as and ld), which are system
dependencies and are not bundled by this build script.

The editor uses wxStyledTextCtrl, which includes Scintilla. Its upstream
copyright and permission notice is preserved at licenses/Scintilla/License.txt.

Rust worker crate dependencies and their exact Cargo.lock versions/licenses are
listed in licenses/Rust/THIRD_PARTY_NOTICES.md. Package-specific license texts
are included under licenses/Rust/<crate>-<version>/.

$RUST_SOURCE_NOTICE

Release status: UNRELEASED. This inventory is informational, is not a legal
review, and does not establish binary redistribution readiness.
EOF

POINTER_DIR="$ROOT/build"
mkdir -p "$POINTER_DIR"
POINTER_TMP="$POINTER_DIR/wx-ui-current-linux.path.tmp"
printf '%s\n' "$BUILD_DIR" > "$POINTER_TMP"
mv -f -- "$POINTER_TMP" "$POINTER_DIR/wx-ui-current-linux.path"

echo "Built RBL Studio prototype: $BUILD_DIR"

