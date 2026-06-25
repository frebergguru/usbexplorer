/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/report.h"

#include <stdio.h>
#include <stdlib.h>

int cli_latency(const UsbNode *root, const char *busdev)
{
    int bus, dev;
    if (sscanf(busdev, "%d.%d", &bus, &dev) != 2) {
        fprintf(stderr, "usbexplorer: --latency expects BUSNUM.DEVNUM\n");
        return 2;
    }
    const UsbNode *n = usb_find_by_busdev(root, bus, dev);
    if (!n) {
        fprintf(stderr, "usbexplorer: no device at bus %d device %d\n", bus, dev);
        return 1;
    }
    char *txt = report_latency(n);
    if (txt) {
        fputs(txt, stdout);
        printf("Note: measured intervals require usbmon (--usbmon %d.%d).\n", bus, dev);
        free(txt);
    }
    return 0;
}
