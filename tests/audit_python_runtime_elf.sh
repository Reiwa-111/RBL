#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  echo "Usage: $0 PYTHON_RUNTIME_ARCHIVE" >&2
  exit 2
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/rbl-python-runtime-audit.XXXXXX")"
trap 'rm -rf -- "$TMP"' EXIT
tar -xzf "$1" -C "$TMP"
PYTHON_ROOT="$TMP/python"
for binary in "$PYTHON_ROOT/bin/python3" "$PYTHON_ROOT"/lib/*.so* "$PYTHON_ROOT"/lib/python3.14/lib-dynload/*.so; do
  [[ -f "$binary" ]] || continue
  missing="$(ldd "$binary" 2>&1 | grep 'not found' || true)"
  if [[ -n "$missing" ]]; then
    echo "Unresolved Python runtime dependencies for $binary:" >&2
    echo "$missing" >&2
    exit 1
  fi
  readelf -d "$binary" 2>/dev/null | grep -E 'RPATH|RUNPATH' | sed "s|^|$binary: |" || true
done

"$PYTHON_ROOT/bin/python3" -B -c 'import _ctypes, bz2, ctypes, hashlib, lzma, sqlite3, ssl, tkinter, zlib; print("CPython runtime imports: PASS")'
