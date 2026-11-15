#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="${RBL_STUDIO_LINUX_BUILD_DIR:-$ROOT/build/output/wx-ui-linux}"
export PYTHONDONTWRITEBYTECODE=1
TMP="$(mktemp -d "${TMPDIR:-/tmp}/rbl-linux-installer-acceptance.XXXXXX")"
if [[ "${RBL_KEEP_ACCEPTANCE_TMP:-0}" == 1 ]]; then
  trap 'echo "Acceptance fixture retained at: $TMP"' EXIT
else
  trap 'rm -rf -- "$TMP"' EXIT
fi

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
while read -r source_name checksum_name; do
  test -f "$INSTALL_DIR/source/$source_name"
  test -f "$INSTALL_DIR/source/$checksum_name"
  (cd "$INSTALL_DIR/source" && sha256sum -c "$checksum_name")
done <<'EOF'
Rust-worker-source.tar.zst Rust-worker-source.sha256
wxWidgets-3.2.4.tar.bz2 wxWidgets-3.2.4.sha256
Python-3.14.10.tar.xz Python-3.14.10.tar.xz.sha256
python-build-standalone-20261110.tar.gz python-build-standalone-20261110.tar.gz.sha256
EOF
test -x "$INSTALL_DIR/toolchain/linux-x86_64/bin/clang"
test -x "$INSTALL_DIR/toolchain/linux-x86_64/bin/ld.lld"
test -f "$INSTALL_DIR/lib/libwx_gtk3u_core-3.2.so.0"
grep -Eq '"signature_verified"[[:space:]]*:[[:space:]]*true' "$INSTALL_DIR/toolchain/linux-x86_64/manifest.json"
(cd "$INSTALL_DIR/toolchain/linux-x86_64" && sha256sum -c SHA256SUMS)
grep -Fxq "data_home=$(realpath -m "$DATA_HOME")" "$INSTALL_DIR/.rblstudio-install-state"
grep -Fxq "config_home=$(realpath -m "$CONFIG_HOME")" "$INSTALL_DIR/.rblstudio-install-state"
test -f "$DATA_HOME/applications/rblstudio-wx.desktop"
test -f "$DATA_HOME/icons/hicolor/256x256/apps/rblstudio-wx.png"
test -f "$DATA_HOME/mime/packages/rewa-rbl-wx.xml"
grep -Fxq 'rblstudio-wx.desktop' "$CONFIG_HOME/.rbl-fake-mime-default"
# The installed compiler/runtime/worker must work without system Python, cc,
# GNU Binutils or inherited build tools. Keep only the app-local LLVM tools.
BACKEND_ONLY_BIN="$TMP/backend-only-bin"
mkdir -p "$BACKEND_ONLY_BIN"
ln -s "$INSTALL_DIR/toolchain/linux-x86_64/bin/clang" "$BACKEND_ONLY_BIN/clang"
ln -s "$INSTALL_DIR/toolchain/linux-x86_64/bin/ld.lld" "$BACKEND_ONLY_BIN/ld.lld"
ln -s "$(command -v bash)" "$BACKEND_ONLY_BIN/bash"
ln -s "$(command -v dirname)" "$BACKEND_ONLY_BIN/dirname"
if PATH="$BACKEND_ONLY_BIN" command -v as >/dev/null 2>&1 || PATH="$BACKEND_ONLY_BIN" command -v ld >/dev/null 2>&1; then
  echo 'FAIL: system GNU Binutils leaked into the clean backend PATH' >&2
  exit 1
fi
PATH="$BACKEND_ONLY_BIN" clang --version | grep -Fq '23.1.5'
PATH="$BACKEND_ONLY_BIN" ld.lld --version | grep -Fq '23.1.5'
APP_PYTHON="$INSTALL_DIR/runtime/python/bin/python3"
[[ -x "$APP_PYTHON" ]] || { echo "Missing installed app-local Python: $APP_PYTHON" >&2; exit 1; }
env -u RBL_PYTHON RBL_TOOLCHAIN=native RBL_NO_BUILD_CACHE=1 RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1 PATH="$BACKEND_ONLY_BIN" \
  "$APP_PYTHON" -B "$INSTALL_DIR/tests/installed_cli_acceptance.py" "$INSTALL_DIR"
env -u RBL_PYTHON RBL_TOOLCHAIN=native RBL_NO_BUILD_CACHE=1 RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1 PATH="$BACKEND_ONLY_BIN" \
  "$INSTALL_DIR/rbl" version
env -u RBL_PYTHON RBL_TOOLCHAIN=native RBL_NO_BUILD_CACHE=1 RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1 PATH="$BACKEND_ONLY_BIN" \
  "$INSTALL_DIR/rbl" run "$INSTALL_DIR/examples/test.rbl"
