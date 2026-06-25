/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/app.h"
#include "gui/css.h"
#include "gui/mainwindow.h"
#include "util/notify.h"

#include <adwaita.h>
#include <stdlib.h>
#include <string.h>

/* Drop the benign "Unable to acquire session bus … dbus-launch …" warning that
 * GTK/GLib emit when launched without a D-Bus session bus (SSH/headless). The
 * app degrades fine (no notifications/portals); every other log passes through. */
static GLogWriterOutput log_filter(GLogLevelFlags level, const GLogField *fields,
                                   gsize n_fields, gpointer user)
{
    for (gsize i = 0; i < n_fields; i++) {
        if (strcmp(fields[i].key, "MESSAGE") == 0 && fields[i].value &&
            strstr(fields[i].value, "Unable to acquire session bus"))
            return G_LOG_WRITER_HANDLED;
    }
    return g_log_writer_default(level, fields, n_fields, user);
}

static gboolean selftest_shot(gpointer win)
{
    g_action_group_activate_action(G_ACTION_GROUP(win), "screenshot", NULL);
    return G_SOURCE_REMOVE;
}

static gboolean selftest_quit(gpointer app)
{
    /* Destroy the window the way a real close would, so teardown (and our
     * cleanup handler) runs; the app then exits as its last window closes. */
    GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (win)
        gtk_window_close(win);   /* emits close-request, then destroys */
    else
        g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer user)
{
    (void)user;
    ue_css_init();
    GtkWidget *win = ue_main_window_new(ADW_APPLICATION(app));
    gtk_window_present(GTK_WINDOW(win));

    /* Headless CI / smoke test: open, settle, then quit on its own.  The env
     * value, if numeric, sets the delay in ms (default 900). */
    const char *st = g_getenv("USBEXPLORER_GUI_SELFTEST");
    if (st) {
        int ms = atoi(st);
        if (ms <= 0)
            ms = 900;
        /* If a screenshot path was requested, fire it once the tree is sized. */
        if (g_getenv("USBEXPLORER_SHOT"))
            g_timeout_add(ms / 2, selftest_shot, win);
        if (g_getenv("USBEXPLORER_DIFFTEST"))
            g_action_group_activate_action(G_ACTION_GROUP(win), "diff", NULL);
        if (g_getenv("USBEXPLORER_BTTEST"))
            g_action_group_activate_action(G_ACTION_GROUP(win), "bluetooth", NULL);
        const char *act = g_getenv("USBEXPLORER_ACT");
        if (act)
            g_action_group_activate_action(G_ACTION_GROUP(win), act, NULL);
        g_timeout_add(ms, selftest_quit, app);
    }
}

int gui_main(void)
{
    g_log_set_writer_func(log_filter, NULL, NULL);
    ue_notify_init();
    AdwApplication *app = adw_application_new("org.usbexplorer.UsbExplorer",
                                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    ue_notify_uninit();
    return status;
}
