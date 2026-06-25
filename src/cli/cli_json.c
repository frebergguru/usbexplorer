/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/badusb.h"
#include "usb/descriptors.h"
#include "util/json.h"
#include "util/pciids.h"
#include "util/usbids.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex_string(const uint8_t *p, size_t len, char *out, size_t out_sz)
{
    size_t off = 0;
    for (size_t i = 0; i < len && off + 3 < out_sz; i++)
        off += (size_t)snprintf(out + off, out_sz - off, i ? " %02x" : "%02x", p[i]);
}

static void emit_desc(JsonWriter *w, const DescNode *d)
{
    json_begin_object(w, NULL);
    json_str(w, "type", d->type);

    if (d->n_fields) {
        json_begin_array(w, "fields");
        for (size_t i = 0; i < d->n_fields; i++) {
            json_begin_object(w, NULL);
            json_str(w, "name", d->fields[i].name);
            json_str(w, "value", d->fields[i].value);
            json_end_object(w);
        }
        json_end_array(w);
    }

    if (d->raw && d->raw_len && d->raw_len <= 512) {
        char hex[1600];
        hex_string(d->raw, d->raw_len, hex, sizeof hex);
        json_str(w, "raw", hex);
    }

    if (d->n_children) {
        json_begin_array(w, "children");
        for (size_t i = 0; i < d->n_children; i++)
            emit_desc(w, d->children[i]);
        json_end_array(w);
    }
    json_end_object(w);
}

static void emit_str_or_null(JsonWriter *w, const char *key, const char *s)
{
    if (s && *s)
        json_str(w, key, s);
    else
        json_null(w, key);
}

/* Common device fields shared by tree and filtered output. */
static void emit_device_core(JsonWriter *w, const UsbNode *n)
{
    char buf[16];

    json_str(w, "kind", n->kind == NODE_ROOT_HUB ? "root_hub"
                       : n->kind == NODE_HUB ? "hub" : "device");
    json_str(w, "devname", n->devname);
    json_int(w, "busnum", n->busnum);
    json_int(w, "devnum", n->devnum);
    json_int(w, "port", n->port);

    snprintf(buf, sizeof buf, "0x%04x", n->vid);
    json_str(w, "idVendor", buf);
    snprintf(buf, sizeof buf, "0x%04x", n->pid);
    json_str(w, "idProduct", buf);
    emit_str_or_null(w, "vendor_name",
                     n->manufacturer[0] ? n->manufacturer : usbids_vendor(n->vid));
    emit_str_or_null(w, "product_name",
                     n->product[0] ? n->product : usbids_product(n->vid, n->pid));

    snprintf(buf, sizeof buf, "0x%02x", n->dev_class);
    json_str(w, "bDeviceClass", buf);
    emit_str_or_null(w, "class_name", usbids_class(n->dev_class));
    json_int(w, "bDeviceSubClass", n->dev_subclass);
    json_int(w, "bDeviceProtocol", n->dev_protocol);

    json_str(w, "speed", usb_speed_badge(n->speed));
    json_str(w, "speed_detail", usb_speed_str(n->speed));
    snprintf(buf, sizeof buf, "%x.%02x", n->bcd_usb >> 8, n->bcd_usb & 0xff);
    json_str(w, "bcdUSB", buf);
    snprintf(buf, sizeof buf, "%x.%02x", n->bcd_device >> 8, n->bcd_device & 0xff);
    json_str(w, "bcdDevice", buf);

    emit_str_or_null(w, "manufacturer", n->manufacturer);
    emit_str_or_null(w, "product", n->product);
    emit_str_or_null(w, "serial", n->serial);

    json_int(w, "max_power_ma", n->max_power_ma);
    json_bool(w, "self_powered", n->self_powered);
    json_bool(w, "remote_wakeup", n->remote_wakeup);
    emit_str_or_null(w, "power_control", n->power_control);
    json_int(w, "autosuspend_delay_ms", n->autosuspend_delay_ms);
    emit_str_or_null(w, "wakeup", n->wakeup);
    emit_str_or_null(w, "runtime_status", n->runtime_status);
    json_int(w, "num_configurations", n->num_configs);
    json_int(w, "current_configuration", n->cur_config);
    json_str(w, "sysfs_path", n->sysfs_path);

    if (n->n_interfaces) {
        json_begin_array(w, "interfaces");
        for (size_t i = 0; i < n->n_interfaces; i++) {
            const UsbInterface *ui = &n->interfaces[i];
            json_begin_object(w, NULL);
            json_int(w, "number", ui->number);
            json_int(w, "alternate_setting", ui->alt);
            json_int(w, "num_endpoints", ui->n_endpoints);
            snprintf(buf, sizeof buf, "0x%02x", ui->cls);
            json_str(w, "class", buf);
            emit_str_or_null(w, "class_name", usbids_class(ui->cls));
            json_int(w, "subclass", ui->subcls);
            json_int(w, "protocol", ui->proto);
            emit_str_or_null(w, "driver", ui->driver);
            json_end_object(w);
        }
        json_end_array(w);
    }

    if (n->raw_descriptors && n->raw_len) {
        DescNode *d = usb_decode_descriptors(n->raw_descriptors, n->raw_len);
        if (d) {
            json_begin_array(w, "descriptors");
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(w, d->children[i]);
            json_end_array(w);
            desc_node_free(d);
        }
    }

    if (n->bos_raw && n->bos_len) {
        DescNode *d = usb_decode_descriptors(n->bos_raw, n->bos_len);
        if (d) {
            json_begin_array(w, "bos");
            for (size_t i = 0; i < d->n_children; i++)
                emit_desc(w, d->children[i]);
            json_end_array(w);
            desc_node_free(d);
        }
    }

    BadUsbFinding finds[8];
    size_t nf = badusb_analyze(n, finds, 8);
    if (nf) {
        json_begin_array(w, "warnings");
        for (size_t i = 0; i < nf; i++) {
            json_begin_object(w, NULL);
            json_str(w, "severity", badusb_severity_str(finds[i].severity));
            json_str(w, "title", finds[i].title);
            json_str(w, "detail", finds[i].detail);
            json_end_object(w);
        }
        json_end_array(w);
    }
}

