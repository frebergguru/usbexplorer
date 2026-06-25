/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/notify.h"

#include <libnotify/notify.h>

static gboolean g_ready;

void ue_notify_init(void)
{
    if (!g_ready)
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
