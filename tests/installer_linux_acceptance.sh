#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="${RBL_STUDIO_LINUX_BUILD_DIR:-$ROOT/build/wx-ui-linux}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/rbl-linux-installer-acceptance.XXXXXX")"
trap 'rm -rf -- "$TMP"' EXIT

[[ -x "$BUILD_DIR/rbl-studio-wx" && -x "$BUILD_DIR/rbl-studio-worker" ]]

FAKEBIN="$TMP/fakebin"
mkdir -p "$FAKEBIN"
cat > "$FAKEBIN/xdg-mime" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == query && "${2:-}" == default ]]; then
  [[ -f "$XDG_CONFIG_HOME/.rbl-fake-mime-default" ]] &&
    cat "$XDG_CONFIG_HOME/.rbl-fake-mime-default"
  exit 0
fi
if [[ "${1:-}" == default ]]; then
  app="$2"
  mkdir -p "$XDG_CONFIG_HOME"
  printf '%s\n' "$app" > "$XDG_CONFIG_HOME/.rbl-fake-mime-default"
  exit 0
fi
echo "unsupported xdg-mime invocation: $*" >&2
exit 2
EOF
cat > "$FAKEBIN/update-mime-database" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$FAKEBIN/update-desktop-database" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$FAKEBIN"/*

export PATH="$FAKEBIN:$PATH"
TEST_HOME="$TMP/home"
DATA_HOME="$TEST_HOME/data"
CONFIG_HOME="$TEST_HOME/config"
INSTALL_DIR="$TMP/install path"
mkdir -p "$DATA_HOME" "$CONFIG_HOME"
export HOME="$TEST_HOME" XDG_DATA_HOME="$DATA_HOME" XDG_CONFIG_HOME="$CONFIG_HOME"
printf 'previous.desktop\n' > "$CONFIG_HOME/.rbl-fake-mime-default"

# Normal registered install, then uninstall with different XDG environment.
bash "$ROOT/installer/install_linux.sh" --install-dir "$INSTALL_DIR" --build-dir "$BUILD_DIR"
grep -Fxq "registered=1" "$INSTALL_DIR/.rblstudio-install-state"
grep -Fxq "data_home=$(realpath -m "$DATA_HOME")" "$INSTALL_DIR/.rblstudio-install-state"
grep -Fxq "config_home=$(realpath -m "$CONFIG_HOME")" "$INSTALL_DIR/.rblstudio-install-state"
test -f "$DATA_HOME/applications/rblstudio-wx.desktop"
test -f "$DATA_HOME/icons/hicolor/256x256/apps/rblstudio-wx.png"
test -f "$DATA_HOME/mime/packages/rewa-rbl-wx.xml"
grep -Fxq 'rblstudio-wx.desktop' "$CONFIG_HOME/.rbl-fake-mime-default"

NEW_DATA="$TMP/changed-data"
NEW_CONFIG="$TMP/changed-config"
mkdir -p "$NEW_DATA" "$NEW_CONFIG"
export XDG_DATA_HOME="$NEW_DATA" XDG_CONFIG_HOME="$NEW_CONFIG"
bash "$ROOT/installer/uninstall_linux.sh" --install-dir "$INSTALL_DIR"
test ! -e "$INSTALL_DIR"
test ! -e "$DATA_HOME/applications/rblstudio-wx.desktop"
test ! -e "$DATA_HOME/icons/hicolor/256x256/apps/rblstudio-wx.png"
test ! -e "$DATA_HOME/mime/packages/rewa-rbl-wx.xml"
grep -Fxq 'previous.desktop' "$CONFIG_HOME/.rbl-fake-mime-default"
test ! -e "$NEW_DATA/applications/rblstudio-wx.desktop"

# --no-register install/uninstall must preserve unowned user files.
NO_REG_DIR="$TMP/no register install"
export XDG_DATA_HOME="$TMP/no-register-data" XDG_CONFIG_HOME="$TMP/no-register-config"
bash "$ROOT/installer/install_linux.sh" --no-register --install-dir "$NO_REG_DIR" --build-dir "$BUILD_DIR"
grep -Fxq 'registered=0' "$NO_REG_DIR/.rblstudio-install-state"
test ! -e "$XDG_DATA_HOME/applications/rblstudio-wx.desktop"
printf 'keep-me\n' > "$NO_REG_DIR/user-note.txt"
bash "$ROOT/installer/uninstall_linux.sh" --no-register --install-dir "$NO_REG_DIR"
grep -Fxq 'keep-me' "$NO_REG_DIR/user-note.txt"

# A symlinked XDG integration directory must fail before committing the app.
SYMDATA="$TMP/symlink-data"
mkdir -p "$SYMDATA" "$TMP/outside"
ln -s "$TMP/outside" "$SYMDATA/applications"
export XDG_DATA_HOME="$SYMDATA" XDG_CONFIG_HOME="$TMP/symlink-config"
if bash "$ROOT/installer/install_linux.sh" --install-dir "$TMP/symlink directory install" --build-dir "$BUILD_DIR"; then
  echo 'FAIL: installer accepted a symlinked XDG applications directory' >&2
  exit 1
fi
test ! -e "$TMP/symlink directory install"

# A dangling symlink integration file must not be followed or overwritten.
DANGLING_DATA="$TMP/dangling-data"
mkdir -p "$DANGLING_DATA/applications" "$DANGLING_DATA/icons/hicolor/256x256/apps" "$DANGLING_DATA/mime/packages"
ln -s "$TMP/nonexistent-target" "$DANGLING_DATA/applications/rblstudio-wx.desktop"
export XDG_DATA_HOME="$DANGLING_DATA" XDG_CONFIG_HOME="$TMP/dangling-config"
if bash "$ROOT/installer/install_linux.sh" --install-dir "$TMP/dangling install" --build-dir "$BUILD_DIR"; then
  echo 'FAIL: installer accepted a dangling symlink integration file' >&2
  exit 1
fi
test -L "$DANGLING_DATA/applications/rblstudio-wx.desktop"
test ! -e "$TMP/dangling install"

# Inject failure on the MIME-file rename during an upgrade. The old install
# and all integration files must be restored; the failed candidate is retained.
ROLL_HOME="$TMP/rollback-home"
ROLL_DATA="$ROLL_HOME/data"
ROLL_CONFIG="$ROLL_HOME/config"
ROLL_INSTALL="$TMP/rollback install"
mkdir -p "$ROLL_DATA" "$ROLL_CONFIG"
export HOME="$ROLL_HOME" XDG_DATA_HOME="$ROLL_DATA" XDG_CONFIG_HOME="$ROLL_CONFIG"
printf 'previous.desktop\n' > "$ROLL_CONFIG/.rbl-fake-mime-default"
bash "$ROOT/installer/install_linux.sh" --install-dir "$ROLL_INSTALL" --build-dir "$BUILD_DIR"
printf 'preserve-old-install\n' > "$ROLL_INSTALL/user-marker.txt"
before_desktop="$(sha256sum "$ROLL_DATA/applications/rblstudio-wx.desktop" | awk '{print $1}')"
before_mime="$(sha256sum "$ROLL_DATA/mime/packages/rewa-rbl-wx.xml" | awk '{print $1}')"
FAILBIN="$TMP/failbin"
mkdir -p "$FAILBIN"
cat > "$FAILBIN/mv" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
last="${!#}"
if [[ "$last" == */rewa-rbl-wx.xml ]]; then
  echo 'injected MIME commit failure' >&2
  exit 73
fi
exec /usr/bin/mv "$@"
EOF
chmod +x "$FAILBIN/mv"
if PATH="$FAILBIN:$FAKEBIN:$PATH" bash "$ROOT/installer/install_linux.sh" --install-dir "$ROLL_INSTALL" --build-dir "$BUILD_DIR"; then
  echo 'FAIL: injected integration commit failure did not fail installation' >&2
  exit 1
fi
grep -Fxq 'preserve-old-install' "$ROLL_INSTALL/user-marker.txt"
[[ "$(sha256sum "$ROLL_DATA/applications/rblstudio-wx.desktop" | awk '{print $1}')" == "$before_desktop" ]]
[[ "$(sha256sum "$ROLL_DATA/mime/packages/rewa-rbl-wx.xml" | awk '{print $1}')" == "$before_mime" ]]
find "$TMP" -maxdepth 1 -type d -name 'rollback install.failed-*' | grep -q .
bash "$ROOT/installer/uninstall_linux.sh" --install-dir "$ROLL_INSTALL"

echo 'LINUX INSTALLER ACCEPTANCE: PASS (XDG migration, no-register, symlink refusal, transactional rollback)'
