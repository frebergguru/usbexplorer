/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/detailpanel.h"

#include <stdarg.h>

#include "usb/badusb.h"
#include "usb/descriptors.h"
#include "util/pciids.h"
#include "util/usbids.h"

struct UeDetail {
    GtkWidget     *scrolled;
    GtkWidget     *view;
    GtkTextBuffer *buf;
};

static GtkTextIter end_iter(GtkTextBuffer *b)
{
    GtkTextIter it;
    gtk_text_buffer_get_end_iter(b, &it);
    return it;
}

static void put(GtkTextBuffer *b, const char *tag, const char *fmt, ...)
    G_GNUC_PRINTF(3, 4);

static void put(GtkTextBuffer *b, const char *tag, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    g_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    GtkTextIter it = end_iter(b);
    if (tag)
        gtk_text_buffer_insert_with_tags_by_name(b, &it, line, -1, tag, NULL);
    else
        gtk_text_buffer_insert(b, &it, line, -1);
    it = end_iter(b);
    gtk_text_buffer_insert(b, &it, "\n", 1);
}

static void emit_desc(GtkTextBuffer *b, const DescNode *d, int depth)
{
    put(b, "header", "%*s%s", depth * 2, "", d->type);
    for (size_t i = 0; i < d->n_fields; i++)
        put(b, "mono", "%*s    %-22s %s", depth * 2, "", d->fields[i].name,
            d->fields[i].value);
    for (size_t i = 0; i < d->n_children; i++)
        emit_desc(b, d->children[i], depth + 1);
}

static void build_controller(GtkTextBuffer *b, const UsbNode *n)
{
    const ControllerInfo *c = n->controller;
    put(b, "title", "Host Controller");
    if (!c)
        return;
    const char *v = pciids_vendor(c->pci_vendor);
    const char *dv = pciids_device(c->pci_vendor, c->pci_device);
    put(b, NULL, "PCI address    : %s", c->pci_addr);
    put(b, NULL, "PCI vendor     : 0x%04x  %s", c->pci_vendor, v ? v : "");
    put(b, NULL, "PCI device     : 0x%04x  %s", c->pci_device, dv ? dv : "");
    put(b, NULL, "PCI class      : 0x%06x", c->pci_class);
    put(b, NULL, "Driver         : %s", c->driver[0] ? c->driver : "(none)");
    if (c->iommu_group >= 0)
        put(b, NULL, "IOMMU group    : %d", c->iommu_group);
}

