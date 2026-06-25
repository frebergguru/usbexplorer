/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/treeview.h"
#include "gui/nodeobject.h"

#include "usb/badusb.h"
#include "util/pciids.h"
#include "util/usbids.h"

struct UeTree {
    GtkWidget          *scrolled;
    GtkListView        *list;
    GtkSingleSelection *selection;   /* current model (owns the tree list model) */
    UeTreeSelectFn      on_select;
    gpointer            on_select_data;
};

/* ---- icon + label for a node ---- */

/* Build a themed icon with a priority list of names, so it resolves on any
 * icon theme (Adwaita, Breeze, …): GTK tries each name until one is found.
 * Every list ends in widely-available generic fallbacks. */
static GIcon *icon_for_class(uint8_t cls, uint8_t proto)
{
    const char *names[7];
    int n = 0;
    switch (cls) {
    case 0x01:
        names[n++] = "audio-card-symbolic"; names[n++] = "audio-card"; break;
    case 0x02:
    case 0x0A:
        names[n++] = "network-wired-symbolic"; names[n++] = "network-wired"; break;
    case 0x03:
        if (proto == 2) { names[n++] = "input-mouse-symbolic"; names[n++] = "input-mouse"; }
        else { names[n++] = "input-keyboard-symbolic"; names[n++] = "input-keyboard"; }
        break;
    case 0x06:
    case 0x0E:
        names[n++] = "camera-web-symbolic"; names[n++] = "camera-web";
        names[n++] = "camera-video-symbolic"; break;
    case 0x07:
        names[n++] = "printer-symbolic"; names[n++] = "printer"; break;
    case 0x08:
        names[n++] = "drive-harddisk-symbolic"; names[n++] = "drive-harddisk"; break;
    case 0x09:
        names[n++] = "view-grid-symbolic"; names[n++] = "drive-multidisk-symbolic"; break;
    case 0x0B:
        names[n++] = "auth-sim-symbolic"; names[n++] = "media-flash-symbolic"; break;
    case 0xE0: /* wireless / Bluetooth — name differs widely between themes */
        names[n++] = "bluetooth-symbolic";
        names[n++] = "network-bluetooth-symbolic";
        names[n++] = "preferences-system-bluetooth-symbolic";
        names[n++] = "network-wireless-symbolic";
        break;
    default: break;
    }
    /* Generic last-resort fallbacks present in essentially every theme. */
    names[n++] = "drive-removable-media-symbolic";
    names[n++] = "media-removable";
    return g_themed_icon_new_from_names((char **)names, n);
}

static GIcon *icon_for_node(const UsbNode *n)
{
    if (n->kind == NODE_CONTROLLER) {
        const char *c[] = { "computer-symbolic", "computer" };
        return g_themed_icon_new_from_names((char **)c, 2);
    }
    if (n->kind == NODE_ROOT_HUB || n->kind == NODE_HUB) {
        const char *h[] = { "view-grid-symbolic", "drive-multidisk-symbolic",
                            "network-workgroup-symbolic" };
        return g_themed_icon_new_from_names((char **)h, 3);
    }
    /* Prefer the device class; fall back to the first interface's class. */
    uint8_t cls = n->dev_class, proto = n->dev_protocol;
    if (cls == 0 && n->n_interfaces) {
        cls = n->interfaces[0].cls;
        proto = n->interfaces[0].proto;
    }
    return icon_for_class(cls, proto);
}

/* Pango-markup label; caller frees with g_free(). */
static char *label_for_node(const UsbNode *n)
{
    const char *speed_color = "#4caf50";
    char *markup;

    if (n->kind == NODE_CONTROLLER) {
        const char *v = pciids_vendor(n->controller ? n->controller->pci_vendor : 0);
        char *esc = g_markup_escape_text(v ? v : "Host Controller", -1);
        markup = g_strdup_printf("<b>%s</b>  <small>%s</small>",
                                 esc, n->controller ? n->controller->pci_addr : "");
        g_free(esc);
        return markup;
    }

    const char *name = n->product[0] ? n->product
                     : (n->kind == NODE_ROOT_HUB ? n->devname
                        : (usbids_product(n->vid, n->pid) ? usbids_product(n->vid, n->pid)
                                                          : "device"));
    char *esc = g_markup_escape_text(name, -1);

    const char *warn = "";
    switch (badusb_worst(n)) {
    case FINDING_DANGER: warn = "<span foreground='#e53935'> ⚠</span>"; break;
    case FINDING_WARN:   warn = "<span foreground='#fb8c00'> ⚠</span>"; break;
    default: break;
    }

    if (n->kind == NODE_ROOT_HUB)
        markup = g_strdup_printf("%s  <span foreground='%s'><small>%s</small></span>",
                                 esc, speed_color, usb_speed_badge(n->speed));
    else
        markup = g_strdup_printf(
            "%s%s  <small>%04x:%04x</small>  "
            "<span foreground='%s'><small>%s</small></span>",
            esc, warn, n->vid, n->pid, speed_color, usb_speed_badge(n->speed));
    g_free(esc);
    return markup;
}

