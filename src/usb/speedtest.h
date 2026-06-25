/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_SPEEDTEST_H
#define USBEXPLORER_USB_SPEEDTEST_H

#include <stdbool.h>

#include "usb/enumerate.h"

/*
 * Sequential read-speed test for a storage device.  The block device backing
 * a USB node is located via sysfs, then read sequentially (O_DIRECT where
 * possible, to bypass the page cache) and timed.  This only ever READS, so it
 * is non-destructive.
 */

typedef struct {
    bool   ok;
    char   devnode[64];     /* e.g. "/dev/sdb"           */
    long long bytes;        /* bytes actually read        */
    double seconds;
    double mb_per_s;        /* MB/s (decimal megabytes)   */
    char   error[256];      /* populated when !ok         */
} SpeedResult;

/* Read up to 'max_bytes' from the storage device behind 'node' (rounded to the
 * I/O chunk size).  Returns 'out->ok'. */
bool speedtest_run(const UsbNode *node, long long max_bytes, SpeedResult *out);

#endif /* USBEXPLORER_USB_SPEEDTEST_H */
