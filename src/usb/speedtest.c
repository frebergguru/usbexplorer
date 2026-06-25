/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/speedtest.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (1u << 20)   /* 1 MiB sequential reads */

/* Locate the block device (e.g. "sdb") whose sysfs "device" link points
 * somewhere beneath 'usb_realpath'.  Writes "/dev/<name>" into 'devnode'. */
static bool find_block_device(const char *usb_realpath, char *devnode, size_t sz)
{
    DIR *d = opendir("/sys/block");
    if (!d)
        return false;

    bool found = false;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char link[PATH_MAX], real[PATH_MAX];
        snprintf(link, sizeof link, "/sys/block/%s/device", de->d_name);
        if (!realpath(link, real))
            continue;
        if (strncmp(real, usb_realpath, strlen(usb_realpath)) == 0) {
            snprintf(devnode, sz, "/dev/%.58s", de->d_name);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static double elapsed(const struct timespec *a, const struct timespec *b)
{
    return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) / 1e9;
}

bool speedtest_run(const UsbNode *node, long long max_bytes, SpeedResult *out)
{
    memset(out, 0, sizeof *out);

    char usb_real[PATH_MAX];
    if (!realpath(node->sysfs_path, usb_real)) {
        snprintf(out->error, sizeof out->error, "cannot resolve %.200s",
                 node->sysfs_path);
        return false;
    }
    if (!find_block_device(usb_real, out->devnode, sizeof out->devnode)) {
        snprintf(out->error, sizeof out->error,
                 "no block device found for this USB device "
                 "(is it a mounted/partitioned storage device?)");
        return false;
    }

    /* Prefer O_DIRECT so we measure the device, not the page cache. */
    int fd = open(out->devnode, O_RDONLY | O_DIRECT | O_CLOEXEC);
    bool direct = fd >= 0;
    if (fd < 0)
        fd = open(out->devnode, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(out->error, sizeof out->error, "cannot open %s: %s%s",
                 out->devnode, strerror(errno),
                 errno == EACCES ? " (try running as root)" : "");
        return false;
    }

    void *buf = NULL;
    if (posix_memalign(&buf, 4096, CHUNK) != 0) {
        snprintf(out->error, sizeof out->error, "out of memory");
        close(fd);
        return false;
    }
    if (!direct)
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    long long total = 0;
    while (total < max_bytes) {
        ssize_t r = read(fd, buf, CHUNK);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            snprintf(out->error, sizeof out->error, "read error: %s",
                     strerror(errno));
            free(buf);
            close(fd);
            return false;
        }
        if (r == 0)
            break; /* end of device */
        total += r;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(buf);
    close(fd);

    out->bytes = total;
    out->seconds = elapsed(&t0, &t1);
    out->mb_per_s = out->seconds > 0 ? (double)total / 1e6 / out->seconds : 0.0;
    out->ok = total > 0;
    if (!out->ok)
        snprintf(out->error, sizeof out->error, "read 0 bytes from %s",
                 out->devnode);
    return out->ok;
}
