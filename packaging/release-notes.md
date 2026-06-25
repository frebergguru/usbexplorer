## Assets

- **`usbexplorer-<ver>-x86_64.tar.gz`** — prebuilt system install (CLI + ncurses
  TUI + GTK4 GUI, man page, `.desktop`, icon, shell completions). Unpack and run
  `sudo ./install.sh`. Built on Ubuntu 24.04 (glibc 2.39), so it runs on
  24.04-era and newer distributions with gtk4/libadwaita installed.

Build from source on any modern distro with `meson setup build && ninja -C build`.

---
