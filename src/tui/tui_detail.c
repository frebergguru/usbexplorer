/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tui/tui.h"

#include "usb/badusb.h"
#include "usb/descriptors.h"
#include "util/pciids.h"
#include "util/usbids.h"

#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_line(TuiState *s, int attr, const char *fmt, ...)
{
    if (s->n_detail == s->cap_detail) {
        size_t nc = s->cap_detail ? s->cap_detail * 2 : 64;
        TuiLine *g = realloc(s->detail, nc * sizeof *g);
        if (!g)
            return;
        s->detail = g;
        s->cap_detail = nc;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s->detail[s->n_detail].text = strdup(buf);
    s->detail[s->n_detail].attr = attr;
    s->n_detail++;
}

void tui_detail_free(TuiState *s)
{
    for (size_t i = 0; i < s->n_detail; i++)
        free(s->detail[i].text);
    s->n_detail = 0;
}

/* Recursively flatten a decoded descriptor subtree into indented lines. */
static void emit_desc(TuiState *s, const DescNode *d, int depth)
{
    add_line(s, COLOR_PAIR(CP_HEADER), "%*s%s", depth * 2, "", d->type);
    for (size_t i = 0; i < d->n_fields; i++)
        add_line(s, 0, "%*s  %-22s %s", depth * 2, "", d->fields[i].name,
                 d->fields[i].value);
    for (size_t i = 0; i < d->n_children; i++)
        emit_desc(s, d->children[i], depth + 1);
}

static void build_controller(TuiState *s, const UsbNode *n)
{
    const ControllerInfo *c = n->controller;
    add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "Host Controller");
    if (!c)
        return;
    add_line(s, 0, "  %-18s %s", "PCI address", c->pci_addr);
    const char *v = pciids_vendor(c->pci_vendor);
    const char *dv = pciids_device(c->pci_vendor, c->pci_device);
    add_line(s, 0, "  %-18s 0x%04x %s", "PCI vendor", c->pci_vendor, v ? v : "");
    add_line(s, 0, "  %-18s 0x%04x %s", "PCI device", c->pci_device, dv ? dv : "");
    add_line(s, 0, "  %-18s 0x%06x", "PCI class", c->pci_class);
    add_line(s, 0, "  %-18s %s", "Driver", c->driver[0] ? c->driver : "(none)");
    if (c->iommu_group >= 0)
        add_line(s, 0, "  %-18s %d", "IOMMU group", c->iommu_group);
}

static void build_device(TuiState *s, const UsbNode *n)
{
    const char *vn = n->manufacturer[0] ? n->manufacturer : usbids_vendor(n->vid);
    const char *pn = n->product[0] ? n->product : usbids_product(n->vid, n->pid);

    add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "%s — %s",
             vn ? vn : "Unknown", pn ? pn : "Unknown device");
    add_line(s, 0, "  %-18s %04x:%04x", "VID:PID", n->vid, n->pid);
    add_line(s, 0, "  %-18s %s", "Manufacturer", n->manufacturer[0] ? n->manufacturer : "(none)");
    add_line(s, 0, "  %-18s %s", "Product", n->product[0] ? n->product : "(none)");
    add_line(s, 0, "  %-18s %s", "Serial", n->serial[0] ? n->serial : "(none)");
    add_line(s, COLOR_PAIR(CP_SPEED), "  %-18s %s", "Speed", usb_speed_str(n->speed));
    add_line(s, 0, "  %-18s 0x%02x (%s)", "Class", n->dev_class,
             usbids_class(n->dev_class) ? usbids_class(n->dev_class) : "");
    add_line(s, 0, "  %-18s %x.%02x", "USB version", n->bcd_usb >> 8, n->bcd_usb & 0xff);
    add_line(s, 0, "  %-18s %x.%02x", "Device version", n->bcd_device >> 8, n->bcd_device & 0xff);
    add_line(s, 0, "  %-18s %d mA, %s", "Power", n->max_power_ma,
             n->self_powered ? "self-powered" : "bus-powered");
    if (n->power_control[0])
        add_line(s, 0, "  %-18s %s (delay %d ms), wakeup %s, %s", "Autosuspend",
                 n->power_control, n->autosuspend_delay_ms,
                 n->wakeup[0] ? n->wakeup : "n/a",
                 n->runtime_status[0] ? n->runtime_status : "?");
    add_line(s, 0, "  %-18s bus %d, dev %d, port %d", "Address",
             n->busnum, n->devnum, n->port);
    add_line(s, 0, "  %-18s %d of %d", "Configuration", n->cur_config, n->num_configs);
    add_line(s, COLOR_PAIR(CP_DIM), "  %-18s %s", "sysfs", n->sysfs_path);

    if (n->n_interfaces) {
        add_line(s, 0, "%s", "");
        add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "Interfaces");
        for (size_t i = 0; i < n->n_interfaces; i++) {
            const UsbInterface *ui = &n->interfaces[i];
            add_line(s, 0, "  #%u alt %u  class 0x%02x/0x%02x/0x%02x  %u ep  %s",
                     ui->number, ui->alt, ui->cls, ui->subcls, ui->proto,
                     ui->n_endpoints, ui->driver[0] ? ui->driver : "(no driver)");
        }
    }

    BadUsbFinding f[8];
    size_t nf = badusb_analyze(n, f, 8);
    if (nf) {
        add_line(s, 0, "%s", "");
        add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "Warnings");
        for (size_t i = 0; i < nf; i++) {
            int cp = f[i].severity == FINDING_DANGER ? CP_DANGER
                   : f[i].severity == FINDING_WARN ? CP_WARN : CP_DIM;
            add_line(s, COLOR_PAIR(cp) | A_BOLD, "  [%s] %s",
                     badusb_severity_str(f[i].severity), f[i].title);
            add_line(s, COLOR_PAIR(CP_DIM), "      %s", f[i].detail);
        }
        add_line(s, COLOR_PAIR(CP_DIM), "      see %s", BADUSB_REFERENCE_URL);
    }

    if (n->raw_descriptors && n->raw_len) {
        add_line(s, 0, "%s", "");
        add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "Descriptors");
        DescNode *d = usb_decode_descriptors(n->raw_descriptors, n->raw_len);
        if (d) {
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(s, d->children[i], 1);
            desc_node_free(d);
        }
    }

    if (n->bos_raw && n->bos_len) {
        add_line(s, 0, "%s", "");
        add_line(s, COLOR_PAIR(CP_HEADER) | A_BOLD, "Binary Object Store (BOS)");
        DescNode *d = usb_decode_descriptors(n->bos_raw, n->bos_len);
        if (d) {
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(s, d->children[i], 1);
            desc_node_free(d);
        }
    }
}

void tui_detail_build(TuiState *s)
{
    tui_detail_free(s);
    if (s->n_rows == 0)
        return;
    const UsbNode *n = s->rows[s->sel].node;

    if (n != s->detail_for) {
        s->detail_top = 0;
        s->detail_for = n;
    }

    if (n->kind == NODE_CONTROLLER)
        build_controller(s, n);
    else
        build_device(s, n);
}