static void emit_controller(JsonWriter *w, const UsbNode *n);

static void emit_node(JsonWriter *w, const UsbNode *n)
{
    if (n->kind == NODE_CONTROLLER) {
        emit_controller(w, n);
        return;
    }
    json_begin_object(w, NULL);
    emit_device_core(w, n);
    if (n->n_children) {
        json_begin_array(w, "children");
        for (size_t i = 0; i < n->n_children; i++)
            emit_node(w, n->children[i]);
        json_end_array(w);
    }
    json_end_object(w);
}

static void emit_controller(JsonWriter *w, const UsbNode *n)
{
    char buf[16];
    json_begin_object(w, NULL);
    json_str(w, "kind", "controller");
    if (n->controller) {
        json_str(w, "pci_address", n->controller->pci_addr);
        snprintf(buf, sizeof buf, "0x%04x", n->controller->pci_vendor);
        json_str(w, "pci_vendor", buf);
        emit_str_or_null(w, "pci_vendor_name", pciids_vendor(n->controller->pci_vendor));
        snprintf(buf, sizeof buf, "0x%04x", n->controller->pci_device);
        json_str(w, "pci_device", buf);
        emit_str_or_null(w, "pci_device_name",
                         pciids_device(n->controller->pci_vendor, n->controller->pci_device));
        snprintf(buf, sizeof buf, "0x%06x", n->controller->pci_class);
        json_str(w, "pci_class", buf);
        emit_str_or_null(w, "driver", n->controller->driver);
        json_int(w, "iommu_group", n->controller->iommu_group);
    }
    if (n->n_children) {
        json_begin_array(w, "children");
        for (size_t i = 0; i < n->n_children; i++)
            emit_node(w, n->children[i]);
        json_end_array(w);
    }
    json_end_object(w);
}

/* Collect every device node matching the filter into a flat array. */
static void emit_matches(JsonWriter *w, const UsbNode *n, const DeviceFilter *f)
{
    if (device_matches(n, f)) {
        json_begin_object(w, NULL);
        emit_device_core(w, n);
        json_end_object(w);
    }
    for (size_t i = 0; i < n->n_children; i++)
        emit_matches(w, n->children[i], f);
}

void cli_json(const UsbNode *root, const DeviceFilter *f, bool pretty)
{
    JsonWriter *w = json_new(stdout, pretty);
    if (!w)
        return;

    json_begin_object(w, NULL);
    json_str(w, "tool", "usbexplorer");
    json_str(w, "version", PROJECT_VERSION);

    if (f && f->active) {
        json_begin_array(w, "devices");
        for (size_t i = 0; i < root->n_children; i++)
            emit_matches(w, root->children[i], f);
        json_end_array(w);
    } else {
        json_begin_array(w, "tree");
        for (size_t i = 0; i < root->n_children; i++)
            emit_node(w, root->children[i]);
        json_end_array(w);
    }
    json_end_object(w);
    json_free(w);
}
