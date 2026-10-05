#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.local/share/applications/rblstudio.desktop"
rm -f "$HOME/.local/share/icons/hicolor/256x256/apps/rblstudio.png"
rm -f "$HOME/.local/share/mime/packages/rewa-rbl.xml"
if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "$HOME/.local/share/mime"; fi
printf 'RBL Studio user integration removed.\n'
