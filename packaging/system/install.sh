#!/bin/sh
# Install the prebuilt usbexplorer tree. Run as root (sudo ./install.sh).
set -e
PREFIX="${PREFIX:-/usr}"
here="$(cd "$(dirname "$0")" && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "This installer needs root. Try: sudo ./install.sh" >&2
    exit 1
fi

echo "Installing usbexplorer into ${PREFIX} …"
install -Dm0755 "${here}/usr/bin/usbexplorer" "${PREFIX}/bin/usbexplorer"
cp -a "${here}/usr/share/." "${PREFIX}/share/"

# Refresh desktop/icon caches when the tools are present (best effort).
command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -f "${PREFIX}/share/icons/hicolor" 2>/dev/null || true

echo "Done. Try:  usbexplorer --tree   |   usbexplorer --gui   |   usbexplorer --tui"
echo "Runtime needs: gtk4, libadwaita, libusb-1.0, libudev, libnotify, sqlite3,"
echo "libxml2, ncursesw, and hwdata (for /usr/share/hwdata/usb.ids)."
