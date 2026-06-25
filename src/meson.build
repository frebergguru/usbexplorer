# Core backend + utilities + CLI. (TUI/GUI added in later milestones.)
core_sources = files(
  'usb/enumerate.c',
  'usb/descriptors.c',
  'usb/monitor.c',
  'usb/badusb.c',
  'usb/speedtest.c',
  'usb/actions.c',
  'usb/bluetooth.c',
  'usb/report.c',
  'util/sysfs.c',
  'util/usbids.c',
  'util/pciids.c',
  'util/i18n.c',
  'util/json.c',
  'util/xml.c',
  'util/udev_rule.c',
  'cli/cli_list.c',
  'cli/cli_json.c',
  'cli/cli_xml.c',
  'cli/cli_watch.c',
  'cli/cli_dmesg.c',
  'cli/cli_speedtest.c',
  'cli/cli_diff.c',
  'cli/cli_bluetooth.c',
  'cli/cli_latency.c',
)

core_deps = [libusb_dep, libudev_dep, libxml_dep, gio_dep] + argp_dep

tui_sources = []
extra_deps = []
if feat_tui
  tui_sources = files(
    'tui/tui_main.c',
    'tui/tui_tree.c',
    'tui/tui_detail.c',
    'tui/tui_search.c',
    'tui/tui_commands.c',
  )
  extra_deps += curses_dep
endif

usbmon_sources = []
if feat_usbmon
  usbmon_sources = files('usb/usbmon.c', 'cli/cli_usbmon.c')
endif

history_sources = []
if feat_history
  history_sources = files('usb/history.c')
  extra_deps += sqlite_dep
endif

gui_sources = []
if feat_gui
  gui_sources = files(
    'gui/app.c',
    'gui/mainwindow.c',
    'gui/treeview.c',
    'gui/detailpanel.c',
    'gui/logpanel.c',
    'gui/nodeobject.c',
    'gui/css.c',
    'gui/prefs.c',
    'gui/diff_ui.c',
    'gui/bluetooth_ui.c',
    'util/notify.c',
  )
  extra_deps += [gtk_dep, adwaita_dep, notify_dep]
endif

usbexplorer = executable('usbexplorer',
  ['main.c'] + core_sources + tui_sources + gui_sources + history_sources + usbmon_sources,
  include_directories : inc,
  dependencies : core_deps + extra_deps,
  install : true,
)
