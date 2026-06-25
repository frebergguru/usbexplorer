/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_DIFF_UI_H
#define USBEXPLORER_GUI_DIFF_UI_H

#include <gtk/gtk.h>

#include "usb/enumerate.h"

/* Open a modal-ish window letting the user pick two devices from 'root' and
 * see their descriptor-level diff. */
void ue_diff_dialog_show(GtkWindow *parent, UsbNode *root);

#endif /* USBEXPLORER_GUI_DIFF_UI_H */
