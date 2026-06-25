/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_HISTORY_H
#define USBEXPLORER_USB_HISTORY_H

#include <stdint.h>
#include <stdio.h>

/*
 * Persistent connect/disconnect audit log backed by SQLite, stored under the
 * XDG data dir (usbexplorer/history.db).  Only built when -Dhistory is enabled.
 */

typedef struct UsbHistory UsbHistory;

/* Open (creating the DB + schema if needed). NULL on failure. */
UsbHistory *usb_history_open(void);
void        usb_history_close(UsbHistory *h);

/* Append one event (epoch seconds recorded automatically). */
void usb_history_record(UsbHistory *h, const char *action, uint16_t vid,
                        uint16_t pid, const char *serial, const char *product,
                        const char *devname);

/* Print the most recent 'limit' events (0 = all), newest first.
 * 'csv' selects CSV vs. a human-readable table.  Returns row count, or -1. */
int usb_history_dump(UsbHistory *h, int limit, int csv, FILE *out);

#endif /* USBEXPLORER_USB_HISTORY_H */
