/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_MONITOR_H
#define USBEXPLORER_USB_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

/*
 * Thin wrapper over a libudev netlink monitor filtered to the "usb" subsystem.
 * The monitor exposes a pollable file descriptor; callers integrate it into
 * their own event loop (the CLI uses poll(); the GUI will bridge it to GLib).
 */

typedef enum {
    USB_EV_ADD,
    USB_EV_REMOVE,
    USB_EV_BIND,
    USB_EV_UNBIND,
    USB_EV_CHANGE,
    USB_EV_OTHER,
} UsbEventType;

typedef struct {
    UsbEventType type;
    char action[16];            /* raw udev action string             */
    char sysname[80];           /* "1-5" or "1-5:1.0"                 */
    char devtype[32];           /* "usb_device" / "usb_interface"     */
    char syspath[PATH_MAX];
    char driver[64];
    bool has_ids;               /* vid/pid valid (usb_device events)  */
    uint16_t vid, pid;
} UsbEvent;

typedef struct UsbMonitor UsbMonitor;

/* Create and start a monitor, or NULL on failure. */
UsbMonitor *usb_monitor_new(void);

/* File descriptor to poll for readability. */
int usb_monitor_fd(const UsbMonitor *m);

/* Receive the next pending event (non-blocking).  Returns true and fills
 * '*out' if an event was available, false otherwise. */
bool usb_monitor_next(UsbMonitor *m, UsbEvent *out);

void usb_monitor_free(UsbMonitor *m);

#endif /* USBEXPLORER_USB_MONITOR_H */
