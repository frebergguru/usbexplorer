/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_CLI_CLI_H
#define USBEXPLORER_CLI_CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "usb/enumerate.h"

/* A device filter built from --device VID:PID [--serial S]. */
typedef struct {
    bool        active;
    bool        have_vid_pid;
    uint16_t    vid, pid;
    const char *serial;   /* NULL if not specified; matched case-insensitively */
} DeviceFilter;

/* True if 'n' is a real device (NODE_DEVICE/HUB/ROOT_HUB) matching the filter.
 * A filter that is not active matches every device node. */
bool device_matches(const UsbNode *n, const DeviceFilter *f);

/* True for node kinds that represent an actual USB device (not the synthetic
 * root or a host controller). */
bool node_is_device(const UsbNode *n);

/* --- output modes (each writes to stdout) --- */
void cli_list(const UsbNode *root, const DeviceFilter *f);
void cli_tree(const UsbNode *root, const DeviceFilter *f, bool show_interfaces);
void cli_json(const UsbNode *root, const DeviceFilter *f, bool pretty);
void cli_xml(const UsbNode *root, const DeviceFilter *f);

/* Live hotplug event stream as JSON lines; runs until SIGINT/SIGTERM. */
void cli_watch(const DeviceFilter *f);

/* Filtered kernel-log / read-speed actions for one device, addressed as
 * "BUSNUM.DEVNUM" (e.g. "6.3").  Return a process exit status. */
int cli_dmesg(const UsbNode *root, const char *busdev);
int cli_speedtest(const UsbNode *root, const char *busdev);
int cli_latency(const UsbNode *root, const char *busdev);

/* Descriptor-level diff of two devices, each addressed as "bus.dev" or
 * "vid:pid", given together as "A,B". */
int cli_diff(const UsbNode *root, const char *arg);

/* Diff two device nodes, returning a heap string (caller frees). */
char *cli_diff_string(const UsbNode *a, const UsbNode *b);

/* Bluetooth inventory (BlueZ) and device removal by MAC. */
int cli_bluetooth(void);
int cli_bt_remove(const char *mac);

/* usbmon capture (only present when built with -Dusbmon). */
int cli_usbmon(const char *arg);        /* "BUS[.DEV]" live text capture */
int cli_usbmon_pcap(const char *arg);   /* "BUS:FILE" pcap export        */

#endif /* USBEXPLORER_CLI_CLI_H */
