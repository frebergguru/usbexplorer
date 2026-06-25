/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/bluetooth_ui.h"

#include "usb/bluetooth.h"
#include "util/i18n.h"

typedef struct {
    GtkWindow  *win;
    GtkWidget  *list;     /* GtkListBox */
    GtkWidget  *status;
} BtUI;

static void populate(BtUI *u);

typedef struct { BtUI *ui; char adapter[128]; char path[128]; char mac[24]; } RemoveCtx;

static void free_ctx(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static void on_forget(GtkButton *btn, gpointer user)
{
    (void)btn;
    RemoveCtx *c = user;
    char err[256];
    if (bt_remove_device(c->adapter, c->path, err, sizeof err) == 0) {
        gtk_label_set_text(GTK_LABEL(c->ui->status), _("Device removed."));
        populate(c->ui);  /* refresh */
    } else {
        gtk_label_set_text(GTK_LABEL(c->ui->status), err);
    }
}

static void clear_list(GtkWidget *list)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(list)))
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
}

static void populate(BtUI *u)
{
    clear_list(u->list);

    BtList *l = bt_enumerate();
    if (l->error[0]) {
        gtk_label_set_text(GTK_LABEL(u->status), l->error);
        bt_list_free(l);
        return;
    }

    char summary[256] = "";
    for (size_t i = 0; i < l->n_adapters; i++) {
        char one[96];
        g_snprintf(one, sizeof one, "%s%s (%s)", i ? ", " : "",
                   l->adapters[i].name, l->adapters[i].powered ? "on" : "off");
        g_strlcat(summary, one, sizeof summary);
    }
    char *st = g_strdup_printf(_("%zu adapter(s): %s — %zu device(s)"),
                               l->n_adapters, summary, l->n_devices);
    gtk_label_set_text(GTK_LABEL(u->status), st);
    g_free(st);

    for (size_t i = 0; i < l->n_devices; i++) {
        BtDevice *d = &l->devices[i];
        GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_top(rowbox, 4); gtk_widget_set_margin_bottom(rowbox, 4);
        gtk_widget_set_margin_start(rowbox, 8); gtk_widget_set_margin_end(rowbox, 8);

        char *txt = g_strdup_printf("%s  <small>%s</small>  %s%s%s",
                                    d->name[0] ? d->name : d->address, d->address,
                                    d->connected ? "🔗 " : "",
                                    d->paired ? "paired " : "",
                                    d->trusted ? "trusted" : "");
        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), txt);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_hexpand(lbl, TRUE);
        g_free(txt);

        GtkWidget *forget = gtk_button_new_with_label(_("Forget"));
        RemoveCtx *c = g_new0(RemoveCtx, 1);
        c->ui = u;
        g_strlcpy(c->adapter, d->adapter, sizeof c->adapter);
        g_strlcpy(c->path, d->path, sizeof c->path);
        g_strlcpy(c->mac, d->address, sizeof c->mac);
        g_signal_connect_data(forget, "clicked", G_CALLBACK(on_forget), c,
                              free_ctx, 0);

        gtk_box_append(GTK_BOX(rowbox), lbl);
        gtk_box_append(GTK_BOX(rowbox), forget);
        gtk_list_box_append(GTK_LIST_BOX(u->list), rowbox);
    }
    bt_list_free(l);
}

static void on_destroy(GtkWidget *w, gpointer user)
{
    (void)w;
    g_free(user);
}

void ue_bluetooth_dialog_show(GtkWindow *parent)
{
    BtUI *u = g_new0(BtUI, 1);

    GtkWidget *win = gtk_window_new();
    u->win = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), _("Bluetooth devices"));
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 460);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10); gtk_widget_set_margin_end(box, 10);

    u->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(u->status), 0.0f);
    gtk_widget_add_css_class(u->status, "dim-label");

    u->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(u->list), GTK_SELECTION_NONE);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), u->list);
    gtk_widget_set_vexpand(scroll, TRUE);

    gtk_box_append(GTK_BOX(box), u->status);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_window_set_child(GTK_WINDOW(win), box);

    populate(u);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), u);
    gtk_window_present(GTK_WINDOW(win));
}
