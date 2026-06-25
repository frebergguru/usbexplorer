/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_BADUSB_H
#define USBEXPLORER_USB_BADUSB_H

#include <stddef.h>

#include "usb/enumerate.h"

/*
 * Lightweight security/hygiene heuristics for a single device.  These are
 * advisory: they flag composite devices whose interface mix is characteristic
 * of "BadUSB" keystroke-injection attacks, plus a few hygiene observations
 * (missing serial, vendor-specific-only device).  None of this proves a device
 * is malicious; it points a human at things worth a second look.
 */

typedef enum {
    FINDING_INFO,    /* observation, not a concern                  */
    FINDING_WARN,    /* unusual; worth checking                     */
    FINDING_DANGER,  /* matches a known attack shape                */
} FindingSeverity;

typedef struct {
    FindingSeverity severity;
    char title[64];
    char detail[256];
} BadUsbFinding;

/* Analyse 'n', writing up to 'max' findings into 'out'.  Returns the number
 * written.  A device with no findings returns 0. */
size_t badusb_analyze(const UsbNode *n, BadUsbFinding *out, size_t max);

/* Highest severity across a node's findings (FINDING_INFO if none flagged at
 * WARN/DANGER level). */
FindingSeverity badusb_worst(const UsbNode *n);

const char *badusb_severity_str(FindingSeverity s);

/* Further-reading link shown alongside BadUSB findings. */
#define BADUSB_REFERENCE_URL "https://en.wikipedia.org/wiki/BadUSB"

#endif /* USBEXPLORER_USB_BADUSB_H */
