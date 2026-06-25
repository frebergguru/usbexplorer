#!/bin/sh
# Remove a system-installed usbexplorer. Run as root (sudo ./uninstall.sh).
set -e
PREFIX="${PREFIX:-/usr}"

if [ "$(id -u)" -ne 0 ]; then
    echo "This uninstaller needs root. Try: sudo ./uninstall.sh" >&2
    exit 1
fi

rm -f "${PREFIX}/bin/usbexplorer"
rm -f "${PREFIX}/share/applications/usbexplorer.desktop"
rm -f "${PREFIX}/share/icons/hicolor/scalable/apps/usbexplorer.svg"
rm -f "${PREFIX}/share/man/man1/usbexplorer.1"
rm -f "${PREFIX}/share/bash-completion/completions/usbexplorer"
rm -f "${PREFIX}/share/zsh/site-functions/_usbexplorer"

command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
echo "usbexplorer removed from ${PREFIX}."
