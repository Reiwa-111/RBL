#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ -f "$SCRIPT_DIR/.rblstudio-install-files" ]]; then
  INSTALL_DIR="$SCRIPT_DIR"
else
  INSTALL_DIR="${HOME}/.local/lib/rblstudio"
fi
NO_REGISTER=0
while (($#)); do
  case "$1" in
    --install-dir) (($# >= 2)) || { echo '--install-dir requires a path' >&2; exit 2; }; INSTALL_DIR="$2"; shift 2 ;;
    --no-register) NO_REGISTER=1; shift ;;
    -h|--help) echo 'Usage: uninstall_linux.sh [--install-dir PATH] [--no-register]'; exit 0 ;;
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
      echo "Uninstall path contains a symlink component: $current" >&2
      return 1
    fi
  done
}
[[ "$INSTALL_DIR" == /* ]] || INSTALL_DIR="$PWD/$INSTALL_DIR"
assert_no_symlink_components "$INSTALL_DIR"
INSTALL_DIR="$(realpath -m -- "$INSTALL_DIR")"
[[ "$INSTALL_DIR" != / ]] || { echo 'Refusing to uninstall from filesystem root.' >&2; exit 1; }
FILES_LIST="$INSTALL_DIR/.rblstudio-install-files"
STATE_FILE="$INSTALL_DIR/.rblstudio-install-state"
[[ ! -L "$FILES_LIST" && ! -L "$STATE_FILE" ]] || { echo 'Install manifest files must not be symlinks.' >&2; exit 1; }
if [[ ! -f "$FILES_LIST" ]]; then
  echo "No RBL Studio install manifest found at: $INSTALL_DIR"
  exit 0
fi

REGISTERED=0
PREVIOUS_DEFAULT=""
DATA_HOME=""
CONFIG_HOME=""
DATA_HOME_STORED=0
CONFIG_HOME_STORED=0
if [[ -f "$STATE_FILE" ]]; then
  while IFS='=' read -r key value; do
    case "$key" in
      registered) REGISTERED="$value" ;;
      previous_default) PREVIOUS_DEFAULT="$value" ;;
      data_home) DATA_HOME="$value"; DATA_HOME_STORED=1 ;;
      config_home) CONFIG_HOME="$value"; CONFIG_HOME_STORED=1 ;;
    esac
  done < "$STATE_FILE"
fi
if [[ ! -f "$STATE_FILE" ]] || ! grep -qx 'schema=1' "$STATE_FILE" || ! grep -qx 'release_status=UNRELEASED' "$STATE_FILE"; then
  echo 'Invalid or unsupported RBL Studio install state manifest.' >&2
  exit 1
fi
case "$REGISTERED" in 0|1) ;; *) echo 'Invalid registered flag in install state.' >&2; exit 1 ;; esac

if ((REGISTERED == 1)); then
  if ((DATA_HOME_STORED == 0)); then DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"; fi
  if ((CONFIG_HOME_STORED == 0)); then CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"; fi
  [[ "$DATA_HOME" == /* && "$CONFIG_HOME" == /* ]] || {
    echo 'Stored XDG data/config paths must be absolute.' >&2; exit 1;
  }
  if ((DATA_HOME_STORED == 1)); then assert_no_symlink_components "$DATA_HOME"; fi
  if ((CONFIG_HOME_STORED == 1)); then assert_no_symlink_components "$CONFIG_HOME"; fi
  DATA_HOME="$(realpath -m -- "$DATA_HOME")"
  CONFIG_HOME="$(realpath -m -- "$CONFIG_HOME")"
  assert_no_symlink_components "$DATA_HOME"
  assert_no_symlink_components "$CONFIG_HOME"
  export XDG_DATA_HOME="$DATA_HOME"
  export XDG_CONFIG_HOME="$CONFIG_HOME"
  APP_DIR="$DATA_HOME/applications"
  ICON_DIR="$DATA_HOME/icons/hicolor/256x256/apps"
  MIME_DIR="$DATA_HOME/mime/packages"
  for integration_dir in "$APP_DIR" "$ICON_DIR" "$MIME_DIR" "$DATA_HOME/mime"; do
    assert_no_symlink_components "$integration_dir"
  done
  DESKTOP="$APP_DIR/rblstudio-wx.desktop"
  ICON="$ICON_DIR/rblstudio-wx.png"
  MIME_FILE="$MIME_DIR/rewa-rbl-wx.xml"
  for integration_file in "$DESKTOP" "$ICON" "$MIME_FILE" \
      "$CONFIG_HOME/mimeapps.list" "$APP_DIR/mimeapps.list"; do
    if [[ -L "$integration_file" ]]; then
      echo "Refusing to follow a symlink integration file: $integration_file" >&2
      exit 1
    fi
  done
fi

if ((NO_REGISTER == 1 && REGISTERED == 1)); then
  echo 'This install owns desktop/MIME integration; refusing --no-register because it would leave broken integration entries.' >&2
  exit 1
fi

mapfile -t MANIFEST_FILES < "$FILES_LIST"
if ((${#MANIFEST_FILES[@]} == 0)); then
  echo 'Install file manifest is empty; refusing to uninstall.' >&2
  exit 1
fi
declare -A OWNED_DIRS=()
for relative in "${MANIFEST_FILES[@]}"; do
  [[ -n "$relative" ]] || continue
  case "$relative" in
    /*|.|./*|../*|*/../*|*/..|*/./*|*/.|*//*|*\\*) echo "Refusing unsafe manifest path: $relative" >&2; exit 1 ;;
  esac
  target="$INSTALL_DIR/$relative"
  assert_no_symlink_components "$target"
  resolved_target="$(realpath -m -- "$target")"
  case "$resolved_target" in "$INSTALL_DIR"/*) ;; *) echo "Manifest path escapes install root: $relative" >&2; exit 1 ;; esac
  parent="$(dirname "$relative")"
  while [[ "$parent" != . && "$parent" != / ]]; do
    OWNED_DIRS["$parent"]=1
    parent="$(dirname "$parent")"
  done
done

for relative in "${MANIFEST_FILES[@]}"; do
  [[ -n "$relative" ]] || continue
  target="$INSTALL_DIR/$relative"
  assert_no_symlink_components "$target"
  [[ ! -f "$target" ]] || rm -f -- "$target"
done
rm -f -- "$FILES_LIST" "$STATE_FILE"

if ((NO_REGISTER == 0 && REGISTERED == 1)); then
  APP_DIR="$DATA_HOME/applications"
  ICON_DIR="$DATA_HOME/icons/hicolor/256x256/apps"
  MIME_DIR="$DATA_HOME/mime/packages"
  DESKTOP="$APP_DIR/rblstudio-wx.desktop"
  ICON="$ICON_DIR/rblstudio-wx.png"
  MIME_FILE="$MIME_DIR/rewa-rbl-wx.xml"
  if [[ -f "$DESKTOP" ]] && grep -Fq "$INSTALL_DIR/RBLStudio.sh" "$DESKTOP"; then
    rm -f -- "$DESKTOP"
    rm -f -- "$ICON"
  fi
  if [[ -f "$MIME_FILE" ]] && grep -Fq 'RBLStudio wx integration' "$MIME_FILE"; then rm -f -- "$MIME_FILE"; fi
  CURRENT_DEFAULT=""
  if command -v xdg-mime >/dev/null 2>&1; then
    CURRENT_DEFAULT="$(xdg-mime query default application/x-reiwa-rbl 2>/dev/null || true)"
  fi
  if [[ "$CURRENT_DEFAULT" == 'rblstudio-wx.desktop' && -n "$PREVIOUS_DEFAULT" ]] && command -v xdg-mime >/dev/null 2>&1; then
    xdg-mime default "$PREVIOUS_DEFAULT" application/x-reiwa-rbl || true
  fi
  for mimeapps in "$CONFIG_HOME/mimeapps.list" "$DATA_HOME/applications/mimeapps.list"; do
    [[ -f "$mimeapps" ]] || continue
    temporary="$mimeapps.rblstudio-tmp.$$"
    awk '
      /^\[/ { section = $0 }
      /^application\/x-reiwa-rbl=/ && section == "[Added Associations]" {
        value = substr($0, index($0, "=") + 1)
        count = split(value, entries, ";")
        kept = ""
        for (i = 1; i <= count; i++) if (entries[i] != "" && entries[i] != "rblstudio-wx.desktop") kept = kept (kept == "" ? "" : ";") entries[i]
        if (kept != "") print "application/x-reiwa-rbl=" kept
        next
      }
      /^application\/x-reiwa-rbl=/ && section == "[Default Applications]" {
        value = substr($0, index($0, "=") + 1)
        count = split(value, entries, ";")
        kept = ""
        for (i = 1; i <= count; i++) if (entries[i] != "" && entries[i] != "rblstudio-wx.desktop") kept = kept (kept == "" ? "" : ";") entries[i]
        if (kept != "") print "application/x-reiwa-rbl=" kept
        next
      }
      { print }
    ' "$mimeapps" > "$temporary"
    mv -f -- "$temporary" "$mimeapps"
  done
  command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$DATA_HOME/mime" || true
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true
fi

# Prune only directories implied by manifest-owned files. Unrelated empty
# directories under the selected program root are left untouched.
while IFS= read -r directory; do
  [[ -n "$directory" ]] || continue
  [[ ! -d "$INSTALL_DIR/$directory" ]] || rmdir -- "$INSTALL_DIR/$directory" 2>/dev/null || true
done < <(printf '%s\n' "${!OWNED_DIRS[@]}" | awk 'NF' | awk '{ print length, $0 }' | sort -rn | cut -d' ' -f2-)
rmdir -- "$INSTALL_DIR" 2>/dev/null || true
echo 'RBL Studio wxWidgets program files removed. User settings, workspaces and backups were not touched.'
[[ ! -d "$INSTALL_DIR" ]] || echo "Untracked user files remain in: $INSTALL_DIR"
