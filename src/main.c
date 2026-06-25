/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/actions.h"
#include "usb/enumerate.h"
#include "util/i18n.h"
#include "util/pciids.h"
#include "util/udev_rule.h"
#include "util/usbids.h"
#include "config.h"

#if HAVE_TUI
#include "tui/tui.h"
#endif
#if HAVE_GUI
#include "gui/app.h"
#endif
#if HAVE_HISTORY
#include "usb/history.h"
#endif

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *argp_program_version = "usbexplorer " PROJECT_VERSION;
const char *argp_program_bug_address = "<https://github.com/usbexplorer/usbexplorer/issues>";

static char doc[] =
    "usbexplorer -- inspect the USB device tree.\v"
    "With no mode option, prints the device tree (--tree).";

typedef enum {
    MODE_TREE, MODE_LIST, MODE_JSON, MODE_XML,
    MODE_WATCH, MODE_UDEV_RULE, MODE_DMESG, MODE_SPEEDTEST,
    MODE_TUI, MODE_GUI,
    MODE_RESET, MODE_RESTART, MODE_PORT_CYCLE,
    MODE_AUTOSUSPEND, MODE_WAKEUP,
    MODE_HISTORY, MODE_HISTORY_CSV,
    MODE_DIFF,
    MODE_BLUETOOTH, MODE_BT_REMOVE,
    MODE_USBMON, MODE_USBMON_PCAP,
    MODE_LATENCY,
} Mode;

typedef struct {
    Mode         mode;
    DeviceFilter filter;
    bool         show_interfaces;
    bool         compact;
    const char  *mode_arg;   /* argument for udev-rule/dmesg/speed-test */
    uint16_t     arg_vid, arg_pid;
} Options;

/* Option keys for long-only options (no short form). */
enum {
    KEY_WATCH = 0x100,
    KEY_UDEV_RULE,
    KEY_DMESG,
    KEY_SPEEDTEST,
    KEY_TUI,
    KEY_GUI,
    KEY_RESET,
    KEY_RESTART,
    KEY_PORT_CYCLE,
    KEY_AUTOSUSPEND,
    KEY_WAKEUP,
    KEY_HISTORY,
    KEY_HISTORY_CSV,
    KEY_DIFF,
    KEY_BLUETOOTH,
    KEY_BT_REMOVE,
    KEY_USBMON,
    KEY_USBMON_PCAP,
    KEY_LATENCY,
};

