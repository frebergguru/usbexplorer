/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/monitor.h"

#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct UsbMonitor {
    struct udev *udev;
    struct udev_monitor *mon;
    int fd;
};

UsbMonitor *usb_monitor_new(void)
{
    UsbMonitor *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;

    m->udev = udev_new();
    if (!m->udev)
        goto fail;

    m->mon = udev_monitor_new_from_netlink(m->udev, "udev");
    if (!m->mon)
        goto fail;

    /* Match both whole-device and per-interface events. */
    udev_monitor_filter_add_match_subsystem_devtype(m->mon, "usb", NULL);
    if (udev_monitor_enable_receiving(m->mon) < 0)
        goto fail;

    m->fd = udev_monitor_get_fd(m->mon);
    return m;

fail:
    usb_monitor_free(m);
    return NULL;
}

int usb_monitor_fd(const UsbMonitor *m)
{
    return m ? m->fd : -1;
}

static UsbEventType classify(const char *action)
{
    if (!action)
        return USB_EV_OTHER;
    if (!strcmp(action, "add"))    return USB_EV_ADD;
    if (!strcmp(action, "remove")) return USB_EV_REMOVE;
    if (!strcmp(action, "bind"))   return USB_EV_BIND;
    if (!strcmp(action, "unbind")) return USB_EV_UNBIND;
    if (!strcmp(action, "change")) return USB_EV_CHANGE;
    return USB_EV_OTHER;
}

static void copy_attr(char *dst, size_t dst_sz, const char *src)
{
    snprintf(dst, dst_sz, "%s", src ? src : "");
}

bool usb_monitor_next(UsbMonitor *m, UsbEvent *out)
{
    if (!m)
        return false;
    struct udev_device *dev = udev_monitor_receive_device(m->mon);
    if (!dev)
        return false;

    memset(out, 0, sizeof *out);
    const char *action = udev_device_get_action(dev);
    out->type = classify(action);
    copy_attr(out->action, sizeof out->action, action);
    copy_attr(out->sysname, sizeof out->sysname, udev_device_get_sysname(dev));
    copy_attr(out->devtype, sizeof out->devtype, udev_device_get_devtype(dev));
    copy_attr(out->syspath, sizeof out->syspath, udev_device_get_syspath(dev));
    copy_attr(out->driver, sizeof out->driver, udev_device_get_driver(dev));

    const char *vid = udev_device_get_sysattr_value(dev, "idVendor");
    const char *pid = udev_device_get_sysattr_value(dev, "idProduct");
    if (vid && pid) {
        out->vid = (uint16_t)strtoul(vid, NULL, 16);
        out->pid = (uint16_t)strtoul(pid, NULL, 16);
        out->has_ids = true;
    }

    udev_device_unref(dev);
    return true;
}

void usb_monitor_free(UsbMonitor *m)
{
    if (!m)
        return;
    if (m->mon)
        udev_monitor_unref(m->mon);
    if (m->udev)
        udev_unref(m->udev);
    free(m);
}
