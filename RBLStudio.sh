#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="$ROOT/.venv/bin/python3"
if [ ! -x "$PY" ]; then PY="$(command -v python3 || true)"; fi
if [ -z "$PY" ]; then echo 'RBL Studio: Python 3 not found.' >&2; exit 1; fi
if ! "$PY" -c 'import tkinter' >/dev/null 2>&1; then
  echo 'RBL Studio: tkinter is missing. Install with: sudo apt install python3-tk' >&2
  exit 1
fi
cd "$ROOT"
if [ "${RBLSTUDIO_DEBUG:-0}" = "1" ] || [ "${1:-}" = "--debug" ]; then
  export RBLSTUDIO_DEBUG=1
fi
exec "$PY" "$ROOT/RBLStudio.py" "$@"
