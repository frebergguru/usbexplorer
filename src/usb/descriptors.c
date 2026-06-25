/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/descriptors.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- descriptor type constants ---- */
enum {
    DT_DEVICE = 0x01,
    DT_CONFIG = 0x02,
    DT_STRING = 0x03,
    DT_INTERFACE = 0x04,
    DT_ENDPOINT = 0x05,
    DT_DEVICE_QUALIFIER = 0x06,
    DT_OTHER_SPEED_CONFIG = 0x07,
    DT_INTERFACE_ASSOC = 0x0B,
    DT_BOS = 0x0F,
    DT_DEVICE_CAPABILITY = 0x10,
    DT_HID = 0x21,
    DT_HID_REPORT = 0x22,
    DT_HID_PHYSICAL = 0x23,
    DT_CS_INTERFACE = 0x24,
    DT_CS_ENDPOINT = 0x25,
    DT_HUB = 0x29,
    DT_SS_HUB = 0x2A,
    DT_SS_EP_COMPANION = 0x30,
    DT_SSP_ISOC_EP_COMPANION = 0x31,
};

/* interface classes we special-case */
enum {
    IF_AUDIO = 0x01,
    IF_CDC = 0x02,
    IF_HID = 0x03,
    IF_VIDEO = 0x0E,
    IF_CDC_DATA = 0x0A,
    IF_DFU = 0xFE,
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* ---------------------------------------------------------- node builders */

static DescNode *node_new(const uint8_t *raw, size_t raw_len, const char *fmt, ...)
{
    DescNode *n = calloc(1, sizeof *n);
    if (!n)
        return NULL;
    n->raw = raw;
    n->raw_len = raw_len;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(n->type, sizeof n->type, fmt, ap);
    va_end(ap);
    return n;
}

static void add_field(DescNode *n, const char *name, const char *fmt, ...)
{
    if (!n)
        return;
    if (n->n_fields == n->cap_fields) {
        size_t nc = n->cap_fields ? n->cap_fields * 2 : 8;
        DescField *g = realloc(n->fields, nc * sizeof *g);
        if (!g)
            return;
        n->fields = g;
        n->cap_fields = nc;
    }
    DescField *f = &n->fields[n->n_fields++];
    snprintf(f->name, sizeof f->name, "%s", name);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->value, sizeof f->value, fmt, ap);
    va_end(ap);
}

static void add_child(DescNode *parent, DescNode *child)
{
    if (!parent || !child)
        return;
    if (parent->n_children == parent->cap_children) {
        size_t nc = parent->cap_children ? parent->cap_children * 2 : 8;
        DescNode **g = realloc(parent->children, nc * sizeof *g);
        if (!g)
            return;
        parent->children = g;
        parent->cap_children = nc;
    }
    parent->children[parent->n_children++] = child;
}

/* ----------------------------------------------------- field-decode utils */

static const char *class_name(uint8_t c)
{
    switch (c) {
    case 0x00: return "(defined at interface level)";
    case 0x01: return "Audio";
    case 0x02: return "Communications (CDC)";
    case 0x03: return "Human Interface Device";
    case 0x05: return "Physical";
    case 0x06: return "Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC Data";
    case 0x0B: return "Smart Card";
    case 0x0D: return "Content Security";
    case 0x0E: return "Video";
    case 0x0F: return "Personal Healthcare";
    case 0x10: return "Audio/Video";
    case 0x11: return "Billboard";
    case 0xDC: return "Diagnostic";
    case 0xE0: return "Wireless Controller";
    case 0xEF: return "Miscellaneous";
    case 0xFE: return "Application Specific";
    case 0xFF: return "Vendor Specific";
    default:   return "Unknown";
    }
}

static const char *ep_xfer_name(uint8_t attr)
{
    switch (attr & 0x03) {
    case 0: return "Control";
    case 1: return "Isochronous";
    case 2: return "Bulk";
    default: return "Interrupt";
    }
}

/* ---- individual descriptor decoders ---- */

