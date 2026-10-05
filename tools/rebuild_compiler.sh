#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/bin/linux-x86_64"
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -static "$ROOT/compiler/rblc_asm.c" -o "$ROOT/bin/linux-x86_64/rblc-asm"
as --64 "$ROOT/runtime/rbl_runtime.s" -o "$ROOT/bin/linux-x86_64/rbl_runtime.o"
chmod +x "$ROOT/bin/linux-x86_64/rblc-asm"
printf 'Bootstrap compiler and runtime rebuilt.\n'