env -u RBL_PYTHON RBL_TOOLCHAIN=native RBL_NO_BUILD_CACHE=1 RBL_REQUIRE_BUNDLED_LINUX_TOOLCHAIN=1 PATH="$BACKEND_ONLY_BIN" \
  "$APP_PYTHON" -B "$INSTALL_DIR/rbl.py" clean

NEW_DATA="$TMP/changed-data"
NEW_CONFIG="$TMP/changed-config"
mkdir -p "$NEW_DATA" "$NEW_CONFIG"
export XDG_DATA_HOME="$NEW_DATA" XDG_CONFIG_HOME="$NEW_CONFIG"
bash "$ROOT/installer/uninstall_linux.sh" --install-dir "$INSTALL_DIR"
test ! -e "$INSTALL_DIR"
echo 'PASS: registered install/uninstall and XDG migration'
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
echo 'PASS: --no-register preserves untracked user files'

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
echo 'PASS: symlinked XDG integration directory rejected'

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
echo 'PASS: dangling symlink integration file preserved and rejected'

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
echo 'PASS: failed integration commit restores prior installation'

# Repeat the failed integration commit, but also fail every backup-to-target
# restore. Recovery copies must remain available instead of being deleted by
# the EXIT trap when the rollback itself is incomplete.
printf '\n# USER-DESKTOP-MARKER\n' >> "$ROLL_DATA/applications/rblstudio-wx.desktop"
printf '\nUSER-ICON-MARKER\n' >> "$ROLL_DATA/icons/hicolor/256x256/apps/rblstudio-wx.png"
printf '\n<!-- USER-MIME-MARKER -->\n' >> "$ROLL_DATA/mime/packages/rewa-rbl-wx.xml"
FAIL_RESTORE_BIN="$TMP/fail-restore-bin"
mkdir -p "$FAIL_RESTORE_BIN"
cat > "$FAIL_RESTORE_BIN/cp" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == -a && "${2:-}" == -- && "${3:-}" == */rblstudio-xdg-backup.*/* ]]; then
  echo 'injected integration restore failure' >&2
  exit 74
fi
exec /usr/bin/cp "$@"
EOF
chmod +x "$FAIL_RESTORE_BIN/cp"
RESTORE_FAILURE_LOG="$TMP/restore-failure.log"
if PATH="$FAIL_RESTORE_BIN:$FAILBIN:$FAKEBIN:$PATH" TMPDIR="$TMP" \
    bash "$ROOT/installer/install_linux.sh" --install-dir "$ROLL_INSTALL" --build-dir "$BUILD_DIR" \
    >"$RESTORE_FAILURE_LOG" 2>&1; then
  echo 'FAIL: injected restore failure did not fail installation' >&2
  exit 1
fi
grep -Fq 'integration recovery data retained at:' "$RESTORE_FAILURE_LOG"
grep -Fxq 'preserve-old-install' "$ROLL_INSTALL/user-marker.txt"
PRESERVED_INTEGRATION_BACKUP="$(find "$TMP" -maxdepth 1 -type d -name 'rblstudio-xdg-backup.*' -print -quit)"
[[ -n "$PRESERVED_INTEGRATION_BACKUP" ]]
grep -aFq 'USER-DESKTOP-MARKER' "$PRESERVED_INTEGRATION_BACKUP/desktop"
grep -aFq 'USER-ICON-MARKER' "$PRESERVED_INTEGRATION_BACKUP/icon"
grep -aFq 'USER-MIME-MARKER' "$PRESERVED_INTEGRATION_BACKUP/mime"
# The harness restores its isolated fixture from the retained recovery copy.
/usr/bin/cp -a -- "$PRESERVED_INTEGRATION_BACKUP/desktop" "$ROLL_DATA/applications/rblstudio-wx.desktop"
/usr/bin/cp -a -- "$PRESERVED_INTEGRATION_BACKUP/icon" "$ROLL_DATA/icons/hicolor/256x256/apps/rblstudio-wx.png"
/usr/bin/cp -a -- "$PRESERVED_INTEGRATION_BACKUP/mime" "$ROLL_DATA/mime/packages/rewa-rbl-wx.xml"
rm -rf -- "$PRESERVED_INTEGRATION_BACKUP"
grep -aFq 'USER-DESKTOP-MARKER' "$ROLL_DATA/applications/rblstudio-wx.desktop"
grep -aFq 'USER-ICON-MARKER' "$ROLL_DATA/icons/hicolor/256x256/apps/rblstudio-wx.png"
grep -aFq 'USER-MIME-MARKER' "$ROLL_DATA/mime/packages/rewa-rbl-wx.xml"
bash "$ROOT/installer/uninstall_linux.sh" --install-dir "$ROLL_INSTALL"
echo 'PASS: incomplete rollback retains recovery data and restores user-owned integration files'

echo 'LINUX INSTALLER ACCEPTANCE: PASS (XDG migration, no-register, symlink refusal, transactional rollback and retained recovery data)'