static struct argp_option options[] = {
    { "list",       'l', 0,        0, "One line per device (like lsusb)", 0 },
    { "tree",       't', 0,        0, "ASCII topology tree (default)", 0 },
    { "json",       'j', 0,        0, "Full decoded descriptor tree as JSON", 0 },
    { "xml",        'x', 0,        0, "Full device tree as an XML report", 0 },
    { "gui",        KEY_GUI, 0,    0, "Launch the GTK4 graphical interface", 0 },
    { "tui",        KEY_TUI, 0,    0, "Launch the interactive ncurses TUI", 0 },
    { "watch",      KEY_WATCH, 0,  0, "Stream hotplug events as JSON lines", 0 },
    { "udev-rule",  KEY_UDEV_RULE, "VID:PID", 0, "Print a udev rule for a device", 0 },
    { "dmesg",      KEY_DMESG, "BUSNUM.DEVNUM", 0, "Print kernel-log lines for a device", 0 },
    { "history",    KEY_HISTORY, 0, 0, "Show the connect/disconnect audit log", 0 },
    { "history-csv", KEY_HISTORY_CSV, 0, 0, "Dump the audit log as CSV", 0 },
    { "diff",       KEY_DIFF, "A,B", 0, "Diff two devices' descriptors (bus.dev or vid:pid)", 0 },
    { "bluetooth",  KEY_BLUETOOTH, 0, 0, "List Bluetooth adapters and devices (BlueZ)", 0 },
    { "bt-remove",  KEY_BT_REMOVE, "MAC", 0, "Remove/forget a Bluetooth device", 0 },
    { "usbmon",     KEY_USBMON, "BUSNUM[.DEVNUM]", 0, "Live usbmon text capture (needs root)", 0 },
    { "usbmon-pcap", KEY_USBMON_PCAP, "BUSNUM:FILE", 0, "Capture usbmon packets to a pcap file", 0 },
    { "speed-test", KEY_SPEEDTEST, "BUSNUM.DEVNUM", 0, "Sequential read-speed test (storage)", 0 },
    { "latency",    KEY_LATENCY, "BUSNUM.DEVNUM", 0, "Interrupt-endpoint poll-rate analysis", 0 },
    { 0, 0, 0, 0, "Device actions (need root; the GUI uses pkexec):", 3 },
    { "reset-device",   KEY_RESET,      "BUSNUM.DEVNUM", 0, "Reset the device (USBDEVFS_RESET)", 3 },
    { "restart-device", KEY_RESTART,    "BUSNUM.DEVNUM", 0, "Unbind then rebind the usb driver", 3 },
    { "port-cycle",     KEY_PORT_CYCLE, "BUSNUM.DEVNUM", 0, "De-authorise then re-authorise the device", 3 },
    { "autosuspend",    KEY_AUTOSUSPEND, "on|off:BUSNUM.DEVNUM", 0, "Toggle runtime autosuspend", 3 },
    { "wakeup",         KEY_WAKEUP,      "on|off:BUSNUM.DEVNUM", 0, "Toggle remote-wakeup", 3 },
    { 0, 0, 0, 0, "Filtering:", 1 },
    { "device",     'd', "VID:PID", 0, "Restrict output to matching device(s)", 1 },
    { "serial",     's', "SERIAL",  0, "Further restrict by serial number", 1 },
    { 0, 0, 0, 0, "Formatting:", 2 },
    { "interfaces", 'i', 0,        0, "Show interfaces in --tree output", 2 },
    { "compact",    'c', 0,        0, "Compact (non-pretty) --json output", 2 },
    { 0 },
};

