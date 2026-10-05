#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"
MIME_DIR="${HOME}/.local/share/mime/packages"
mkdir -p "$APP_DIR" "$ICON_DIR" "$MIME_DIR"
cp "$ROOT/assets/rbl_256.png" "$ICON_DIR/rblstudio.png"
cat > "$APP_DIR/rblstudio.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=RBL Studio
Comment=IDE for Reiwa Bat Language
Exec=$ROOT/RBLStudio.sh %F
Icon=rblstudio
Terminal=false
Categories=Development;IDE;
MimeType=application/x-reiwa-rbl;
StartupNotify=true
EOF
cat > "$MIME_DIR/rewa-rbl.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-reiwa-rbl">
    <comment>Reiwa Bat Language source</comment>
    <glob pattern="*.rbl"/>
  </mime-type>
</mime-info>
EOF
if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "$HOME/.local/share/mime"; fi
if command -v xdg-mime >/dev/null 2>&1; then xdg-mime default rblstudio.desktop application/x-reiwa-rbl; fi
if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true; fi
printf 'RBL Studio installed. .rbl is associated with RBL Studio for this user.\n'
