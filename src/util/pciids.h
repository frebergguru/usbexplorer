/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_PCIIDS_H
#define USBEXPLORER_UTIL_PCIIDS_H

#include <stdint.h>

/*
 * Lookups against the system PCI ID database (pci.ids).  Same lazy-load,
 * cached-for-process-lifetime model as util/usbids.  Returned strings are
 * owned internally and must not be freed.  NULL means "not found".
 *
 * (PCI ids are a different namespace from USB ids, so host-controller vendor/
 * device names must come from here, not from usb.ids.)
 */

const char *pciids_vendor(uint16_t vid);
const char *pciids_device(uint16_t vid, uint16_t did);
void        pciids_free(void);

#endif /* USBEXPLORER_UTIL_PCIIDS_H */