static DescNode *decode_device(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Device Descriptor");
    if (len < 18)
        return n;
    add_field(n, "bLength", "%u", p[0]);
    add_field(n, "bDescriptorType", "0x%02X (Device)", p[1]);
    add_field(n, "bcdUSB", "%x.%02x", p[3], p[2]);
    add_field(n, "bDeviceClass", "0x%02X (%s)", p[4], class_name(p[4]));
    add_field(n, "bDeviceSubClass", "0x%02X", p[5]);
    add_field(n, "bDeviceProtocol", "0x%02X", p[6]);
    add_field(n, "bMaxPacketSize0", "%u", p[7]);
    add_field(n, "idVendor", "0x%04X", le16(p + 8));
    add_field(n, "idProduct", "0x%04X", le16(p + 10));
    add_field(n, "bcdDevice", "%x.%02x", p[13], p[12]);
    add_field(n, "iManufacturer", "%u", p[14]);
    add_field(n, "iProduct", "%u", p[15]);
    add_field(n, "iSerialNumber", "%u", p[16]);
    add_field(n, "bNumConfigurations", "%u", p[17]);
    return n;
}

static DescNode *decode_config(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Configuration Descriptor");
    if (len < 9)
        return n;
    add_field(n, "bLength", "%u", p[0]);
    add_field(n, "bDescriptorType", "0x%02X (Configuration)", p[1]);
    add_field(n, "wTotalLength", "%u", le16(p + 2));
    add_field(n, "bNumInterfaces", "%u", p[4]);
    add_field(n, "bConfigurationValue", "%u", p[5]);
    add_field(n, "iConfiguration", "%u", p[6]);
    uint8_t a = p[7];
    add_field(n, "bmAttributes", "0x%02X (%s%s%s)", a,
              (a & 0x40) ? "Self-powered" : "Bus-powered",
              (a & 0x20) ? ", Remote-wakeup" : "",
              (a & 0x10) ? ", BatteryPowered" : "");
    add_field(n, "bMaxPower", "%u mA", p[8] * 2);
    return n;
}

static DescNode *decode_iad(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Interface Association Descriptor");
    if (len < 8)
        return n;
    add_field(n, "bFirstInterface", "%u", p[2]);
    add_field(n, "bInterfaceCount", "%u", p[3]);
    add_field(n, "bFunctionClass", "0x%02X (%s)", p[4], class_name(p[4]));
    add_field(n, "bFunctionSubClass", "0x%02X", p[5]);
    add_field(n, "bFunctionProtocol", "0x%02X", p[6]);
    add_field(n, "iFunction", "%u", p[7]);
    return n;
}

static DescNode *decode_interface(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Interface Descriptor");
    if (len < 9)
        return n;
    add_field(n, "bInterfaceNumber", "%u", p[2]);
    add_field(n, "bAlternateSetting", "%u", p[3]);
    add_field(n, "bNumEndpoints", "%u", p[4]);
    add_field(n, "bInterfaceClass", "0x%02X (%s)", p[5], class_name(p[5]));
    add_field(n, "bInterfaceSubClass", "0x%02X", p[6]);
    add_field(n, "bInterfaceProtocol", "0x%02X", p[7]);
    add_field(n, "iInterface", "%u", p[8]);
    return n;
}

static DescNode *decode_endpoint(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Endpoint Descriptor");
    if (len < 7)
        return n;
    uint8_t addr = p[2];
    add_field(n, "bEndpointAddress", "0x%02X (EP %u %s)", addr, addr & 0x0F,
              (addr & 0x80) ? "IN" : "OUT");
    uint8_t attr = p[3];
    add_field(n, "bmAttributes", "0x%02X (%s)", attr, ep_xfer_name(attr));
    uint16_t mps = le16(p + 4);
    add_field(n, "wMaxPacketSize", "%u (max %u, %u/microframe)", mps,
              mps & 0x7FF, ((mps >> 11) & 0x3) + 1);
    add_field(n, "bInterval", "%u", p[6]);
    return n;
}

static DescNode *decode_ss_ep_companion(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "SuperSpeed Endpoint Companion");
    if (len < 6)
        return n;
    add_field(n, "bMaxBurst", "%u", p[2]);
    add_field(n, "bmAttributes", "0x%02X", p[3]);
    add_field(n, "wBytesPerInterval", "%u", le16(p + 4));
    return n;
}

