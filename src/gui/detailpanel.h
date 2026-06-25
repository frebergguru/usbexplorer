/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_DETAILPANEL_H
#define USBEXPLORER_GUI_DETAILPANEL_H

#include <gtk/gtk.h>

#include "usb/enumerate.h"

typedef struct UeDetail UeDetail;

UeDetail  *ue_detail_new(void);
GtkWidget *ue_detail_widget(UeDetail *d);
void       ue_detail_show(UeDetail *d, UsbNode *node);  /* NULL clears */
void       ue_detail_free(UeDetail *d);

#endif /* USBEXPLORER_GUI_DETAILPANEL_H */
