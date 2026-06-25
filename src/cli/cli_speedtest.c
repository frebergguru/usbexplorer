/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/report.h"

#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_MAX_BYTES (256LL << 20)   /* 256 MiB */

int cli_speedtest(const UsbNode *root, const char *busdev)
{
    int bus, dev;
    if (sscanf(busdev, "%d.%d", &bus, &dev) != 2) {
        fprintf(stderr, "usbexplorer: --speed-test expects BUSNUM.DEVNUM (e.g. 6.3)\n");
        return 2;
    }
    const UsbNode *n = usb_find_by_busdev(root, bus, dev);
    if (!n) {
        fprintf(stderr, "usbexplorer: no device at bus %d device %d\n", bus, dev);
        return 1;
    }
    char *txt = report_speedtest(n, DEFAULT_MAX_BYTES);
    if (txt) {
        fputs(txt, stdout);
        free(txt);
    }
    return 0;
}
