/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/nodeobject.h"

struct _UeNode {
    GObject   parent_instance;
    UsbNode  *node;   /* borrowed, not owned */
};

G_DEFINE_FINAL_TYPE(UeNode, ue_node, G_TYPE_OBJECT)

static void ue_node_class_init(UeNodeClass *klass) { (void)klass; }
static void ue_node_init(UeNode *self) { (void)self; }

UeNode *ue_node_new(UsbNode *node)
{
    UeNode *self = g_object_new(UE_TYPE_NODE, NULL);
    self->node = node;
    return self;
}

UsbNode *ue_node_get(UeNode *self)
{
    return self ? self->node : NULL;
}

GListModel *ue_node_create_child_model(gpointer item, gpointer user_data)
{
    (void)user_data;
    UsbNode *n = ue_node_get(UE_NODE(item));
    if (!n || n->n_children == 0)
        return NULL;

    GListStore *store = g_list_store_new(UE_TYPE_NODE);
    for (size_t i = 0; i < n->n_children; i++) {
        UeNode *child = ue_node_new(n->children[i]);
        g_list_store_append(store, child);
        g_object_unref(child);
    }
    return G_LIST_MODEL(store);
}
