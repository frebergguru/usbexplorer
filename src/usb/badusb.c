/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/badusb.h"

#include <stdio.h>
#include <string.h>

/* USB interface class codes we reason about. */
enum {
    CLS_AUDIO = 0x01,
    CLS_CDC = 0x02,
    CLS_HID = 0x03,
    CLS_MASS_STORAGE = 0x08,
    CLS_CDC_DATA = 0x0A,
    CLS_VIDEO = 0x0E,
    CLS_WIRELESS = 0xE0,
    CLS_VENDOR = 0xFF,
};

/* HID interface protocol: 1 = keyboard, 2 = mouse (when subclass == boot). */
static bool iface_is_keyboard(const UsbInterface *ui)
{
    return ui->cls == CLS_HID && ui->proto == 1;
}

static size_t add(BadUsbFinding *out, size_t n, size_t max, FindingSeverity sev,
                  const char *title, const char *detail)
{
    if (n >= max)
        return n;
    out[n].severity = sev;
    snprintf(out[n].title, sizeof out[n].title, "%s", title);
    snprintf(out[n].detail, sizeof out[n].detail, "%s", detail);
    return n + 1;
}

size_t badusb_analyze(const UsbNode *n, BadUsbFinding *out, size_t max)
{
    if (!n || n->kind == NODE_CONTROLLER || n->kind == NODE_ROOT)
        return 0;

    bool has_keyboard = false, has_storage = false, has_net = false;
    bool has_audio = false, has_video = false;
    int distinct = 0;
    uint16_t seen_mask = 0;

    for (size_t i = 0; i < n->n_interfaces; i++) {
        const UsbInterface *ui = &n->interfaces[i];
        if (iface_is_keyboard(ui))
            has_keyboard = true;
        if (ui->cls == CLS_MASS_STORAGE)
            has_storage = true;
        if (ui->cls == CLS_CDC || ui->cls == CLS_CDC_DATA || ui->cls == CLS_WIRELESS)
            has_net = true;
        if (ui->cls == CLS_AUDIO)
            has_audio = true;
        if (ui->cls == CLS_VIDEO)
            has_video = true;
        uint16_t bit = (uint16_t)(1u << (ui->cls & 0x0F));
        if (!(seen_mask & bit)) {
            seen_mask |= bit;
            distinct++;
        }
    }

    size_t cnt = 0;

    /* The classic BadUSB shape: a device that is also a keyboard, paired with
     * a function the user actually wanted (storage / network / media). */
    if (has_keyboard && (has_storage || has_net || has_audio || has_video)) {
        const char *other = has_storage ? "mass storage"
                          : has_net     ? "a network adapter"
                          : has_audio   ? "an audio device"
                                        : "a video device";
        char detail[256];
        snprintf(detail, sizeof detail,
                 "Presents a keyboard (HID) interface alongside %s. Hidden "
                 "keyboards are how BadUSB devices inject keystrokes. Verify "
                 "this device is supposed to be a keyboard.",
                 other);
        cnt = add(out, cnt, max, FINDING_DANGER, "Keyboard + other function",
                  detail);
    } else if (has_keyboard && distinct > 1) {
        cnt = add(out, cnt, max, FINDING_WARN, "Composite device with keyboard",
                  "A composite device that includes a keyboard interface. "
                  "Confirm the extra interfaces are expected.");
    }

    /* Hygiene observations. */
    if ((n->kind == NODE_DEVICE) && !n->serial[0])
        cnt = add(out, cnt, max, FINDING_INFO, "No serial number",
                  "Device exposes no serial number string; it cannot be "
                  "uniquely distinguished from identical units.");

    if (n->vid == 0x0000 || n->dev_class == CLS_VENDOR)
        cnt = add(out, cnt, max, FINDING_INFO, "Vendor-specific device",
                  "Uses a generic/vendor-specific class; its behaviour is not "
                  "described by a standard USB device class.");

    return cnt;
}

FindingSeverity badusb_worst(const UsbNode *n)
{
    BadUsbFinding f[8];
    size_t cnt = badusb_analyze(n, f, 8);
    FindingSeverity worst = FINDING_INFO;
    for (size_t i = 0; i < cnt; i++)
        if (f[i].severity > worst)
            worst = f[i].severity;
    return worst;
}

const char *badusb_severity_str(FindingSeverity s)
{
    switch (s) {
    case FINDING_DANGER: return "danger";
    case FINDING_WARN:   return "warning";
    default:             return "info";
    }
}
