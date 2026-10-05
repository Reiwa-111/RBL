#!/usr/bin/env bash
set -euo pipefail
need=0
for tool in as ld; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing: $tool"
    need=1
  fi
done
if [ "$need" -ne 0 ]; then
  echo 'Install binutils in the WSL distribution, e.g.: sudo apt install binutils'
  exit 1
fi
echo 'WSL backend is ready: as + ld detected.'
