/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_NODEOBJECT_H
#define USBEXPLORER_GUI_NODEOBJECT_H

#include <gio/gio.h>

#include "usb/enumerate.h"

/*
 * A trivial GObject wrapper around a UsbNode pointer so the device tree can be
 * driven through GtkTreeListModel / GtkListView.  The wrapper does NOT own the
 * UsbNode (the tree is owned by the main window); it only borrows the pointer
 * for the lifetime of the model.
 */

G_BEGIN_DECLS

#define UE_TYPE_NODE (ue_node_get_type())
G_DECLARE_FINAL_TYPE(UeNode, ue_node, UE, NODE, GObject)

UeNode  *ue_node_new(UsbNode *node);
UsbNode *ue_node_get(UeNode *self);

/* Build a GListModel (GListStore of UeNode) for a node's device children, or
 * NULL if it has none.  Suitable as a GtkTreeListModelCreateModelFunc. */
GListModel *ue_node_create_child_model(gpointer item, gpointer user_data);

G_END_DECLS

#endif /* USBEXPLORER_GUI_NODEOBJECT_H */
