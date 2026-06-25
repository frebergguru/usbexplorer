/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/bluetooth.h"

#include <stdio.h>
#include <strings.h>

int cli_bluetooth(void)
{
    BtList *l = bt_enumerate();
    if (l->error[0]) {
        fprintf(stderr, "usbexplorer: %s\n", l->error);
        bt_list_free(l);
        return 1;
    }

    printf("Adapters (%zu):\n", l->n_adapters);
    for (size_t i = 0; i < l->n_adapters; i++) {
        BtAdapter *a = &l->adapters[i];
        printf("  %s  %-20s  %s\n", a->address, a->name,
               a->powered ? "powered" : "off");
    }

    printf("Devices (%zu):\n", l->n_devices);
    for (size_t i = 0; i < l->n_devices; i++) {
        BtDevice *d = &l->devices[i];
        printf("  %s  %-24s class 0x%06x  %s%s%s\n", d->address, d->name,
               d->class,
               d->paired ? "paired " : "",
               d->connected ? "connected " : "",
               d->trusted ? "trusted" : "");
    }
    bt_list_free(l);
    return 0;
}

int cli_bt_remove(const char *mac)
{
    BtList *l = bt_enumerate();
    if (l->error[0]) {
        fprintf(stderr, "usbexplorer: %s\n", l->error);
        bt_list_free(l);
        return 1;
    }
    int rc = 1;
    for (size_t i = 0; i < l->n_devices; i++) {
        if (strcasecmp(l->devices[i].address, mac) == 0) {
            char err[256];
            if (bt_remove_device(l->devices[i].adapter, l->devices[i].path,
                                 err, sizeof err) == 0) {
                printf("removed %s\n", mac);
                rc = 0;
            } else {
                fprintf(stderr, "usbexplorer: %s\n", err);
            }
            bt_list_free(l);
            return rc;
        }
    }
    fprintf(stderr, "usbexplorer: no Bluetooth device with address %s\n", mac);
    bt_list_free(l);
    return 1;
}
