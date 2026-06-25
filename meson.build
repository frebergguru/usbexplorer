project('usbexplorer', 'c',
  version : '0.1.0',
  license : 'GPL-2.0-or-later',
  meson_version : '>= 0.60.0',
  default_options : [
    'c_std=c11',
    'warning_level=3',      # -Wall -Wextra -Wpedantic
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')

# Expose _GNU_SOURCE for argp, strdup, realpath, etc.
add_project_arguments('-D_GNU_SOURCE', language : 'c')

# ---- Core dependencies (always required) ----
# include_type:'system' so third-party headers are -isystem and their own
# warnings never trip our -Wall -Wextra -Wpedantic gate.
libusb_dep = dependency('libusb-1.0', include_type : 'system')
libudev_dep = dependency('libudev', include_type : 'system')
libxml_dep  = dependency('libxml-2.0', include_type : 'system')
gio_dep     = dependency('gio-2.0', include_type : 'system')   # BlueZ D-Bus

# argp lives in glibc; on musl/BSD it needs -largp.
argp_dep = []
if not cc.has_function('argp_parse')
  argp_lib = cc.find_library('argp', required : false)
  if argp_lib.found()
    argp_dep = [argp_lib]
  else
    error('argp_parse not available; install argp-standalone')
  endif
endif

# ---- Optional feature dependencies ----
gtk_dep      = dependency('gtk4',         required : get_option('gui'),     include_type : 'system')
adwaita_dep  = dependency('libadwaita-1', required : get_option('gui'),     include_type : 'system')
notify_dep   = dependency('libnotify',    required : get_option('gui'),     include_type : 'system')
curses_dep   = dependency('ncursesw',     required : get_option('tui'),     include_type : 'system')
sqlite_dep   = dependency('sqlite3',      required : get_option('history'), include_type : 'system')

feat_gui     = gtk_dep.found() and adwaita_dep.found()
feat_tui     = curses_dep.found()
feat_history = sqlite_dep.found()
feat_usbmon  = not get_option('usbmon').disabled()

conf = configuration_data()
conf.set('USBIDS_PATH', '"/usr/share/hwdata/usb.ids"')
conf.set_quoted('GETTEXT_PACKAGE', 'usbexplorer')
conf.set_quoted('LOCALEDIR', get_option('prefix') / get_option('datadir') / 'locale')
conf.set10('HAVE_GUI', feat_gui)
conf.set10('HAVE_TUI', feat_tui)
conf.set10('HAVE_HISTORY', feat_history)
conf.set10('HAVE_USBMON', feat_usbmon)
conf.set_quoted('PROJECT_VERSION', meson.project_version())
configure_file(output : 'config.h', configuration : conf)

inc = include_directories('.', 'src')

subdir('src')
subdir('po')

# Install the man page, desktop entry, icon and shell completions.
install_man('man/usbexplorer.1')
install_data('data/usbexplorer.desktop', install_dir : get_option('datadir') / 'applications')
install_data('data/icons/usbexplorer.svg',
  install_dir : get_option('datadir') / 'icons/hicolor/scalable/apps')
install_data('completions/usbexplorer.bash',
  install_dir : get_option('datadir') / 'bash-completion/completions',
  rename : 'usbexplorer')
install_data('completions/_usbexplorer',
  install_dir : get_option('datadir') / 'zsh/site-functions')

summary({
  'GUI (GTK4)'      : feat_gui,
  'TUI (ncurses)'   : feat_tui,
  'History (SQLite)': feat_history,
  'usbmon capture'  : feat_usbmon,
}, section : 'Features')
