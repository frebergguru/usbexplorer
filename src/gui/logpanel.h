/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_LOGPANEL_H
#define USBEXPLORER_GUI_LOGPANEL_H

#include <gtk/gtk.h>

#include "usb/monitor.h"

typedef struct UeLog UeLog;

/* Called on the GLib main thread for each whole-device add/remove, so the
 * owner can refresh the tree and (optionally) raise a notification. */
typedef void (*UeLogHotplugFn)(const UsbEvent *e, gpointer user_data);

UeLog     *ue_log_new(UeLogHotplugFn cb, gpointer user_data);
GtkWidget *ue_log_widget(UeLog *l);
void       ue_log_free(UeLog *l);

#endif /* USBEXPLORER_GUI_LOGPANEL_H */
