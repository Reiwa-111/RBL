#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
INSTALL_DIR="${HOME}/.local/lib/rblstudio"
NO_REGISTER=0
BUILD_DIR="${RBL_STUDIO_BUILD_DIR:-}"
while (($#)); do
  case "$1" in
    --install-dir) (($# >= 2)) || { echo '--install-dir requires a path' >&2; exit 2; }; INSTALL_DIR="$2"; shift 2 ;;
    --no-register) NO_REGISTER=1; shift ;;
    --build-dir) (($# >= 2)) || { echo '--build-dir requires a path' >&2; exit 2; }; BUILD_DIR="$2"; shift 2 ;;
    -h|--help) echo 'Usage: install_linux.sh [--install-dir PATH] [--no-register] [--build-dir PATH]'; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

assert_no_symlink_components() {
  local path="$1" current part
  [[ "$path" == /* ]] || path="$PWD/$path"
  current="/"
  IFS='/' read -r -a components <<< "$path"
  for part in "${components[@]}"; do
    case "$part" in ''|.) continue ;; ..) current="$(dirname "$current")"; continue ;; esac
    current="${current%/}/$part"
    if [[ -L "$current" ]]; then
      echo "Install path contains a symlink component: $current" >&2
      return 1
    fi
  done
}

[[ "$INSTALL_DIR" == /* ]] || INSTALL_DIR="$PWD/$INSTALL_DIR"
assert_no_symlink_components "$INSTALL_DIR"

if [[ -z "$BUILD_DIR" ]]; then
  POINTER="$ROOT/build/wx-ui-current-linux.path"
  [[ -f "$POINTER" ]] && IFS= read -r BUILD_DIR < "$POINTER" || true
fi
if [[ -n "$BUILD_DIR" && "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
if [[ -z "$BUILD_DIR" || ! -d "$BUILD_DIR" ]]; then
  bash "$ROOT/tools/build_wx_ide.sh"
  IFS= read -r BUILD_DIR < "$ROOT/build/wx-ui-current-linux.path"
  if [[ "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
fi
BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"
for required in rbl-studio-wx rbl-studio-worker THIRD_PARTY_NOTICES.txt licenses/Scintilla/License.txt licenses/Rust/THIRD_PARTY_NOTICES.md source/Rust-worker-source.tar.zst source/Rust-worker-source.sha256; do
  [[ -f "$BUILD_DIR/$required" ]] || { echo "Incomplete wx build: missing $BUILD_DIR/$required" >&2; exit 1; }
done

PYTHON_COMMAND="${RBL_PYTHON:-python3}"
command -v "$PYTHON_COMMAND" >/dev/null 2>&1 || { echo 'Python >=3.9 is required for backend actions; install Python or set RBL_PYTHON.' >&2; exit 1; }
"$PYTHON_COMMAND" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)' || {
  echo "Python >=3.9 is required (selected interpreter: $PYTHON_COMMAND)." >&2; exit 1;
}
for tool in as ld; do
  command -v "$tool" >/dev/null 2>&1 || { echo "GNU $tool is required for RBL backend actions; install binutils." >&2; exit 1; }
done
if command -v ldd >/dev/null 2>&1; then
  for binary in "$BUILD_DIR/rbl-studio-wx" "$BUILD_DIR/rbl-studio-worker"; do
    missing="$(ldd "$binary" 2>&1 | grep 'not found' || true)"
    [[ -z "$missing" ]] || { echo "Missing runtime dependencies for $binary:" >&2; echo "$missing" >&2; exit 1; }
  done
else
  echo 'Warning: ldd is unavailable; wx/worker shared-library dependencies were not checked.' >&2
fi

mkdir -p "$(dirname "$INSTALL_DIR")"
assert_no_symlink_components "$INSTALL_DIR"
INSTALL_DIR="$(realpath -m -- "$INSTALL_DIR")"
assert_no_symlink_components "$INSTALL_DIR"
[[ "$INSTALL_DIR" != / ]] || { echo 'Refusing to install into filesystem root.' >&2; exit 1; }
PARENT="$(dirname "$INSTALL_DIR")"
STAGE="$(mktemp -d "$PARENT/.rblstudio-stage.XXXXXX")"
assert_no_symlink_components "$STAGE"
BACKUP_DIR=""
BACKUP_MOVED=0
INSTALL_COMMITTED=0
INSTALL_SUCCESS=0
FAILED_DIR=""
INTEGRATION_BACKUP_DIR=""
INTEGRATION_BACKUPS_COMPLETE=0
DESKTOP_TMP=""
ICON_TMP=""
MIME_TMP=""
DESKTOP_TARGET=""
ICON_TARGET=""
MIME_TARGET=""
cleanup() {
  rm -rf -- "$STAGE"
  [[ -z "$DESKTOP_TMP" ]] || rm -f -- "$DESKTOP_TMP"
  [[ -z "$ICON_TMP" ]] || rm -f -- "$ICON_TMP"
  [[ -z "$MIME_TMP" ]] || rm -f -- "$MIME_TMP"
  if [[ -n "$INTEGRATION_BACKUP_DIR" ]]; then
    if ((INSTALL_SUCCESS == 0 && INTEGRATION_BACKUPS_COMPLETE == 1)); then
      if [[ -f "$INTEGRATION_BACKUP_DIR/desktop.present" && -f "$INTEGRATION_BACKUP_DIR/desktop.ready" ]]; then
        cp -a -- "$INTEGRATION_BACKUP_DIR/desktop" "$DESKTOP_TARGET" 2>/dev/null || \
          echo "WARNING: could not restore desktop entry $DESKTOP_TARGET" >&2
      elif [[ ! -f "$INTEGRATION_BACKUP_DIR/desktop.present" ]]; then
        rm -f -- "$DESKTOP_TARGET" 2>/dev/null || \
          echo "WARNING: could not remove partial desktop entry $DESKTOP_TARGET" >&2
      fi
      if [[ -f "$INTEGRATION_BACKUP_DIR/icon.present" && -f "$INTEGRATION_BACKUP_DIR/icon.ready" ]]; then
        cp -a -- "$INTEGRATION_BACKUP_DIR/icon" "$ICON_TARGET" 2>/dev/null || \
          echo "WARNING: could not restore icon $ICON_TARGET" >&2
      elif [[ ! -f "$INTEGRATION_BACKUP_DIR/icon.present" ]]; then
        rm -f -- "$ICON_TARGET" 2>/dev/null || \
          echo "WARNING: could not remove partial icon $ICON_TARGET" >&2
      fi
      if [[ -f "$INTEGRATION_BACKUP_DIR/mime.present" && -f "$INTEGRATION_BACKUP_DIR/mime.ready" ]]; then
        cp -a -- "$INTEGRATION_BACKUP_DIR/mime" "$MIME_TARGET" 2>/dev/null || \
          echo "WARNING: could not restore MIME package $MIME_TARGET" >&2
      elif [[ ! -f "$INTEGRATION_BACKUP_DIR/mime.present" ]]; then
        rm -f -- "$MIME_TARGET" 2>/dev/null || \
          echo "WARNING: could not remove partial MIME package $MIME_TARGET" >&2
      fi
    fi
    rm -rf -- "$INTEGRATION_BACKUP_DIR"
  fi
  if ((INSTALL_SUCCESS == 0)); then
    if ((INSTALL_COMMITTED == 1)) && [[ -e "$INSTALL_DIR" ]]; then
      [[ -n "$FAILED_DIR" ]] || FAILED_DIR="$INSTALL_DIR.failed-$(date +%Y%m%d-%H%M%S)-$$"
      if mv -- "$INSTALL_DIR" "$FAILED_DIR"; then
        echo "Failed installation candidate preserved at: $FAILED_DIR" >&2
      else
        echo "WARNING: could not move failed installation candidate from $INSTALL_DIR" >&2
      fi
    fi
    if ((BACKUP_MOVED == 1)) && [[ -n "$BACKUP_DIR" && -d "$BACKUP_DIR" && ! -e "$INSTALL_DIR" ]]; then
      if mv -- "$BACKUP_DIR" "$INSTALL_DIR"; then
        BACKUP_MOVED=0
        echo "Previous installation restored at: $INSTALL_DIR" >&2
      else
        echo "WARNING: previous installation backup remains at: $BACKUP_DIR" >&2
      fi
    fi
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cp -- "$BUILD_DIR/rbl-studio-wx" "$BUILD_DIR/rbl-studio-worker" "$STAGE/"
cp -- "$ROOT/RBLStudio.sh" "$STAGE/"
cp -- "$ROOT/installer/uninstall_linux.sh" "$STAGE/Uninstall-RBLStudio.sh"
if [[ -d "$BUILD_DIR/licenses" ]]; then cp -a -- "$BUILD_DIR/licenses" "$STAGE/licenses"; fi
cp -a -- "$BUILD_DIR/source" "$STAGE/source"
for directory in tools compiler runtime bin toolchain tests benchmarks examples docs assets; do
  [[ -d "$ROOT/$directory" ]] || { echo "Missing required payload directory: $directory" >&2; exit 1; }
  cp -a -- "$ROOT/$directory" "$STAGE/$directory"
done
for file in rbl.py VERSION LICENSE README.md RELEASE_MANIFEST.txt; do
  [[ -f "$ROOT/$file" ]] || { echo "Missing required payload file: $ROOT/$file" >&2; exit 1; }
  cp -a -- "$ROOT/$file" "$STAGE/$file"
done
cp -- "$BUILD_DIR/THIRD_PARTY_NOTICES.txt" "$STAGE/THIRD_PARTY_NOTICES.txt"
chmod +x "$STAGE/RBLStudio.sh" "$STAGE/Uninstall-RBLStudio.sh"
for marker in VERSION rbl.py tools/rbltool.py compiler runtime toolchain tests/run_tests.py benchmarks/run_benchmarks.py licenses/Scintilla/License.txt licenses/Rust/THIRD_PARTY_NOTICES.md source/Rust-worker-source.tar.zst source/Rust-worker-source.sha256; do
  [[ -e "$STAGE/$marker" ]] || { echo "Staged application root is missing required marker: $marker" >&2; exit 1; }
done
(cd "$STAGE/source" && sha256sum -c Rust-worker-source.sha256)

PREVIOUS_DEFAULT=""
HAD_OWN_REGISTRATION=0
HAD_OWN_REGISTRATION_SET=0
SET_DEFAULT=1
PREVIOUS_DATA_HOME=""
PREVIOUS_CONFIG_HOME=""
STATE_DATA_HOME=""
STATE_CONFIG_HOME=""
if [[ -e "$INSTALL_DIR" ]]; then
  [[ -d "$INSTALL_DIR" && ! -L "$INSTALL_DIR" ]] || {
    echo "Refusing to replace a non-directory or symlink install path: $INSTALL_DIR" >&2; exit 1;
  }
  OLD_STATE="$INSTALL_DIR/.rblstudio-install-state"
  OLD_FILES="$INSTALL_DIR/.rblstudio-install-files"
  [[ -f "$OLD_STATE" && ! -L "$OLD_STATE" && -f "$OLD_FILES" && ! -L "$OLD_FILES" ]] || {
    echo "Existing directory is missing a safe RBL Studio install manifest: $INSTALL_DIR" >&2; exit 1;
  }
  grep -qx 'schema=1' "$OLD_STATE" && grep -qx 'release_status=UNRELEASED' "$OLD_STATE" || {
    echo 'Existing RBL Studio install state is invalid or unsupported.' >&2; exit 1;
  }
fi
if [[ -f "$INSTALL_DIR/.rblstudio-install-state" ]]; then
  while IFS='=' read -r key value; do
    case "$key" in
      registered) HAD_OWN_REGISTRATION="$value"; HAD_OWN_REGISTRATION_SET=1 ;;
      previous_default) PREVIOUS_DEFAULT="$value" ;;
      data_home) PREVIOUS_DATA_HOME="$value" ;;
      config_home) PREVIOUS_CONFIG_HOME="$value" ;;
    esac
  done < "$INSTALL_DIR/.rblstudio-install-state"
fi
case "$HAD_OWN_REGISTRATION" in
  0|1) ;;
  *) echo 'Invalid registered flag in existing install state.' >&2; exit 1 ;;
esac
if [[ -e "$INSTALL_DIR" && "$HAD_OWN_REGISTRATION_SET" != 1 ]]; then
  echo 'Existing RBL Studio install state has no registered flag.' >&2
  exit 1
fi
if ((NO_REGISTER == 1 && HAD_OWN_REGISTRATION == 1)); then
  STATE_DATA_HOME="$PREVIOUS_DATA_HOME"
  STATE_CONFIG_HOME="$PREVIOUS_CONFIG_HOME"
fi
if ((NO_REGISTER == 0)); then
  USING_STORED_DATA_HOME=0
  USING_STORED_CONFIG_HOME=0
  if ((HAD_OWN_REGISTRATION == 1)) && [[ -n "$PREVIOUS_DATA_HOME" ]]; then
    DATA_HOME="$PREVIOUS_DATA_HOME"
    USING_STORED_DATA_HOME=1
  else
    DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
  fi
  if ((HAD_OWN_REGISTRATION == 1)) && [[ -n "$PREVIOUS_CONFIG_HOME" ]]; then
    CONFIG_HOME="$PREVIOUS_CONFIG_HOME"
    USING_STORED_CONFIG_HOME=1
  else
    CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
  fi
  [[ "$DATA_HOME" == /* && "$CONFIG_HOME" == /* ]] || {
    echo 'XDG_DATA_HOME and XDG_CONFIG_HOME must be absolute paths.' >&2; exit 1;
  }
  if ((USING_STORED_DATA_HOME == 1)); then assert_no_symlink_components "$DATA_HOME"; fi
  if ((USING_STORED_CONFIG_HOME == 1)); then assert_no_symlink_components "$CONFIG_HOME"; fi
  DATA_HOME="$(realpath -m -- "$DATA_HOME")"
  CONFIG_HOME="$(realpath -m -- "$CONFIG_HOME")"
  assert_no_symlink_components "$DATA_HOME"
  assert_no_symlink_components "$CONFIG_HOME"
  export XDG_DATA_HOME="$DATA_HOME"
  export XDG_CONFIG_HOME="$CONFIG_HOME"
  STATE_DATA_HOME="$DATA_HOME"
  STATE_CONFIG_HOME="$CONFIG_HOME"
  APP_DIR="$DATA_HOME/applications"
  ICON_DIR="$DATA_HOME/icons/hicolor/256x256/apps"
  MIME_DIR="$DATA_HOME/mime/packages"
  assert_no_symlink_components "$APP_DIR"
  assert_no_symlink_components "$ICON_DIR"
  assert_no_symlink_components "$MIME_DIR"
  DESKTOP="$APP_DIR/rblstudio-wx.desktop"
  ICON="$ICON_DIR/rblstudio-wx.png"
  MIME_FILE="$MIME_DIR/rewa-rbl-wx.xml"
  OWN_INTEGRATION=0
  for integration_file in "$DESKTOP" "$ICON" "$MIME_FILE"; do
    if [[ -L "$integration_file" ]]; then
      echo "Refusing to follow a symlink integration file: $integration_file" >&2
      exit 1
    fi
  done
  [[ -f "$DESKTOP" ]] && grep -Fq "$INSTALL_DIR/RBLStudio.sh" "$DESKTOP" && OWN_INTEGRATION=1
  if ((OWN_INTEGRATION == 0)); then
    for integration_file in "$DESKTOP" "$ICON" "$MIME_FILE"; do
      if [[ -e "$integration_file" ]]; then
        echo "Refusing to overwrite unrelated user integration file: $integration_file" >&2
        exit 1
      fi
    done
  fi
  if ((HAD_OWN_REGISTRATION == 0)) && command -v xdg-mime >/dev/null 2>&1; then
    PREVIOUS_DEFAULT="$(xdg-mime query default application/x-reiwa-rbl 2>/dev/null || true)"
  elif ((HAD_OWN_REGISTRATION == 1)) && command -v xdg-mime >/dev/null 2>&1; then
    CURRENT_DEFAULT="$(xdg-mime query default application/x-reiwa-rbl 2>/dev/null || true)"
    [[ "$CURRENT_DEFAULT" == 'rblstudio-wx.desktop' ]] || SET_DEFAULT=0
  fi
fi

FILES_LIST="$STAGE/.rblstudio-install-files"
(cd "$STAGE" && find . -type f ! -name '.rblstudio-install-files' -printf '%P\n' | LC_ALL=C sort) > "$FILES_LIST"
printf 'schema=1\nrelease_status=UNRELEASED\nregistered=%s\nprevious_default=%s\ndata_home=%s\nconfig_home=%s\n' \
  "$(( (1 - NO_REGISTER) || HAD_OWN_REGISTRATION ))" "$PREVIOUS_DEFAULT" \
  "$STATE_DATA_HOME" "$STATE_CONFIG_HOME" > "$STAGE/.rblstudio-install-state"

if [[ -e "$INSTALL_DIR" ]]; then
  [[ -d "$INSTALL_DIR" && -f "$INSTALL_DIR/.rblstudio-install-files" ]] || {
    echo "Refusing to overwrite a non-RBLStudio directory: $INSTALL_DIR" >&2; exit 1;
  }
  BACKUP_DIR="$INSTALL_DIR.backup-$(date +%Y%m%d-%H%M%S)"
  [[ ! -e "$BACKUP_DIR" ]] || { echo "Backup path already exists: $BACKUP_DIR" >&2; exit 1; }
  BACKUP_MOVED=1
  mv -- "$INSTALL_DIR" "$BACKUP_DIR"
fi
INSTALL_COMMITTED=1
if ! mv -- "$STAGE" "$INSTALL_DIR"; then
  echo 'Could not commit staged installation; previous install restored when possible.' >&2
  exit 1
fi
RESOLVED_INSTALL_ROOT="$(realpath -e -- "$INSTALL_DIR")"
if [[ "$RESOLVED_INSTALL_ROOT" != "$INSTALL_DIR" ]]; then
  echo "Installed root resolved unexpectedly: $RESOLVED_INSTALL_ROOT" >&2
  exit 1
fi
for marker in VERSION rbl.py tools/rbltool.py compiler runtime toolchain tests/run_tests.py benchmarks/run_benchmarks.py; do
  if [[ ! -e "$INSTALL_DIR/$marker" ]]; then
    FAILED_DIR="$INSTALL_DIR.failed-$(date +%Y%m%d-%H%M%S)-$$"
    echo "Installed application root missing marker $marker; rollback requested." >&2
    exit 1
  fi
done

if ((NO_REGISTER == 0)); then
  mkdir -p "$APP_DIR" "$ICON_DIR" "$MIME_DIR"
  assert_no_symlink_components "$APP_DIR"
  assert_no_symlink_components "$ICON_DIR"
  assert_no_symlink_components "$MIME_DIR"
  for integration_file in "$DESKTOP" "$ICON" "$MIME_FILE"; do
    if [[ -L "$integration_file" ]]; then
      echo "Refusing to follow a symlink integration file: $integration_file" >&2
      exit 1
    fi
  done
  for integration_file in "$DESKTOP" "$ICON" "$MIME_FILE"; do
    if [[ -e "$integration_file" && ! -f "$integration_file" ]]; then
      echo "Refusing to replace a non-file integration path: $integration_file" >&2
      exit 1
    fi
  done
  if [[ -e "$MIME_FILE" ]] && ! grep -Fq 'RBLStudio wx integration' "$MIME_FILE"; then
    echo "Refusing to replace an unowned MIME package: $MIME_FILE" >&2
    exit 1
  fi

  DESKTOP_TARGET="$DESKTOP"
  ICON_TARGET="$ICON"
  MIME_TARGET="$MIME_FILE"
  INTEGRATION_BACKUP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/rblstudio-xdg-backup.XXXXXX")"
  backup_integration_file() {
    local name="$1" target="$2"
    if [[ -f "$target" ]]; then
      : > "$INTEGRATION_BACKUP_DIR/$name.present"
      cp -a -- "$target" "$INTEGRATION_BACKUP_DIR/$name.tmp"
      mv -- "$INTEGRATION_BACKUP_DIR/$name.tmp" "$INTEGRATION_BACKUP_DIR/$name"
      : > "$INTEGRATION_BACKUP_DIR/$name.ready"
    fi
  }
  backup_integration_file desktop "$DESKTOP"
  backup_integration_file icon "$ICON"
  backup_integration_file mime "$MIME_FILE"
  INTEGRATION_BACKUPS_COMPLETE=1

  DESKTOP_TMP="$(mktemp "$APP_DIR/.rblstudio-wx.desktop.XXXXXX")"
  MIME_TMP="$(mktemp "$MIME_DIR/.rewa-rbl-wx.xml.XXXXXX")"
  if [[ ! -e "$ICON" ]]; then
    ICON_TMP="$(mktemp "$ICON_DIR/.rblstudio-wx.png.XXXXXX")"
    cp -- "$ROOT/assets/rbl_256.png" "$ICON_TMP"
  fi
  cat > "$DESKTOP_TMP" <<EOF
[Desktop Entry]
Type=Application
Name=RBL Studio
Comment=IDE for Reiwa Bat Language
Exec="$INSTALL_DIR/RBLStudio.sh" %F
Icon=rblstudio-wx
Terminal=false
Categories=Development;IDE;
MimeType=application/x-reiwa-rbl;
StartupNotify=true
EOF
  cat > "$MIME_TMP" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <!-- RBLStudio wx integration -->
  <mime-type type="application/x-reiwa-rbl">
    <comment>Reiwa Bat Language source</comment>
    <glob pattern="*.rbl"/>
  </mime-type>
</mime-info>
EOF
  chmod 644 "$DESKTOP_TMP" "$MIME_TMP"
  [[ -z "$ICON_TMP" ]] || chmod 644 "$ICON_TMP"
  mv -f -- "$DESKTOP_TMP" "$DESKTOP"
  DESKTOP_TMP=""
  mv -f -- "$MIME_TMP" "$MIME_FILE"
  MIME_TMP=""
  if [[ -n "$ICON_TMP" ]]; then
    mv -f -- "$ICON_TMP" "$ICON"
    ICON_TMP=""
  fi
  command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$DATA_HOME/mime" || true
  if ((SET_DEFAULT == 1)) && command -v xdg-mime >/dev/null 2>&1; then xdg-mime default rblstudio-wx.desktop application/x-reiwa-rbl || true; fi
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true
fi

INSTALL_SUCCESS=1
echo "RBL Studio wxWidgets installed to: $INSTALL_DIR"
echo 'Release status: UNRELEASED (no legal-readiness claim or source archive is included).'
((NO_REGISTER == 0)) || echo 'Desktop/MIME integration was not changed (--no-register).'
[[ -z "$BACKUP_DIR" ]] || echo "Previous installation preserved at: $BACKUP_DIR"
