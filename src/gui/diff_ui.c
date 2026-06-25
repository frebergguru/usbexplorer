/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/diff_ui.h"

#include "cli/cli.h"
#include "util/i18n.h"
#include "util/usbids.h"

typedef struct {
    GPtrArray   *devices;   /* of UsbNode* (borrowed) */
    GtkDropDown *a, *b;
    GtkTextBuffer *buf;
} DiffUI;

static void collect(UsbNode *n, GPtrArray *out, GtkStringList *labels)
{
    if (n->kind == NODE_DEVICE || n->kind == NODE_HUB || n->kind == NODE_ROOT_HUB) {
        const char *pn = n->product[0] ? n->product : usbids_product(n->vid, n->pid);
        char *label = g_strdup_printf("%d.%d  %04x:%04x  %s", n->busnum, n->devnum,
                                      n->vid, n->pid, pn ? pn : "");
        gtk_string_list_append(labels, label);
        g_ptr_array_add(out, n);
        g_free(label);
    }
    for (size_t i = 0; i < n->n_children; i++)
        collect(n->children[i], out, labels);
}

static void on_compare(GtkButton *btn, gpointer user)
{
    (void)btn;
    DiffUI *d = user;
    guint ia = gtk_drop_down_get_selected(d->a);
    guint ib = gtk_drop_down_get_selected(d->b);
    if (ia >= d->devices->len || ib >= d->devices->len)
        return;
    UsbNode *na = g_ptr_array_index(d->devices, ia);
    UsbNode *nb = g_ptr_array_index(d->devices, ib);
    char *text = cli_diff_string(na, nb);
    gtk_text_buffer_set_text(d->buf, text ? text : "", -1);
    free(text);
}

static void on_destroy(GtkWidget *w, gpointer user)
{
    (void)w;
    DiffUI *d = user;
    g_ptr_array_free(d->devices, TRUE);
    g_free(d);
}

void ue_diff_dialog_show(GtkWindow *parent, UsbNode *root)
{
    DiffUI *d = g_new0(DiffUI, 1);
    d->devices = g_ptr_array_new();

    GtkStringList *labels = gtk_string_list_new(NULL);
    for (size_t i = 0; i < root->n_children; i++)
        collect(root->children[i], d->devices, labels);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), _("Compare devices"));
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 720, 560);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    d->a = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(g_object_ref(labels)), NULL));
    d->b = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(labels), NULL));
    gtk_widget_set_hexpand(GTK_WIDGET(d->a), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(d->b), TRUE);
    if (d->devices->len > 1)
        gtk_drop_down_set_selected(d->b, 1);
    GtkWidget *cmp = gtk_button_new_with_label(_("Compare"));
    g_signal_connect(cmp, "clicked", G_CALLBACK(on_compare), d);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(d->a));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(d->b));
    gtk_box_append(GTK_BOX(row), cmp);
    gtk_box_append(GTK_BOX(box), row);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    d->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);

    gtk_window_set_child(GTK_WINDOW(win), box);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), d);
    gtk_window_present(GTK_WINDOW(win));
}
