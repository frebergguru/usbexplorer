/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_SYSFS_H
#define USBEXPLORER_UTIL_SYSFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Small, error-checked helpers for reading attributes out of sysfs.
 *
 * Every function takes a directory path plus an attribute name and reads
 * "<dir>/<attr>".  They never abort on a missing attribute: a missing or
 * unreadable file is reported via the return value (false / -1 / 0) and the
 * caller decides what to do.  Whitespace (including the trailing newline that
 * sysfs always appends) is trimmed from string results.
 */

/* Read a text attribute into 'out' (NUL-terminated, trimmed).
 * Returns true on success, false if the file is missing/unreadable.
 * On failure 'out' is set to an empty string (when out_sz > 0). */
bool sysfs_read_str(const char *dir, const char *attr, char *out, size_t out_sz);

/* Read an attribute that contains a hexadecimal value (no "0x" prefix, as in
 * idVendor/idProduct/bcdDevice).  Returns the value, or 'fallback' on error. */
uint16_t sysfs_read_hex16(const char *dir, const char *attr, uint16_t fallback);

/* Read an attribute that holds a hex byte (bDeviceClass, bmAttributes, ...). */
uint8_t sysfs_read_hex8(const char *dir, const char *attr, uint8_t fallback);

/* Read a base-10 integer attribute (busnum, devnum, maxchild, ...).
 * Returns the value, or 'fallback' on error. */
int sysfs_read_int(const char *dir, const char *attr, int fallback);

/* Read a binary attribute (e.g. the "descriptors" blob) fully into a freshly
 * malloc'd buffer.  On success *out_buf points to the data and *out_len holds
 * its length; the caller must free(*out_buf).  Returns true on success. */
bool sysfs_read_blob(const char *dir, const char *attr,
                     uint8_t **out_buf, size_t *out_len);

/* Resolve a symlink attribute (e.g. "driver", "iommu_group") and return only
 * its basename in 'out'.  Returns true on success. */
bool sysfs_read_link_base(const char *dir, const char *attr,
                          char *out, size_t out_sz);

/* True if "<dir>/<attr>" exists and is accessible. */
bool sysfs_exists(const char *dir, const char *attr);

#endif /* USBEXPLORER_UTIL_SYSFS_H */
