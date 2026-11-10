#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/rbl-gcc-analyzer.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

if ! command -v gcc >/dev/null 2>&1; then
    echo "GCC is required for -fanalyzer." >&2
    exit 2
fi

CFLAGS=(
    -std=c11 -O0 -Wall -Wextra -Wpedantic
    -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls
    -Werror=missing-prototypes -Werror=redundant-decls
    -Werror=implicit-function-declaration -fanalyzer
    "-DRBL_VERSION=\"$VERSION\""
    -I"$ROOT/compiler" -I"$ROOT/runtime"
)

SOURCES=(
    compiler/common.c
    compiler/lexer.c
    compiler/ast.c
    compiler/parser.c
    compiler/sema.c
    compiler/codegen.c
    compiler/driver.c
    runtime/rbl_containers.c
    runtime/rbl_abi.c
    runtime/rbl_format.c
)

for source in "${SOURCES[@]}"; do
    object="$OUT/$(basename "${source%.c}").o"
    echo "GCC_ANALYZER $source"
    gcc "${CFLAGS[@]}" -c "$ROOT/$source" -o "$object"
done

echo "GCC -fanalyzer passed for all seven compiler modules and three shared Runtime C modules."