/* ---- list item factory ---- */

static void factory_setup(GtkSignalListItemFactory *f, GtkListItem *li, gpointer u)
{
    (void)f; (void)u;
    GtkWidget *expander = gtk_tree_expander_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *icon = gtk_image_new();
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_tree_expander_set_child(GTK_TREE_EXPANDER(expander), box);
    g_object_set_data(G_OBJECT(expander), "ue-icon", icon);
    g_object_set_data(G_OBJECT(expander), "ue-label", label);
    gtk_list_item_set_child(li, expander);
}

static void factory_bind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer u)
{
    (void)f; (void)u;
    GtkTreeListRow *row = gtk_list_item_get_item(li);
    GtkTreeExpander *exp = GTK_TREE_EXPANDER(gtk_list_item_get_child(li));
    gtk_tree_expander_set_list_row(exp, row);

    UeNode *obj = gtk_tree_list_row_get_item(row); /* transfer full */
    UsbNode *n = ue_node_get(obj);

    GtkImage *icon = g_object_get_data(G_OBJECT(exp), "ue-icon");
    GtkLabel *label = g_object_get_data(G_OBJECT(exp), "ue-label");
    GIcon *gicon = icon_for_node(n);
    gtk_image_set_from_gicon(icon, gicon);
    g_object_unref(gicon);
    char *markup = label_for_node(n);
    gtk_label_set_markup(label, markup);
    g_free(markup);

    g_object_unref(obj);
}

/* ---- selection ---- */

static void on_selection_changed(GObject *obj, GParamSpec *pspec, gpointer user)
{
    (void)pspec;
    UeTree *t = user;
    GtkTreeListRow *row = gtk_single_selection_get_selected_item(GTK_SINGLE_SELECTION(obj));
    UsbNode *node = NULL;
    if (row) {
        UeNode *o = gtk_tree_list_row_get_item(row);
        node = ue_node_get(o);
        g_object_unref(o);
    }
    if (t->on_select)
        t->on_select(node, t->on_select_data);
}

static GtkSingleSelection *build_selection(UsbNode *root)
{
    GListStore *roots = g_list_store_new(UE_TYPE_NODE);
    for (size_t i = 0; i < root->n_children; i++) {
        UeNode *o = ue_node_new(root->children[i]);
        g_list_store_append(roots, o);
        g_object_unref(o);
    }
    GtkTreeListModel *tlm = gtk_tree_list_model_new(
        G_LIST_MODEL(roots), FALSE, TRUE, ue_node_create_child_model, NULL, NULL);
    return gtk_single_selection_new(G_LIST_MODEL(tlm));
}

UeTree *ue_tree_new(UsbNode *root, UeTreeSelectFn cb, gpointer user_data)
{
    UeTree *t = g_new0(UeTree, 1);
    t->on_select = cb;
    t->on_select_data = user_data;

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), NULL);

    t->selection = build_selection(root);
    g_signal_connect(t->selection, "notify::selected-item",
                     G_CALLBACK(on_selection_changed), t);

    t->list = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(t->selection), factory));
    gtk_list_view_set_show_separators(t->list, FALSE);

    t->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(t->scrolled), GTK_WIDGET(t->list));
    gtk_widget_set_size_request(t->scrolled, 360, -1);
    return t;
}

GtkWidget *ue_tree_widget(UeTree *t) { return t->scrolled; }
GtkWidget *ue_tree_listview(UeTree *t) { return GTK_WIDGET(t->list); }

gboolean ue_tree_select_node(UeTree *t, const UsbNode *node)
{
    GListModel *model = G_LIST_MODEL(t->selection);
    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        GtkTreeListRow *row = g_list_model_get_item(model, i); /* transfer full */
        UeNode *o = gtk_tree_list_row_get_item(row);
        gboolean match = ue_node_get(o) == node;
        g_object_unref(o);
        g_object_unref(row);
        if (match) {
            gtk_single_selection_set_selected(t->selection, i);
            gtk_list_view_scroll_to(t->list, i, GTK_LIST_SCROLL_SELECT, NULL);
            return TRUE;
        }
    }
    return FALSE;
}

void ue_tree_set_root(UeTree *t, UsbNode *root)
{
    t->selection = build_selection(root);
    g_signal_connect(t->selection, "notify::selected-item",
                     G_CALLBACK(on_selection_changed), t);
    /* GtkListView takes its own ref; setting a new model drops the old one. */
    gtk_list_view_set_model(t->list, GTK_SELECTION_MODEL(t->selection));
    g_object_unref(t->selection);
}

void ue_tree_free(UeTree *t)
{
    g_free(t);
}
