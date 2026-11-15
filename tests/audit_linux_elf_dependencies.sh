#!/usr/bin/env bash
set -euo pipefail

if (($# < 1 || $# > 2)); then
  echo "Usage: $0 LINUX_BUILD_DIR [APPLICATION_ROOT]" >&2
  exit 2
fi

BUILD_DIR="$(cd -- "$1" && pwd -P)"
APPLICATION_ROOT="${2:-$(cd -- "$(dirname -- "$0")/.." && pwd -P)}"
TOOLCHAIN="$BUILD_DIR/toolchain/linux-x86_64"
PYTHON_TMP="$(mktemp -d "${TMPDIR:-/tmp}/rbl-python-elf-audit.XXXXXX")"
trap 'rm -rf -- "$PYTHON_TMP"' EXIT
tar -xzf "$BUILD_DIR/runtime/python/python-runtime.tar.gz" -C "$PYTHON_TMP"
PYTHON_ROOT="$PYTHON_TMP/python"
LIB_PATH="$BUILD_DIR/lib:$BUILD_DIR/wxwidgets-install/lib:$TOOLCHAIN/lib:$PYTHON_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

[[ -x "$BUILD_DIR/rbl-studio-wx" && -x "$BUILD_DIR/rbl-studio-worker" ]]
[[ -x "$TOOLCHAIN/bin/llvm" && -x "$TOOLCHAIN/bin/clang" && -x "$TOOLCHAIN/bin/ld.lld" ]]
[[ -x "$PYTHON_ROOT/bin/python3" ]]

ELF_FILES=("$BUILD_DIR/rbl-studio-wx" "$BUILD_DIR/rbl-studio-worker" "$TOOLCHAIN/bin/llvm" "$TOOLCHAIN/bin/clang" "$TOOLCHAIN/bin/ld.lld" "$PYTHON_ROOT/bin/python3")
if [[ -x "$APPLICATION_ROOT/bin/linux-x86_64/rblc-asm" ]]; then
  ELF_FILES+=("$APPLICATION_ROOT/bin/linux-x86_64/rblc-asm")
fi

while IFS= read -r -d '' binary; do
  missing="$(LD_LIBRARY_PATH="$LIB_PATH" ldd "$binary" 2>&1 | grep 'not found' || true)"
  if [[ -n "$missing" ]]; then
    echo "Unresolved ELF dependencies for $binary:" >&2
    echo "$missing" >&2
    exit 1
  fi
done < <(
  printf '%s\0' "${ELF_FILES[@]}"
  find "$BUILD_DIR/lib" "$PYTHON_ROOT/lib" -type f -name '*.so*' -print0
)

python3 - "$BUILD_DIR" "$PYTHON_ROOT" "${ELF_FILES[*]}" <<'PY'
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
python_root = Path(sys.argv[2])
files = [Path(item) for item in sys.argv[3].split()] + sorted((root / "lib").glob("*.so.*")) + sorted(python_root.glob("lib/**/*.so*"))
versions = {"GLIBC": set(), "GLIBCXX": set()}
for path in files:
    if not path.is_file():
        continue
    result = subprocess.run(["readelf", "--version-info", str(path)], text=True, capture_output=True, check=True)
    for kind, version in re.findall(r"\b(GLIBCXX|GLIBC)_([0-9]+(?:\.[0-9]+)+)", result.stdout):
        versions[kind].add(tuple(int(part) for part in version.split(".")))
    dynamic = subprocess.run(["readelf", "-d", str(path)], text=True, capture_output=True, check=True)
    for kind, paths in re.findall(r"\((RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic.stdout):
        for entry in paths.split(":"):
            if entry.startswith("$ORIGIN/"):
                resolved = (path.parent / entry.replace("$ORIGIN", ".", 1)).resolve()
                if not (resolved.is_relative_to(root) or resolved.is_relative_to(python_root)):
                    raise SystemExit(f"{kind} escapes app-local roots in {path}: {entry}")
            elif path.name in {"libtcl9.0.so", "libtcl9tk9.0.so"} and entry == "/tools/deps/lib":
                print(f"Known upstream python-build-standalone Tcl/Tk RUNPATH retained: {path.name}: {entry}")
            else:
                raise SystemExit(f"unsafe {kind} in {path}: {entry}")

def render(value):
    return ".".join(str(part) for part in value)

for kind in ("GLIBC", "GLIBCXX"):
    maximum = max(versions[kind]) if versions[kind] else None
    print(f"{kind} maximum required: {render(maximum) if maximum else 'not referenced'}")
PY

echo 'LINUX ELF DEPENDENCY AUDIT: PASS (all dependencies resolved; app-local $ORIGIN paths and two pinned upstream Tcl/Tk RUNPATHs only)'
