/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_ACTIONS_H
#define USBEXPLORER_USB_ACTIONS_H

#include <stddef.h>

/*
 * Privileged device operations.  Each performs a single sysfs/ioctl action and
 * needs write access to the device (normally root).  When run unprivileged the
 * call fails with a clear, EACCES-aware message rather than aborting — the GUI
 * re-invokes usbexplorer through pkexec to obtain privilege.
 */

typedef enum {
    ACTION_RESET,            /* USBDEVFS_RESET ioctl (re-enumerate in place)    */
    ACTION_RESTART,          /* unbind + rebind the usb driver                  */
    ACTION_PORT_CYCLE,       /* de-authorise then re-authorise (power-cycle-ish) */
    ACTION_AUTOSUSPEND_ON,   /* power/control = auto                            */
    ACTION_AUTOSUSPEND_OFF,  /* power/control = on                              */
    ACTION_WAKEUP_ON,        /* power/wakeup = enabled                          */
    ACTION_WAKEUP_OFF,       /* power/wakeup = disabled                         */
} UsbActionType;

const char *usb_action_name(UsbActionType t);

/* Parse a "BUSNUM.DEVNUM" string. Returns 1 on success. */
int usb_action_parse_busdev(const char *s, int *bus, int *dev);

/* Perform the action on bus/dev.  Returns 0 on success; on failure returns -1
 * and fills 'errbuf'. */
int usb_action_perform(UsbActionType t, int bus, int dev, char *errbuf, size_t errsz);

#endif /* USBEXPLORER_USB_ACTIONS_H */
