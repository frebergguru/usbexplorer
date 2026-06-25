## Assets

Prebuilt system installs (CLI + ncurses TUI + GTK4 GUI, man page, `.desktop`,
icon, shell completions) for both architectures. Unpack and run `sudo ./install.sh`.

- **`usbexplorer-<ver>-x86_64.tar.gz`** — 64-bit Intel/AMD
- **`usbexplorer-<ver>-aarch64.tar.gz`** — 64-bit ARM

Built natively on Ubuntu 24.04 (glibc 2.39), so they run on 24.04-era and newer
distributions with gtk4/libadwaita installed.

Build from source on any modern distro with `meson setup build && ninja -C build`.

---
