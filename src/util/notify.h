/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_NOTIFY_H
#define USBEXPLORER_UTIL_NOTIFY_H

/*
 * Thin libnotify wrapper for desktop notifications (built only with the GUI).
 * All calls are safe no-ops if libnotify failed to initialise.
 */

void ue_notify_init(void);
void ue_notify_send(const char *summary, const char *body, const char *icon);
void ue_notify_uninit(void);

#endif /* USBEXPLORER_UTIL_NOTIFY_H */
