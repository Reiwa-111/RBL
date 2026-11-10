#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/bin/linux-x86_64"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
SOURCES=(
    "$ROOT/compiler/common.c"
    "$ROOT/compiler/lexer.c"
    "$ROOT/compiler/ast.c"
    "$ROOT/compiler/parser.c"
    "$ROOT/compiler/sema.c"
    "$ROOT/compiler/codegen.c"
    "$ROOT/compiler/driver.c"
)
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls \
    -Werror=missing-prototypes -Werror=redundant-decls -Werror=implicit-function-declaration \
    -static "-DRBL_VERSION=\"$VERSION\"" "${SOURCES[@]}" -o "$ROOT/bin/linux-x86_64/rblc-asm"
as --64 "$ROOT/runtime/rbl_runtime.s" -o "$ROOT/bin/linux-x86_64/rbl_runtime.o"
RUNTIME_CFLAGS=(-std=c11 -O2 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls \
    -Werror=missing-prototypes -Werror=redundant-decls -Werror=implicit-function-declaration)
cc "${RUNTIME_CFLAGS[@]}" "$ROOT/runtime/rbl_containers.c" -c -o "$ROOT/bin/linux-x86_64/rbl_containers.o"
cc "${RUNTIME_CFLAGS[@]}" "$ROOT/runtime/rbl_abi.c" -c -o "$ROOT/bin/linux-x86_64/rbl_abi.o"
cc "${RUNTIME_CFLAGS[@]}" \
    "$ROOT/runtime/rbl_format.c" -c -o "$ROOT/bin/linux-x86_64/rbl_format.o"
chmod +x "$ROOT/bin/linux-x86_64/rblc-asm"
printf 'Bootstrap compiler and all Linux Runtime objects rebuilt.\n'
