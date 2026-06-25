/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_TREEVIEW_H
#define USBEXPLORER_GUI_TREEVIEW_H

#include <gtk/gtk.h>

#include "usb/enumerate.h"

/* Device-tree widget built on GtkTreeListModel + GtkListView. */

typedef void (*UeTreeSelectFn)(UsbNode *node, gpointer user_data);

typedef struct UeTree UeTree;

/* Create the tree for 'root' (the synthetic NODE_ROOT). 'cb' is invoked with
 * the newly selected device node (or NULL). */
UeTree    *ue_tree_new(UsbNode *root, UeTreeSelectFn cb, gpointer user_data);

/* The top-level widget (a GtkScrolledWindow) to pack into a container. */
GtkWidget *ue_tree_widget(UeTree *t);

/* The inner GtkListView (e.g. to attach a right-click gesture). */
GtkWidget *ue_tree_listview(UeTree *t);

/* Select the row for 'node' (autoexpand keeps every node visible) and scroll
 * to it.  Returns TRUE if found. */
gboolean   ue_tree_select_node(UeTree *t, const UsbNode *node);

/* Rebuild the model against a new tree (e.g. after a hotplug refresh). */
void       ue_tree_set_root(UeTree *t, UsbNode *root);

void       ue_tree_free(UeTree *t);

#endif /* USBEXPLORER_GUI_TREEVIEW_H */
