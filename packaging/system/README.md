# usbexplorer — prebuilt system install

This archive contains a prebuilt `usbexplorer` (the binary under `usr/bin` plus
its man page, `.desktop` entry, icon and shell completions under `usr/share`).

## Install

```sh
sudo ./install.sh            # installs into /usr  (override with PREFIX=/usr/local)
```

## Uninstall

```sh
sudo ./uninstall.sh
```

## Runtime dependencies

The binary is dynamically linked, so the target system needs the usual runtime
libraries (present on any modern GTK desktop):

- gtk4, libadwaita-1, libnotify
- libusb-1.0, libudev (systemd)
- sqlite3, libxml2, ncursesw, glib/gio
- `hwdata` (provides `/usr/share/hwdata/usb.ids` and `pci.ids` for name lookups)

If you prefer to build from source instead, see the top-level `README.md`
(`meson setup build && ninja -C build`).
