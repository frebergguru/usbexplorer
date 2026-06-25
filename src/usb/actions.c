/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/actions.h"
#include "util/sysfs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

const char *usb_action_name(UsbActionType t)
{
    switch (t) {
    case ACTION_RESET:           return "reset";
    case ACTION_RESTART:         return "restart";
    case ACTION_PORT_CYCLE:      return "port-cycle";
    case ACTION_AUTOSUSPEND_ON:  return "autosuspend-on";
    case ACTION_AUTOSUSPEND_OFF: return "autosuspend-off";
    case ACTION_WAKEUP_ON:       return "wakeup-on";
    case ACTION_WAKEUP_OFF:      return "wakeup-off";
    default:                     return "?";
    }
}

int usb_action_parse_busdev(const char *s, int *bus, int *dev)
{
    return sscanf(s, "%d.%d", bus, dev) == 2;
}

/* Find the sysfs device name (e.g. "1-5") for a bus/dev pair by scanning
 * /sys/bus/usb/devices.  Returns 1 on success. */
static int find_devname(int bus, int dev, char *out, size_t outsz)
{
    DIR *d = opendir("/sys/bus/usb/devices");
    if (!d)
        return 0;
    int found = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || strchr(de->d_name, ':'))
            continue;
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "/sys/bus/usb/devices/%s", de->d_name);
        if (sysfs_read_int(dir, "busnum", -1) == bus &&
            sysfs_read_int(dir, "devnum", -1) == dev) {
            snprintf(out, outsz, "%.63s", de->d_name);
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static int write_file(const char *path, const char *value, char *errbuf, size_t errsz)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(errbuf, errsz, "cannot open %s: %s%s", path, strerror(errno),
                 errno == EACCES ? " (needs root)" : "");
        return -1;
    }
    ssize_t n = write(fd, value, strlen(value));
    int e = errno;
    close(fd);
    if (n < 0) {
        snprintf(errbuf, errsz, "write %s to %s failed: %s", value, path, strerror(e));
        return -1;
    }
    return 0;
}

static int do_reset(int bus, int dev, char *errbuf, size_t errsz)
{
    char node[64];
    snprintf(node, sizeof node, "/dev/bus/usb/%03d/%03d", bus, dev);
    int fd = open(node, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(errbuf, errsz, "cannot open %s: %s%s", node, strerror(errno),
                 errno == EACCES ? " (needs root)" : "");
        return -1;
    }
    int rc = ioctl(fd, USBDEVFS_RESET, 0);
    int e = errno;
    close(fd);
    if (rc < 0) {
        snprintf(errbuf, errsz, "USBDEVFS_RESET failed: %s", strerror(e));
        return -1;
    }
    return 0;
}

static int do_restart(int bus, int dev, char *errbuf, size_t errsz)
{
    char name[64];
    if (!find_devname(bus, dev, name, sizeof name)) {
        snprintf(errbuf, errsz, "no device at bus %d dev %d", bus, dev);
        return -1;
    }
    if (write_file("/sys/bus/usb/drivers/usb/unbind", name, errbuf, errsz) != 0)
        return -1;
    return write_file("/sys/bus/usb/drivers/usb/bind", name, errbuf, errsz);
}

static int do_port_cycle(int bus, int dev, char *errbuf, size_t errsz)
{
    char name[64];
    if (!find_devname(bus, dev, name, sizeof name)) {
        snprintf(errbuf, errsz, "no device at bus %d dev %d", bus, dev);
        return -1;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/authorized", name);
    if (write_file(path, "0", errbuf, errsz) != 0)
        return -1;
    return write_file(path, "1", errbuf, errsz);
}

/* Write a value into <dev>/power/<attr> for the device at bus/dev. */
static int do_power(int bus, int dev, const char *attr, const char *value,
                    char *errbuf, size_t errsz)
{
    char name[64];
    if (!find_devname(bus, dev, name, sizeof name)) {
        snprintf(errbuf, errsz, "no device at bus %d dev %d", bus, dev);
        return -1;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/power/%s", name, attr);
    return write_file(path, value, errbuf, errsz);
}

int usb_action_perform(UsbActionType t, int bus, int dev, char *errbuf, size_t errsz)
{
    errbuf[0] = '\0';
    switch (t) {
    case ACTION_RESET:      return do_reset(bus, dev, errbuf, errsz);
    case ACTION_RESTART:    return do_restart(bus, dev, errbuf, errsz);
    case ACTION_PORT_CYCLE: return do_port_cycle(bus, dev, errbuf, errsz);
    case ACTION_AUTOSUSPEND_ON:  return do_power(bus, dev, "control", "auto", errbuf, errsz);
    case ACTION_AUTOSUSPEND_OFF: return do_power(bus, dev, "control", "on", errbuf, errsz);
    case ACTION_WAKEUP_ON:       return do_power(bus, dev, "wakeup", "enabled", errbuf, errsz);
    case ACTION_WAKEUP_OFF:      return do_power(bus, dev, "wakeup", "disabled", errbuf, errsz);
    default:
        snprintf(errbuf, errsz, "unknown action");
        return -1;
    }
}