static DescNode *decode_hid(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "HID Descriptor");
    if (len < 6)
        return n;
    add_field(n, "bcdHID", "%x.%02x", p[3], p[2]);
    add_field(n, "bCountryCode", "%u", p[4]);
    add_field(n, "bNumDescriptors", "%u", p[5]);
    for (uint8_t i = 0; i < p[5] && (size_t)(6 + i * 3 + 2) < len; i++) {
        const uint8_t *q = p + 6 + i * 3;
        const char *dt = q[0] == DT_HID_REPORT ? "Report"
                       : q[0] == DT_HID_PHYSICAL ? "Physical" : "Class";
        add_field(n, "  bDescriptorType", "0x%02X (%s)", q[0], dt);
        add_field(n, "  wDescriptorLength", "%u", le16(q + 1));
    }
    return n;
}

static DescNode *decode_dfu(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "DFU Functional Descriptor");
    if (len < 9)
        return n;
    uint8_t a = p[2];
    add_field(n, "bmAttributes", "0x%02X (%s%s%s%s)", a,
              (a & 0x01) ? "Download " : "",
              (a & 0x02) ? "Upload " : "",
              (a & 0x04) ? "Manifestation-tolerant " : "",
              (a & 0x08) ? "Will-detach" : "");
    add_field(n, "wDetachTimeOut", "%u ms", le16(p + 3));
    add_field(n, "wTransferSize", "%u bytes", le16(p + 5));
    add_field(n, "bcdDFUVersion", "%x.%02x", p[8], p[7]);
    return n;
}

/* CDC class-specific interface descriptor (bDescriptorType 0x24). */
static const char *cdc_subtype_name(uint8_t s)
{
    switch (s) {
    case 0x00: return "Header";
    case 0x01: return "Call Management";
    case 0x02: return "Abstract Control Management";
    case 0x06: return "Union";
    case 0x0F: return "Ethernet Networking";
    case 0x13: return "Mobile Direct Line Model (MDLM)";
    case 0x1A: return "MBIM";
    case 0x1C: return "MBIM Extended";
    default:   return "Functional";
    }
}

static DescNode *decode_cdc_cs_interface(const uint8_t *p, size_t len)
{
    uint8_t sub = (len > 2) ? p[2] : 0xFF;
    DescNode *n = node_new(p, len, "CDC %s Descriptor", cdc_subtype_name(sub));
    add_field(n, "bDescriptorSubtype", "0x%02X (%s)", sub, cdc_subtype_name(sub));
    switch (sub) {
    case 0x00: /* Header */
        if (len >= 5)
            add_field(n, "bcdCDC", "%x.%02x", p[4], p[3]);
        break;
    case 0x01: /* Call Management */
        if (len >= 5) {
            add_field(n, "bmCapabilities", "0x%02X", p[3]);
            add_field(n, "bDataInterface", "%u", p[4]);
        }
        break;
    case 0x02: /* ACM */
        if (len >= 4)
            add_field(n, "bmCapabilities", "0x%02X", p[3]);
        break;
    case 0x06: /* Union */
        if (len >= 5) {
            add_field(n, "bControlInterface", "%u", p[3]);
            add_field(n, "bSubordinateInterface0", "%u", p[4]);
        }
        break;
    case 0x0F: /* Ethernet Networking */
        if (len >= 13) {
            add_field(n, "iMACAddress", "%u", p[3]);
            add_field(n, "bmEthernetStatistics", "0x%08X", le32(p + 4));
            add_field(n, "wMaxSegmentSize", "%u", le16(p + 8));
            add_field(n, "wNumberMCFilters", "0x%04X", le16(p + 10));
            add_field(n, "bNumberPowerFilters", "%u", p[12]);
        }
        break;
    default:
        break;
    }
    return n;
}