static void build_device(GtkTextBuffer *b, const UsbNode *n)
{
    const char *vn = n->manufacturer[0] ? n->manufacturer : usbids_vendor(n->vid);
    const char *pn = n->product[0] ? n->product : usbids_product(n->vid, n->pid);

    put(b, "title", "%s — %s", vn ? vn : "Unknown", pn ? pn : "Unknown device");
    put(b, NULL, "VID:PID        : %04x:%04x", n->vid, n->pid);
    put(b, NULL, "Manufacturer   : %s", n->manufacturer[0] ? n->manufacturer : "(none)");
    put(b, NULL, "Product        : %s", n->product[0] ? n->product : "(none)");
    put(b, NULL, "Serial         : %s", n->serial[0] ? n->serial : "(none)");
    put(b, NULL, "Speed          : %s", usb_speed_str(n->speed));
    put(b, NULL, "Class          : 0x%02x (%s)", n->dev_class,
        usbids_class(n->dev_class) ? usbids_class(n->dev_class) : "");
    put(b, NULL, "USB version    : %x.%02x", n->bcd_usb >> 8, n->bcd_usb & 0xff);
    put(b, NULL, "Device version : %x.%02x", n->bcd_device >> 8, n->bcd_device & 0xff);
    put(b, NULL, "Power          : %d mA, %s", n->max_power_ma,
        n->self_powered ? "self-powered" : "bus-powered");
    if (n->power_control[0])
        put(b, NULL, "Autosuspend    : %s (%d ms), wakeup %s, %s",
            n->power_control, n->autosuspend_delay_ms,
            n->wakeup[0] ? n->wakeup : "n/a",
            n->runtime_status[0] ? n->runtime_status : "?");
    put(b, NULL, "Address        : bus %d, dev %d, port %d", n->busnum, n->devnum, n->port);
    put(b, NULL, "sysfs          : %s", n->sysfs_path);

    if (n->n_interfaces) {
        put(b, NULL, "%s", "");
        put(b, "title", "Interfaces");
        for (size_t i = 0; i < n->n_interfaces; i++) {
            const UsbInterface *ui = &n->interfaces[i];
            put(b, "mono", "  #%u alt %u  class 0x%02x/0x%02x/0x%02x  %u ep  %s",
                ui->number, ui->alt, ui->cls, ui->subcls, ui->proto,
                ui->n_endpoints, ui->driver[0] ? ui->driver : "(no driver)");
        }
    }

    BadUsbFinding f[8];
    size_t nf = badusb_analyze(n, f, 8);
    if (nf) {
        put(b, NULL, "%s", "");
        put(b, "title", "Warnings");
        for (size_t i = 0; i < nf; i++) {
            const char *tag = f[i].severity == FINDING_DANGER ? "danger"
                            : f[i].severity == FINDING_WARN ? "warn" : NULL;
            put(b, tag, "  [%s] %s", badusb_severity_str(f[i].severity), f[i].title);
            put(b, "dim", "      %s", f[i].detail);
        }
    }

    if (n->raw_descriptors && n->raw_len) {
        put(b, NULL, "%s", "");
        put(b, "title", "Descriptors");
        DescNode *d = usb_decode_descriptors(n->raw_descriptors, n->raw_len);
        if (d) {
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(b, d->children[i], 0);
            desc_node_free(d);
        }
    }

    if (n->bos_raw && n->bos_len) {
        put(b, NULL, "%s", "");
        put(b, "title", "Binary Object Store (BOS)");
        DescNode *d = usb_decode_descriptors(n->bos_raw, n->bos_len);
        if (d) {
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(b, d->children[i], 0);
            desc_node_free(d);
        }
    }
}

UeDetail *ue_detail_new(void)
{
    UeDetail *d = g_new0(UeDetail, 1);
    d->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(d->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(d->view), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(d->view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(d->view), 6);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(d->view), FALSE);
    d->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));

    gtk_text_buffer_create_tag(d->buf, "title",
                               "weight", PANGO_WEIGHT_BOLD, "scale", 1.15, NULL);
    gtk_text_buffer_create_tag(d->buf, "header",
                               "weight", PANGO_WEIGHT_BOLD,
                               "foreground", "#3584e4", NULL);
    gtk_text_buffer_create_tag(d->buf, "mono", "family", "monospace", NULL);
    gtk_text_buffer_create_tag(d->buf, "danger",
                               "weight", PANGO_WEIGHT_BOLD, "foreground", "#e01b24", NULL);
    gtk_text_buffer_create_tag(d->buf, "warn",
                               "weight", PANGO_WEIGHT_BOLD, "foreground", "#e66100", NULL);
    gtk_text_buffer_create_tag(d->buf, "dim", "foreground", "#77767b", NULL);

    d->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(d->scrolled), d->view);
    gtk_widget_set_hexpand(d->scrolled, TRUE);
    gtk_widget_set_vexpand(d->scrolled, TRUE);
    return d;
}

GtkWidget *ue_detail_widget(UeDetail *d) { return d->scrolled; }

void ue_detail_show(UeDetail *d, UsbNode *node)
{
    gtk_text_buffer_set_text(d->buf, "", 0);
    if (!node)
        return;
    if (node->kind == NODE_CONTROLLER)
        build_controller(d->buf, node);
    else
        build_device(d->buf, node);
}

void ue_detail_free(UeDetail *d) { g_free(d); }
