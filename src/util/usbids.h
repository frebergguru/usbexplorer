/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_USBIDS_H
#define USBEXPLORER_UTIL_USBIDS_H

#include <stdint.h>

/*
 * Lookups against the system USB ID database (usb.ids).  The database is
 * loaded lazily on first use and cached for the process lifetime; callers do
 * not need to initialise anything.  All functions return a pointer to an
 * internal string that stays valid until usbids_free() (or process exit) and
 * must NOT be freed by the caller.  A NULL return means "not found".
 */

const char *usbids_vendor(uint16_t vid);
const char *usbids_product(uint16_t vid, uint16_t pid);

/* Class/subclass/protocol names from the "C <class>" section of usb.ids.
 * Pass the more-specific lookups only when the broader value is known. */
const char *usbids_class(uint8_t cls);
const char *usbids_subclass(uint8_t cls, uint8_t sub);
const char *usbids_protocol(uint8_t cls, uint8_t sub, uint8_t proto);

/* Release the cached database (optional; mainly for leak-check cleanliness). */
void usbids_free(void);

#endif /* USBEXPLORER_UTIL_USBIDS_H */
