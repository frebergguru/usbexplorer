/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_UDEV_RULE_H
#define USBEXPLORER_UTIL_UDEV_RULE_H

#include <stdint.h>
#include <stdio.h>

#include "usb/enumerate.h"

/*
 * Generate a udev .rules snippet matching a device by VID/PID (and, when a
 * node is supplied, optionally by serial number).  The output is a ready-to-
 * use rule granting the invoking user access, plus commented alternatives.
 */

/* Write a rule for the given VID/PID.  'node' may be NULL; when non-NULL its
 * serial number and product string are used to enrich the rule/comments. */
void udev_rule_write(FILE *out, uint16_t vid, uint16_t pid, const UsbNode *node);

#endif /* USBEXPLORER_UTIL_UDEV_RULE_H */
