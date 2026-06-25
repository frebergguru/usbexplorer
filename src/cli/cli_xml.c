/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/badusb.h"
#include "usb/descriptors.h"
#include "util/pciids.h"
#include "util/usbids.h"
#include "util/xml.h"
#include "config.h"

#include <stdio.h>

/*
 * XML report schema (documented in docs/xml-schema.md):
 *   <usbexplorer version="..">
 *     <controller .../>* | <devices><device/>*</devices>
 *   ...
 * Every <device> carries its attributes, <interface> children, a
 * <descriptors> subtree, and (in tree mode) nested child <device> elements.
 */

static void emit_desc(XmlWriter *w, const DescNode *d)
{
    xml_start(w, "descriptor");
    xml_attr(w, "type", "%s", d->type);
    for (size_t i = 0; i < d->n_fields; i++) {
        xml_start(w, "field");
        xml_attr(w, "name", "%s", d->fields[i].name);
        xml_attr(w, "value", "%s", d->fields[i].value);
        xml_end(w); /* field */
    }
    for (size_t i = 0; i < d->n_children; i++)
        emit_desc(w, d->children[i]);
    xml_end(w); /* descriptor */
}

static void emit_device_core(XmlWriter *w, const UsbNode *n)
{
    xml_attr(w, "kind", "%s", n->kind == NODE_ROOT_HUB ? "root_hub"
                            : n->kind == NODE_HUB ? "hub" : "device");
    xml_attr(w, "devname", "%s", n->devname);
    xml_attr(w, "busnum", "%d", n->busnum);
    xml_attr(w, "devnum", "%d", n->devnum);
    xml_attr(w, "port", "%d", n->port);
    xml_attr(w, "idVendor", "0x%04x", n->vid);
    xml_attr(w, "idProduct", "0x%04x", n->pid);
    xml_attr(w, "speed", "%s", usb_speed_badge(n->speed));
    xml_attr(w, "bcdUSB", "%x.%02x", n->bcd_usb >> 8, n->bcd_usb & 0xff);
    xml_attr(w, "bcdDevice", "%x.%02x", n->bcd_device >> 8, n->bcd_device & 0xff);
    xml_attr(w, "bDeviceClass", "0x%02x", n->dev_class);
    xml_attr(w, "maxPowerMa", "%d", n->max_power_ma);
    xml_attr(w, "selfPowered", "%s", n->self_powered ? "true" : "false");
    xml_attr(w, "sysfsPath", "%s", n->sysfs_path);

    const char *vn = n->manufacturer[0] ? n->manufacturer : usbids_vendor(n->vid);
    const char *pn = n->product[0] ? n->product : usbids_product(n->vid, n->pid);
    if (vn) xml_attr(w, "vendorName", "%s", vn);
    if (pn) xml_attr(w, "productName", "%s", pn);
    if (n->serial[0]) xml_attr(w, "serial", "%s", n->serial);

    for (size_t i = 0; i < n->n_interfaces; i++) {
        const UsbInterface *ui = &n->interfaces[i];
        xml_start(w, "interface");
        xml_attr(w, "number", "%u", ui->number);
        xml_attr(w, "alternateSetting", "%u", ui->alt);
        xml_attr(w, "class", "0x%02x", ui->cls);
        xml_attr(w, "subclass", "0x%02x", ui->subcls);
        xml_attr(w, "protocol", "0x%02x", ui->proto);
        if (ui->driver[0])
            xml_attr(w, "driver", "%s", ui->driver);
        xml_end(w);
    }

    if (n->raw_descriptors && n->raw_len) {
        DescNode *d = usb_decode_descriptors(n->raw_descriptors, n->raw_len);
        if (d) {
            xml_start(w, "descriptors");
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(w, d->children[i]);
            xml_end(w);
            desc_node_free(d);
        }
    }

    if (n->bos_raw && n->bos_len) {
        DescNode *d = usb_decode_descriptors(n->bos_raw, n->bos_len);
        if (d) {
            xml_start(w, "bos");
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(w, d->children[i]);
            xml_end(w);
            desc_node_free(d);
        }
    }

    BadUsbFinding finds[8];
    size_t nf = badusb_analyze(n, finds, 8);
    for (size_t i = 0; i < nf; i++) {
        xml_start(w, "warning");
        xml_attr(w, "severity", "%s", badusb_severity_str(finds[i].severity));
        xml_attr(w, "title", "%s", finds[i].title);
        xml_text(w, finds[i].detail);
        xml_end(w);
    }
}

static void emit_node(XmlWriter *w, const UsbNode *n);

static void emit_controller(XmlWriter *w, const UsbNode *n)
{
    xml_start(w, "controller");
    if (n->controller) {
        xml_attr(w, "pciAddress", "%s", n->controller->pci_addr);
        xml_attr(w, "pciVendor", "0x%04x", n->controller->pci_vendor);
        xml_attr(w, "pciDevice", "0x%04x", n->controller->pci_device);
        const char *pv = pciids_vendor(n->controller->pci_vendor);
        const char *pd = pciids_device(n->controller->pci_vendor,
                                       n->controller->pci_device);
        if (pv) xml_attr(w, "pciVendorName", "%s", pv);
        if (pd) xml_attr(w, "pciDeviceName", "%s", pd);
        xml_attr(w, "pciClass", "0x%06x", n->controller->pci_class);
        if (n->controller->driver[0])
            xml_attr(w, "driver", "%s", n->controller->driver);
        xml_attr(w, "iommuGroup", "%d", n->controller->iommu_group);
    }
    for (size_t i = 0; i < n->n_children; i++)
        emit_node(w, n->children[i]);
    xml_end(w);
}

static void emit_node(XmlWriter *w, const UsbNode *n)
{
    if (n->kind == NODE_CONTROLLER) {
        emit_controller(w, n);
        return;
    }
    xml_start(w, "device");
    emit_device_core(w, n);
    for (size_t i = 0; i < n->n_children; i++)
        emit_node(w, n->children[i]);
    xml_end(w);
}

static void emit_matches(XmlWriter *w, const UsbNode *n, const DeviceFilter *f)
{
    if (device_matches(n, f)) {
        xml_start(w, "device");
        emit_device_core(w, n);
        xml_end(w);
    }
    for (size_t i = 0; i < n->n_children; i++)
        emit_matches(w, n->children[i], f);
}

void cli_xml(const UsbNode *root, const DeviceFilter *f)
{
    XmlWriter *w = xml_new(stdout);
    if (!w)
        return;

    xml_start(w, "usbexplorer");
    xml_attr(w, "version", "%s", PROJECT_VERSION);

    if (f && f->active) {
        xml_start(w, "devices");
        for (size_t i = 0; i < root->n_children; i++)
            emit_matches(w, root->children[i], f);
        xml_end(w);
    } else {
        for (size_t i = 0; i < root->n_children; i++)
            emit_node(w, root->children[i]);
    }
    xml_end(w); /* usbexplorer */
    xml_free(w);
}
