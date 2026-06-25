/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_REPORT_H
#define USBEXPLORER_USB_REPORT_H

#include "usb/enumerate.h"

/*
 * Shared, frontend-agnostic text builders.  Each returns a freshly malloc'd,
 * NUL-terminated string (caller frees) so the CLI can print it, the TUI can
 * show it in an overlay, and the GUI can drop it into a dialog — guaranteeing
 * the three frontends present identical content.
 */

char *report_latency(const UsbNode *n);                 /* endpoint poll rates */
char *report_dmesg(const UsbNode *n);                   /* filtered /dev/kmsg  */
char *report_speedtest(const UsbNode *n, long max_bytes); /* runs the test     */
char *report_bluetooth(void);                           /* BlueZ inventory     */
char *report_history(int csv);                          /* SQLite audit log    */

#endif /* USBEXPLORER_USB_REPORT_H */