/* Audio (class 0x01) class-specific interface subtypes. */
static const char *audio_subtype_name(uint8_t s)
{
    switch (s) {
    case 0x01: return "Header";
    case 0x02: return "Input Terminal";
    case 0x03: return "Output Terminal";
    case 0x04: return "Mixer Unit";
    case 0x05: return "Selector Unit";
    case 0x06: return "Feature Unit";
    case 0x0A: return "Clock Source";
    case 0x0B: return "Clock Selector";
    case 0x0C: return "Clock Multiplier";
    default:   return "Unit/Terminal";
    }
}

/* Video (class 0x0E) class-specific interface subtypes. */
static const char *video_subtype_name(uint8_t s)
{
    switch (s) {
    case 0x01: return "Header";
    case 0x02: return "Input Terminal";
    case 0x03: return "Output Terminal";
    case 0x04: return "Selector Unit";
    case 0x05: return "Processing Unit";
    case 0x06: return "Extension Unit";
    default:   return "Unit/Terminal";
    }
}

/* Generic class-specific interface for Audio/Video: identify the subtype and
 * keep the raw bytes for the hex dump (full field decode is a later pass). */
static DescNode *decode_generic_cs_interface(const uint8_t *p, size_t len,
                                             uint8_t iface_class)
{
    uint8_t sub = (len > 2) ? p[2] : 0xFF;
    const char *kind, *name;
    if (iface_class == IF_AUDIO) {
        kind = "Audio";
        name = audio_subtype_name(sub);
    } else if (iface_class == IF_VIDEO) {
        kind = "Video";
        name = video_subtype_name(sub);
    } else {
        kind = "Class-Specific";
        name = "Interface";
    }
    DescNode *n = node_new(p, len, "%s %s Descriptor", kind, name);
    add_field(n, "bDescriptorSubtype", "0x%02X (%s)", sub, name);
    return n;
}

static DescNode *decode_hub(const uint8_t *p, size_t len, int superspeed)
{
    DescNode *n = node_new(p, len, superspeed ? "SuperSpeed Hub Descriptor"
                                              : "Hub Descriptor");
    if (len < 7)
        return n;
    add_field(n, "bNbrPorts", "%u", p[2]);
    uint16_t ch = le16(p + 3);
    const char *psm = (ch & 0x03) == 0 ? "Ganged"
                    : (ch & 0x03) == 1 ? "Individual" : "None";
    add_field(n, "wHubCharacteristics", "0x%04X (Power: %s%s)", ch, psm,
              (ch & 0x80) ? ", Port Indicators" : "");
    add_field(n, "bPwrOn2PwrGood", "%u ms", p[5] * 2);
    add_field(n, "bHubContrCurrent", "%u mA", p[6]);
    return n;
}

static DescNode *decode_bos(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "BOS Descriptor");
    if (len < 5)
        return n;
    add_field(n, "wTotalLength", "%u", le16(p + 2));
    add_field(n, "bNumDeviceCaps", "%u", p[4]);
    return n;
}

static DescNode *decode_device_capability(const uint8_t *p, size_t len)
{
    static const char *names[] = {
        [0x01] = "Wireless USB", [0x02] = "USB 2.0 Extension",
        [0x03] = "SuperSpeed", [0x04] = "Container ID",
        [0x05] = "Platform", [0x06] = "Power Delivery",
        [0x0A] = "SuperSpeedPlus", [0x0B] = "Precision Time Measurement",
        [0x0C] = "Wireless USB Ext", [0x0D] = "Billboard",
        [0x10] = "Configuration Summary",
    };
    uint8_t cap = (len > 2) ? p[2] : 0;
    const char *nm = (cap < sizeof names / sizeof names[0] && names[cap])
                         ? names[cap] : "Unknown";
    DescNode *n = node_new(p, len, "Device Capability: %s", nm);
    add_field(n, "bDevCapabilityType", "0x%02X (%s)", cap, nm);
    return n;
}

static DescNode *decode_unknown(const uint8_t *p, size_t len)
{
    DescNode *n = node_new(p, len, "Unknown Descriptor (0x%02X)",
                           len > 1 ? p[1] : 0);
    add_field(n, "bLength", "%u", len ? p[0] : 0);
    add_field(n, "bDescriptorType", "0x%02X", len > 1 ? p[1] : 0);
    return n;
}

/* ---------------------------------------------------------- main walker */

