/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/logpanel.h"

#include <glib-unix.h>

#include "usb/monitor.h"

struct UeLog {
    GtkWidget      *scrolled;
    GtkWidget      *view;
    GtkTextBuffer  *buf;
    UsbMonitor     *mon;
    guint           fd_source;
    gint64          last_us;       /* for delta-T */
    UeLogHotplugFn  hotplug_cb;
    gpointer        hotplug_data;
};

static void append_line(UeLog *l, const char *text)
{
    GtkTextIter it;
    gtk_text_buffer_get_end_iter(l->buf, &it);
    gtk_text_buffer_insert(l->buf, &it, text, -1);
    gtk_text_buffer_get_end_iter(l->buf, &it);
    gtk_text_buffer_insert(l->buf, &it, "\n", 1);

    /* Auto-scroll to the bottom. */
    GtkTextMark *mark = gtk_text_buffer_get_insert(l->buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(l->view), mark);
}

static gboolean on_monitor_ready(gint fd, GIOCondition cond, gpointer user)
{
    (void)fd; (void)cond;
    UeLog *l = user;

    UsbEvent e;
    while (usb_monitor_next(l->mon, &e)) {
        gint64 now = g_get_real_time();
        long long dms = l->last_us ? (now - l->last_us) / 1000 : 0;
        l->last_us = now;

        char ids[32] = "";
        if (e.has_ids)
            g_snprintf(ids, sizeof ids, "  %04x:%04x", e.vid, e.pid);

        char *line = g_strdup_printf("%+8lld ms  %-7s %-13s %s%s",
                                     dms, e.action, e.devtype, e.sysname, ids);
        append_line(l, line);
        g_free(line);

        /* Notify the owner once per whole-device arrival/removal. */
        if ((e.type == USB_EV_ADD || e.type == USB_EV_REMOVE) &&
            g_strcmp0(e.devtype, "usb_device") == 0 && l->hotplug_cb)
            l->hotplug_cb(&e, l->hotplug_data);
    }

    return G_SOURCE_CONTINUE;
}

UeLog *ue_log_new(UeLogHotplugFn cb, gpointer user_data)
{
    UeLog *l = g_new0(UeLog, 1);
    l->hotplug_cb = cb;
    l->hotplug_data = user_data;

    l->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(l->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(l->view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(l->view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(l->view), 6);
    l->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(l->view));

    l->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(l->scrolled), l->view);
    gtk_widget_set_size_request(l->scrolled, 320, -1);

    l->mon = usb_monitor_new();
    if (l->mon) {
        append_line(l, "# udev event log — watching for hotplug events");
        l->fd_source = g_unix_fd_add(usb_monitor_fd(l->mon), G_IO_IN,
                                     on_monitor_ready, l);
    } else {
        append_line(l, "# udev monitor unavailable");
    }
    return l;
}

GtkWidget *ue_log_widget(UeLog *l) { return l->scrolled; }

void ue_log_free(UeLog *l)
{
    if (!l)
        return;
    if (l->fd_source)
        g_source_remove(l->fd_source);
    usb_monitor_free(l->mon);
    g_free(l);
}
