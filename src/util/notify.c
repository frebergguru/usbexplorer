/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/notify.h"

#include <libnotify/notify.h>
#include <unistd.h>

static gboolean g_ready;

/* Is a D-Bus session bus reachable?  Initialising libnotify without one makes
 * GLib try to spawn `dbus-launch --autolaunch`, which fails (and warns) in
 * environments that have no session bus (SSH, headless, minimal shells). */
static gboolean session_bus_available(void)
{
    if (g_getenv("DBUS_SESSION_BUS_ADDRESS"))
        return TRUE;
    char *path = g_strdup_printf("/run/user/%u/bus", (unsigned)getuid());
    gboolean ok = g_file_test(path, G_FILE_TEST_EXISTS);
    g_free(path);
    return ok;
}

void ue_notify_init(void)
{
    if (g_ready || !session_bus_available())
        return;
    g_ready = notify_init("usbexplorer");
}

void ue_notify_send(const char *summary, const char *body, const char *icon)
{
    if (!g_ready)
        return;
    NotifyNotification *n = notify_notification_new(summary, body,
                                                    icon ? icon : "drive-removable-media");
    notify_notification_show(n, NULL);
    g_object_unref(n);
}

void ue_notify_uninit(void)
{
    if (g_ready) {
        notify_uninit();
        g_ready = FALSE;
    }
}