DescNode *usb_decode_descriptors(const uint8_t *blob, size_t len)
{
    if (!blob || len < 2)
        return NULL;

    DescNode *root = node_new(blob, len, "USB Descriptors");
    if (!root)
        return NULL;

    DescNode *cur_config = NULL;
    DescNode *cur_iface = NULL;
    DescNode *cur_ep = NULL;
    DescNode *cur_bos = NULL;
    uint8_t iface_class = 0, iface_subclass = 0;

    size_t pos = 0;
    while (pos + 2 <= len) {
        uint8_t blen = blob[pos];
        uint8_t btype = blob[pos + 1];
        if (blen < 2 || pos + blen > len)
            break; /* malformed: stop rather than run off the end */

        const uint8_t *p = blob + pos;
        DescNode *node = NULL;
        DescNode *attach_to = root;

        switch (btype) {
        case DT_DEVICE:
            node = decode_device(p, blen);
            break;
        case DT_DEVICE_QUALIFIER:
            node = node_new(p, blen, "Device Qualifier Descriptor");
            break;
        case DT_CONFIG:
        case DT_OTHER_SPEED_CONFIG:
            node = decode_config(p, blen);
            if (btype == DT_OTHER_SPEED_CONFIG)
                snprintf(node->type, sizeof node->type,
                         "Other Speed Configuration Descriptor");
            cur_config = node;
            cur_iface = NULL;
            cur_ep = NULL;
            break;
        case DT_INTERFACE_ASSOC:
            node = decode_iad(p, blen);
            attach_to = cur_config ? cur_config : root;
            break;
        case DT_INTERFACE:
            node = decode_interface(p, blen);
            attach_to = cur_config ? cur_config : root;
            cur_iface = node;
            cur_ep = NULL;
            iface_class = blen > 5 ? p[5] : 0;
            iface_subclass = blen > 6 ? p[6] : 0;
            break;
        case DT_ENDPOINT:
            node = decode_endpoint(p, blen);
            attach_to = cur_iface ? cur_iface : (cur_config ? cur_config : root);
            cur_ep = node;
            break;
        case DT_SS_EP_COMPANION:
        case DT_SSP_ISOC_EP_COMPANION:
            node = decode_ss_ep_companion(p, blen);
            attach_to = cur_ep ? cur_ep : (cur_iface ? cur_iface : root);
            break;
        case DT_HID:
            /* 0x21 is HID for HID interfaces, DFU functional for DFU class. */
            if (iface_class == IF_DFU && iface_subclass == 0x01)
                node = decode_dfu(p, blen);
            else
                node = decode_hid(p, blen);
            attach_to = cur_iface ? cur_iface : root;
            break;
        case DT_CS_INTERFACE:
            if (iface_class == IF_CDC || iface_class == IF_CDC_DATA)
                node = decode_cdc_cs_interface(p, blen);
            else
                node = decode_generic_cs_interface(p, blen, iface_class);
            attach_to = cur_iface ? cur_iface : root;
            break;
        case DT_CS_ENDPOINT:
            node = node_new(p, blen, "Class-Specific Endpoint Descriptor");
            attach_to = cur_ep ? cur_ep : (cur_iface ? cur_iface : root);
            break;
        case DT_HUB:
            node = decode_hub(p, blen, 0);
            break;
        case DT_SS_HUB:
            node = decode_hub(p, blen, 1);
            break;
        case DT_BOS:
            node = decode_bos(p, blen);
            cur_bos = node;
            break;
        case DT_DEVICE_CAPABILITY:
            node = decode_device_capability(p, blen);
            attach_to = cur_bos ? cur_bos : root;
            break;
        default:
            node = decode_unknown(p, blen);
            attach_to = cur_iface ? cur_iface
                      : (cur_config ? cur_config : root);
            break;
        }

        add_child(attach_to, node);
        pos += blen;
    }

    (void)iface_subclass;
    return root;
}

void desc_node_free(DescNode *n)
{
    if (!n)
        return;
    for (size_t i = 0; i < n->n_children; i++)
        desc_node_free(n->children[i]);
    free(n->children);
    free(n->fields);
    free(n);
}
