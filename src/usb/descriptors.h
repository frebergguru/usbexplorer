/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_DESCRIPTORS_H
#define USBEXPLORER_USB_DESCRIPTORS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Descriptor decoding.
 *
 * usb_decode_descriptors() walks a raw descriptor blob (the device descriptor
 * followed by every configuration descriptor set, exactly as exposed by the
 * sysfs "descriptors" attribute) and returns a generic, render-agnostic tree
 * of DescNode.  Each DescNode carries:
 *   - a human label ("Device Descriptor", "Endpoint Descriptor", ...),
 *   - an ordered list of decoded fields (name + value string),
 *   - a pointer to the raw bytes it was decoded from (for hex dumps),
 *   - child nodes (configuration -> interface -> endpoint, etc).
 *
 * The decoders are pure functions over bytes (no I/O), so they can be unit
 * tested against captured blobs and reused unchanged by CLI/TUI/GUI.
 */

typedef struct {
    char name[48];
    char value[192];
} DescField;

typedef struct DescNode {
    char type[64];

    DescField *fields;
    size_t     n_fields;
    size_t     cap_fields;

    const uint8_t *raw;     /* points into the caller's blob; not owned */
    size_t         raw_len;

    struct DescNode **children;
    size_t           n_children;
    size_t           cap_children;
} DescNode;

/* Decode 'blob' (length 'len').  Returns a synthetic root DescNode whose
 * children are the top-level descriptors, or NULL on allocation failure or an
 * empty blob.  Free with desc_node_free(). */
DescNode *usb_decode_descriptors(const uint8_t *blob, size_t len);

void desc_node_free(DescNode *root);

#endif /* USBEXPLORER_USB_DESCRIPTORS_H */
