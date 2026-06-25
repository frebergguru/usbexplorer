/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_BLUETOOTH_H
#define USBEXPLORER_USB_BLUETOOTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bluetooth inventory via the BlueZ D-Bus API (org.bluez ObjectManager on the
 * system bus).  Lists local adapters and known/paired remote devices.  If
 * BlueZ is not running, bt_enumerate() returns a BtList with 'error' set.
 */

typedef struct {
    char     path[128];        /* D-Bus object path        */
    char     address[24];      /* MAC                      */
    char     name[128];        /* Alias/Name               */
    bool     powered;
} BtAdapter;

typedef struct {
    char     path[128];
    char     address[24];
    char     name[128];
    char     adapter[128];     /* parent adapter path      */
    bool     paired, connected, trusted;
    uint32_t class;
} BtDevice;

typedef struct {
    BtAdapter *adapters;
    size_t     n_adapters;
    BtDevice  *devices;
    size_t     n_devices;
    char       error[256];     /* "" on success            */
} BtList;

BtList *bt_enumerate(void);
void    bt_list_free(BtList *l);

/* Remove (unpair/forget) a device by its D-Bus object path.  Returns 0 on
 * success; fills errbuf otherwise. */
int bt_remove_device(const char *adapter_path, const char *device_path,
                     char *errbuf, size_t errsz);

#endif /* USBEXPLORER_USB_BLUETOOTH_H */
