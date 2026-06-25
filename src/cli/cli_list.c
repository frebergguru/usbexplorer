/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/badusb.h"
#include "util/pciids.h"
#include "util/usbids.h"

#include <stdio.h>
#include <strings.h>

/* Leading marker for devices with a security/hygiene finding. */
static const char *warn_marker(const UsbNode *n)
{
    switch (badusb_worst(n)) {
    case FINDING_DANGER: return "[!] ";
    case FINDING_WARN:   return "[*] ";
    default:             return "";
    }
}

bool node_is_device(const UsbNode *n)
{
    return n && (n->kind == NODE_DEVICE || n->kind == NODE_HUB ||
                 n->kind == NODE_ROOT_HUB);
}

bool device_matches(const UsbNode *n, const DeviceFilter *f)
{
    if (!node_is_device(n))
        return false;
    if (!f || !f->active)
        return true;
    if (f->have_vid_pid && (n->vid != f->vid || n->pid != f->pid))
        return false;
    if (f->serial && strcasecmp(n->serial, f->serial) != 0)
        return false;
    return true;
}

/* Best-effort display name: prefer the product string, fall back to the
 * usb.ids database, then to "VID:PID". */
static void device_label(const UsbNode *n, char *out, size_t out_sz)
{
    const char *vendor = n->manufacturer[0] ? n->manufacturer
                                             : usbids_vendor(n->vid);
    const char *product = n->product[0] ? n->product
                                        : usbids_product(n->vid, n->pid);
    if (vendor && product)
        snprintf(out, out_sz, "%.190s %.190s", vendor, product);
    else if (product)
        snprintf(out, out_sz, "%.250s", product);
    else if (vendor)
        snprintf(out, out_sz, "%.250s", vendor);
    else
        snprintf(out, out_sz, "%04x:%04x", n->vid, n->pid);
}

static void walk_list(const UsbNode *n, const DeviceFilter *f)
{
    if (device_matches(n, f)) {
        char label[400];
        device_label(n, label, sizeof label);
        printf("Bus %03d Device %03d: ID %04x:%04x [%-3s] %s%s\n",
               n->busnum, n->devnum, n->vid, n->pid,
               usb_speed_badge(n->speed), warn_marker(n), label);
    }
    for (size_t i = 0; i < n->n_children; i++)
        walk_list(n->children[i], f);
}

void cli_list(const UsbNode *root, const DeviceFilter *f)
{
    for (size_t i = 0; i < root->n_children; i++)
        walk_list(root->children[i], f);
}

/* -------------------------------------------------------------- tree mode */

static bool subtree_match(const UsbNode *n, const DeviceFilter *f)
{
    if (device_matches(n, f))
        return true;
    for (size_t i = 0; i < n->n_children; i++)
        if (subtree_match(n->children[i], f))
            return true;
    return false;
}

static void node_line(const UsbNode *n, char *out, size_t out_sz)
{
    char label[400];
    switch (n->kind) {
    case NODE_CONTROLLER: {
        const char *cn = NULL;
        if (n->controller) {
            cn = pciids_device(n->controller->pci_vendor,
                               n->controller->pci_device);
            if (!cn)
                cn = pciids_vendor(n->controller->pci_vendor);
        }
        snprintf(out, out_sz, "Host Controller %s  %s  [%04x:%04x]%s%s",
                 n->controller ? n->controller->pci_addr : n->devname,
                 n->controller && n->controller->driver[0] ? n->controller->driver : "?",
                 n->controller ? n->controller->pci_vendor : 0,
                 n->controller ? n->controller->pci_device : 0,
                 cn ? "  " : "", cn ? cn : "");
        break;
    }
    case NODE_ROOT_HUB:
        device_label(n, label, sizeof label);
        snprintf(out, out_sz, "%s  %s  [%s]", n->devname, label,
                 usb_speed_badge(n->speed));
        break;
    default:
        device_label(n, label, sizeof label);
        snprintf(out, out_sz, "%sPort %d: %s  %04x:%04x  [%s]", warn_marker(n),
                 n->port, label, n->vid, n->pid, usb_speed_badge(n->speed));
        break;
    }
}

/* Recursively print the children (and, optionally, the interfaces) of 'n',
 * each prefixed by tree-drawing characters.  'prefix' is the indentation
 * carried down from ancestors.  Interfaces are listed first, then child
 * devices, with the very last item using the "└─" elbow connector. */
static void print_children(const UsbNode *n, const char *prefix,
                           const DeviceFilter *f, bool show_if)
{
    size_t n_ifaces = (show_if && node_is_device(n)) ? n->n_interfaces : 0;
    size_t n_visible = 0;
    for (size_t i = 0; i < n->n_children; i++)
        if (subtree_match(n->children[i], f))
            n_visible++;

    size_t total = n_ifaces + n_visible, idx = 0;

    for (size_t i = 0; i < n_ifaces; i++) {
        const UsbInterface *ui = &n->interfaces[i];
        bool last = (++idx == total);
        printf("%s%sInterface %u: class 0x%02x/0x%02x/0x%02x%s%s\n",
               prefix, last ? "└─ " : "├─ ", ui->number, ui->cls, ui->subcls,
               ui->proto, ui->driver[0] ? "  driver=" : "", ui->driver);
    }

    for (size_t i = 0; i < n->n_children; i++) {
        if (!subtree_match(n->children[i], f))
            continue;
        bool last = (++idx == total);

        char line[512];
        node_line(n->children[i], line, sizeof line);
        printf("%s%s%s\n", prefix, last ? "└─ " : "├─ ", line);

        char child_prefix[256];
        snprintf(child_prefix, sizeof child_prefix, "%s%s", prefix,
                 last ? "   " : "│  ");
        print_children(n->children[i], child_prefix, f, show_if);
    }
}

void cli_tree(const UsbNode *root, const DeviceFilter *f, bool show_interfaces)
{
    /* Top-level nodes (host controllers) are printed flush-left; their
     * subtrees are drawn with connectors. */
    for (size_t i = 0; i < root->n_children; i++) {
        if (!subtree_match(root->children[i], f))
            continue;
        char line[512];
        node_line(root->children[i], line, sizeof line);
        printf("%s\n", line);
        print_children(root->children[i], "", f, show_interfaces);
    }
}
