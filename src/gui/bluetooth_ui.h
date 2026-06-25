/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_BLUETOOTH_UI_H
#define USBEXPLORER_GUI_BLUETOOTH_UI_H

#include <gtk/gtk.h>

/* Open a window listing Bluetooth adapters and devices (BlueZ), with a
 * "Forget" action per device. */
void ue_bluetooth_dialog_show(GtkWindow *parent);

#endif /* USBEXPLORER_GUI_BLUETOOTH_UI_H */