static bool parse_vid_pid(const char *arg, uint16_t *vid, uint16_t *pid)
{
    unsigned int v, p;
    if (sscanf(arg, "%x:%x", &v, &p) != 2 || v > 0xFFFF || p > 0xFFFF)
        return false;
    *vid = (uint16_t)v;
    *pid = (uint16_t)p;
    return true;
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    Options *o = state->input;
    switch (key) {
    case 'l': o->mode = MODE_LIST; break;
    case 't': o->mode = MODE_TREE; break;
    case 'j': o->mode = MODE_JSON; break;
    case 'x': o->mode = MODE_XML; break;
    case 'i': o->show_interfaces = true; break;
    case 'c': o->compact = true; break;
    case KEY_TUI: o->mode = MODE_TUI; break;
    case KEY_GUI: o->mode = MODE_GUI; break;
    case KEY_RESET: o->mode = MODE_RESET; o->mode_arg = arg; break;
    case KEY_RESTART: o->mode = MODE_RESTART; o->mode_arg = arg; break;
    case KEY_PORT_CYCLE: o->mode = MODE_PORT_CYCLE; o->mode_arg = arg; break;
    case KEY_AUTOSUSPEND: o->mode = MODE_AUTOSUSPEND; o->mode_arg = arg; break;
    case KEY_WAKEUP: o->mode = MODE_WAKEUP; o->mode_arg = arg; break;
    case KEY_HISTORY: o->mode = MODE_HISTORY; break;
    case KEY_HISTORY_CSV: o->mode = MODE_HISTORY_CSV; break;
    case KEY_DIFF: o->mode = MODE_DIFF; o->mode_arg = arg; break;
    case KEY_BLUETOOTH: o->mode = MODE_BLUETOOTH; break;
    case KEY_BT_REMOVE: o->mode = MODE_BT_REMOVE; o->mode_arg = arg; break;
    case KEY_USBMON: o->mode = MODE_USBMON; o->mode_arg = arg; break;
    case KEY_USBMON_PCAP: o->mode = MODE_USBMON_PCAP; o->mode_arg = arg; break;
    case KEY_LATENCY: o->mode = MODE_LATENCY; o->mode_arg = arg; break;
    case KEY_WATCH: o->mode = MODE_WATCH; break;
    case KEY_DMESG: o->mode = MODE_DMESG; o->mode_arg = arg; break;
    case KEY_SPEEDTEST: o->mode = MODE_SPEEDTEST; o->mode_arg = arg; break;
    case KEY_UDEV_RULE:
        o->mode = MODE_UDEV_RULE;
        if (!parse_vid_pid(arg, &o->arg_vid, &o->arg_pid))
            argp_error(state, "invalid --udev-rule value '%s' (expected VID:PID)", arg);
        break;
    case 'd': {
        if (!parse_vid_pid(arg, &o->filter.vid, &o->filter.pid))
            argp_error(state, "invalid --device value '%s' (expected VID:PID)", arg);
        o->filter.active = true;
        o->filter.have_vid_pid = true;
        break;
    }
    case 's':
        o->filter.active = true;
        o->filter.serial = arg;
        break;
    case ARGP_KEY_ARG:
        argp_error(state, "unexpected argument '%s'", arg);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, NULL, doc, NULL, NULL, NULL };

int main(int argc, char **argv)
{
    ue_i18n_init();

    Options o = { .mode = MODE_TREE };
    argp_parse(&argp, argc, argv, 0, NULL, &o);

    /* Privileged actions act directly on sysfs/usbfs; no tree needed. */
    if (o.mode == MODE_RESET || o.mode == MODE_RESTART || o.mode == MODE_PORT_CYCLE ||
        o.mode == MODE_AUTOSUSPEND || o.mode == MODE_WAKEUP) {
        UsbActionType act;
        const char *busdev = o.mode_arg;
        char argbuf[64];
        if (o.mode == MODE_AUTOSUSPEND || o.mode == MODE_WAKEUP) {
            /* "on:BUS.DEV" / "off:BUS.DEV" */
            const char *colon = strchr(o.mode_arg, ':');
            bool on = strncmp(o.mode_arg, "on", 2) == 0;
            if (!colon) {
                fprintf(stderr, "usbexplorer: expected on|off:BUSNUM.DEVNUM\n");
                return EXIT_FAILURE;
            }
            snprintf(argbuf, sizeof argbuf, "%s", colon + 1);
            busdev = argbuf;
            act = o.mode == MODE_AUTOSUSPEND
                      ? (on ? ACTION_AUTOSUSPEND_ON : ACTION_AUTOSUSPEND_OFF)
                      : (on ? ACTION_WAKEUP_ON : ACTION_WAKEUP_OFF);
        } else {
            act = o.mode == MODE_RESET ? ACTION_RESET
                : o.mode == MODE_RESTART ? ACTION_RESTART : ACTION_PORT_CYCLE;
        }
        int bus, dev;
        if (!usb_action_parse_busdev(busdev, &bus, &dev)) {
            fprintf(stderr, "usbexplorer: expected BUSNUM.DEVNUM (e.g. 6.3)\n");
            return EXIT_FAILURE;
        }
        char err[256];
        if (usb_action_perform(act, bus, dev, err, sizeof err) != 0) {
            fprintf(stderr, "usbexplorer: %s failed: %s\n", usb_action_name(act), err);
            return EXIT_FAILURE;
        }
        printf("usbexplorer: %s on bus %d device %d succeeded\n",
               usb_action_name(act), bus, dev);
        return EXIT_SUCCESS;
    }

    /* Bluetooth talks to BlueZ over D-Bus; no USB tree needed. */
    if (o.mode == MODE_BLUETOOTH)
        return cli_bluetooth();
    if (o.mode == MODE_BT_REMOVE)
        return cli_bt_remove(o.mode_arg);

    /* usbmon capture (no USB tree needed). */
    if (o.mode == MODE_USBMON || o.mode == MODE_USBMON_PCAP) {
#if HAVE_USBMON
        return o.mode == MODE_USBMON ? cli_usbmon(o.mode_arg)
                                     : cli_usbmon_pcap(o.mode_arg);
#else
        fprintf(stderr, "usbexplorer: built without usbmon support\n");
        return EXIT_FAILURE;
#endif
    }

    /* Device history is read straight from the SQLite log. */
    if (o.mode == MODE_HISTORY || o.mode == MODE_HISTORY_CSV) {
#if HAVE_HISTORY
        UsbHistory *h = usb_history_open();
        if (!h) {
            fprintf(stderr, "usbexplorer: cannot open the history database\n");
            return EXIT_FAILURE;
        }
        usb_history_dump(h, 0, o.mode == MODE_HISTORY_CSV, stdout);
        usb_history_close(h);
        return EXIT_SUCCESS;
#else
        fprintf(stderr, "usbexplorer: built without history support "
                        "(reconfigure with -Dhistory=enabled)\n");
        return EXIT_FAILURE;
#endif
    }

    /* --watch does not need a tree snapshot; it streams live events. */
    if (o.mode == MODE_WATCH) {
        cli_watch(&o.filter);
        usbids_free();
        return EXIT_SUCCESS;
    }

    /* The TUI manages its own enumeration lifecycle. */
    if (o.mode == MODE_TUI) {
#if HAVE_TUI
        return tui_run();
#else
        fprintf(stderr, "usbexplorer: built without TUI support "
                        "(reconfigure with -Dtui=enabled)\n");
        return EXIT_FAILURE;
#endif
    }

    /* The GUI manages its own enumeration lifecycle. */
    if (o.mode == MODE_GUI) {
#if HAVE_GUI
        return gui_main();
#else
        fprintf(stderr, "usbexplorer: built without GUI support "
                        "(reconfigure with -Dgui=enabled)\n");
        return EXIT_FAILURE;
#endif
    }

    UsbNode *root = usb_enumerate();
    if (!root) {
        fprintf(stderr,
                "usbexplorer: cannot enumerate USB devices (is %s available?)\n",
                "/sys/bus/usb/devices");
        return EXIT_FAILURE;
    }

    int rc = EXIT_SUCCESS;
    switch (o.mode) {
    case MODE_LIST: cli_list(root, &o.filter); break;
    case MODE_TREE: cli_tree(root, &o.filter, o.show_interfaces); break;
    case MODE_JSON: cli_json(root, &o.filter, !o.compact); break;
    case MODE_XML:  cli_xml(root, &o.filter); break;
    case MODE_DIFF: rc = cli_diff(root, o.mode_arg); break;
    case MODE_LATENCY: rc = cli_latency(root, o.mode_arg); break;
    case MODE_DMESG: rc = cli_dmesg(root, o.mode_arg); break;
    case MODE_SPEEDTEST: rc = cli_speedtest(root, o.mode_arg); break;
    case MODE_UDEV_RULE:
        udev_rule_write(stdout, o.arg_vid, o.arg_pid,
                        usb_find_by_vidpid(root, o.arg_vid, o.arg_pid));
        break;
    case MODE_WATCH: break; /* handled above */
    case MODE_TUI: break;   /* handled above */
    case MODE_GUI: break;   /* handled above */
    case MODE_RESET: case MODE_RESTART: case MODE_PORT_CYCLE:
    case MODE_AUTOSUSPEND: case MODE_WAKEUP: break; /* handled above */
    case MODE_HISTORY: case MODE_HISTORY_CSV: break; /* handled above */
    case MODE_BLUETOOTH: case MODE_BT_REMOVE: break; /* handled above */
    case MODE_USBMON: case MODE_USBMON_PCAP: break; /* handled above */
    }

    usb_tree_free(root);
    usbids_free();
    pciids_free();
    return rc;
}
