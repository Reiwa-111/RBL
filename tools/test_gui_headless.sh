#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v xvfb-run >/dev/null 2>&1; then
  echo "xvfb-run not installed; skipping headless GUI test"
  exit 0
fi
cd "$ROOT"
timeout 12s xvfb-run -a python3 tools/ide_smoke.py

timeout 4s xvfb-run -a python3 RBLStudio.py --safe >/tmp/rblstudio-safe.out 2>&1 || rc=$?
rc=${rc:-0}
# timeout may return 124 because safe mode intentionally remains open.
if [ "$rc" != 0 ] && [ "$rc" != 124 ]; then
  cat /tmp/rblstudio-safe.out
  exit "$rc"
fi

echo "RBL Studio headless GUI test: PASS"
