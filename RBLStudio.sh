#!/usr/bin/env bash
set -euo pipefail

SOURCE="${BASH_SOURCE[0]}"
while [[ -L "$SOURCE" ]]; do
  SOURCE_DIR="$(cd -P -- "$(dirname -- "$SOURCE")" >/dev/null 2>&1 && pwd)"
  LINK_TARGET="$(readlink -- "$SOURCE")"
  if [[ "$LINK_TARGET" == /* ]]; then
    SOURCE="$LINK_TARGET"
  else
    SOURCE="$SOURCE_DIR/$LINK_TARGET"
  fi
done
APP_ROOT="$(cd -P -- "$(dirname -- "$SOURCE")" >/dev/null 2>&1 && pwd)"
GUI=""
WORKER=""

# Portable package: GUI, worker and backend payload are siblings.
if [[ -x "$APP_ROOT/rbl-studio-wx" && -x "$APP_ROOT/rbl-studio-worker" ]]; then
  GUI="$APP_ROOT/rbl-studio-wx"
  WORKER="$APP_ROOT/rbl-studio-worker"
else
  # Developer checkout: the build script atomically updates this pointer.
  unset BUILD_DIR
  POINTER="$APP_ROOT/build/wx-ui-current-linux.path"
  if [[ -f "$POINTER" ]]; then
    IFS= read -r BUILD_DIR < "$POINTER" || true
    if [[ -n "${BUILD_DIR:-}" && "$BUILD_DIR" != /* ]]; then BUILD_DIR="$APP_ROOT/$BUILD_DIR"; fi
    if [[ -n "${BUILD_DIR:-}" && -x "$BUILD_DIR/rbl-studio-wx" && -x "$BUILD_DIR/rbl-studio-worker" ]]; then
      GUI="$BUILD_DIR/rbl-studio-wx"
      WORKER="$BUILD_DIR/rbl-studio-worker"
    fi
  fi
fi

if [[ -n "$GUI" ]]; then
  export RBL_STUDIO_ROOT="$APP_ROOT"
  export RBL_STUDIO_WORKER="$WORKER"
  cd "$APP_ROOT"
  exec "$GUI" "$@"
fi

if [[ "${RBL_STUDIO_ALLOW_LEGACY:-}" == "1" ]]; then
  if [[ -x "$APP_ROOT/target/release/rbl-studio" ]]; then
    exec "$APP_ROOT/target/release/rbl-studio" "$@"
  fi
  echo 'Legacy egui executable was not found.' >&2
  exit 1
fi

echo 'RBL Studio wxWidgets build was not found.' >&2
echo "Build it with: '$APP_ROOT/tools/build_wx_ide.sh'" >&2
echo 'To explicitly run the retired egui frontend, set RBL_STUDIO_ALLOW_LEGACY=1.' >&2
exit 1
