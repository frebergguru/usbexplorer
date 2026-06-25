/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/enumerate.h"
#include "util/sysfs.h"

#include <dirent.h>
#include <libusb-1.0/libusb.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSFS_USB_DEVICES "/sys/bus/usb/devices"

/* ---------------------------------------------------------------- helpers */

static UsbNode *node_new(UsbNodeKind kind)
{
    UsbNode *n = calloc(1, sizeof *n);
    if (n)
        n->kind = kind;
    return n;
}

static bool node_add_child(UsbNode *parent, UsbNode *child)
{
    UsbNode **grown = realloc(parent->children,
                              (parent->n_children + 1) * sizeof *grown);
    if (!grown)
        return false;
    parent->children = grown;
    parent->children[parent->n_children++] = child;
    child->parent = parent;
    return true;
}

/* Map sysfs "speed" (Mbit/s as text) to our enum. */
static UsbSpeed speed_from_text(const char *s)
{
    if (!s || !*s)
        return SPD_UNKNOWN;
    if (strcmp(s, "1.5") == 0)
        return SPD_LS;
    int mbps = atoi(s);
    switch (mbps) {
    case 12:    return SPD_FS;
    case 480:   return SPD_HS;
    case 5000:  return SPD_SS;
    case 10000: return SPD_SSP_10;
    case 20000: return SPD_SSP_20;
    default:    return SPD_UNKNOWN;
    }
}

/* Parse a sysfs "version" string like " 2.00" into BCD 0x0200. */
static uint16_t bcd_usb_from_text(const char *s)
{
    while (*s == ' ')
        s++;
    int major = 0, minor = 0;
    if (sscanf(s, "%d.%2d", &major, &minor) < 1)
        return 0;
    return (uint16_t)(((major / 10) << 12) | ((major % 10) << 8) |
                      ((minor / 10) << 4) | (minor % 10));
}

const char *usb_speed_str(UsbSpeed s)
{
    switch (s) {
    case SPD_LS:     return "1.5 Mbit/s (Low Speed)";
    case SPD_FS:     return "12 Mbit/s (Full Speed)";
    case SPD_HS:     return "480 Mbit/s (High Speed)";
    case SPD_SS:     return "5 Gbit/s (SuperSpeed)";
    case SPD_SSP_10: return "10 Gbit/s (SuperSpeed+)";
    case SPD_SSP_20: return "20 Gbit/s (SuperSpeed+ 20G)";
    case SPD_USB4:   return "USB4";
    default:         return "unknown";
    }
}

const char *usb_speed_badge(UsbSpeed s)
{
    switch (s) {
    case SPD_LS:     return "LS";
    case SPD_FS:     return "FS";
    case SPD_HS:     return "HS";
    case SPD_SS:     return "SS";
    case SPD_SSP_10: return "SS+";
    case SPD_SSP_20: return "SS+20G";
    case SPD_USB4:   return "USB4";
    default:         return "?";
    }
}

/* Last port number encoded in a devname: "1-5"->5, "1-5.2"->2, "usb1"->0. */
static int port_from_devname(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot)
        return atoi(dot + 1);
    const char *dash = strchr(name, '-');
    if (dash)
        return atoi(dash + 1);
    return 0;
}

/* ------------------------------------------------- name -> node index map */

typedef struct {
    UsbNode **items;
    size_t n, cap;
} NodeList;

static void list_push(NodeList *l, UsbNode *n)
{
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 32;
        UsbNode **g = realloc(l->items, nc * sizeof *g);
        if (!g)
            return;
        l->items = g;
        l->cap = nc;
    }
    l->items[l->n++] = n;
}

static UsbNode *list_find(NodeList *l, const char *devname)
{
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->items[i]->devname, devname) == 0)
            return l->items[i];
    return NULL;
}

/* ------------------------------------------------ per-device attribute read */

static void read_device_attrs(UsbNode *n)
{
    const char *d = n->sysfs_path;

    n->vid = sysfs_read_hex16(d, "idVendor", 0);
    n->pid = sysfs_read_hex16(d, "idProduct", 0);
    n->bcd_device = sysfs_read_hex16(d, "bcdDevice", 0);
    n->dev_class = sysfs_read_hex8(d, "bDeviceClass", 0);
    n->dev_subclass = sysfs_read_hex8(d, "bDeviceSubClass", 0);
    n->dev_protocol = sysfs_read_hex8(d, "bDeviceProtocol", 0);
    n->max_packet0 = sysfs_read_hex8(d, "bMaxPacketSize0", 0);
    n->num_configs = (uint8_t)sysfs_read_int(d, "bNumConfigurations", 0);
    n->cur_config = (uint8_t)sysfs_read_int(d, "bConfigurationValue", 0);
    n->num_interfaces = sysfs_read_int(d, "bNumInterfaces", 0);
    n->maxchild = sysfs_read_int(d, "maxchild", 0);
    n->busnum = sysfs_read_int(d, "busnum", 0);
    n->devnum = sysfs_read_int(d, "devnum", 0);
    n->port = port_from_devname(n->devname);

    char buf[64];
    if (sysfs_read_str(d, "speed", buf, sizeof buf))
        n->speed = speed_from_text(buf);
    if (sysfs_read_str(d, "version", buf, sizeof buf))
        n->bcd_usb = bcd_usb_from_text(buf);

    sysfs_read_str(d, "manufacturer", n->manufacturer, sizeof n->manufacturer);
    sysfs_read_str(d, "product", n->product, sizeof n->product);
    sysfs_read_str(d, "serial", n->serial, sizeof n->serial);

    /* bMaxPower is like "500mA" */
    if (sysfs_read_str(d, "bMaxPower", buf, sizeof buf))
        n->max_power_ma = atoi(buf);

    uint8_t attrs = sysfs_read_hex8(d, "bmAttributes", 0);
    n->self_powered = (attrs & 0x40) != 0;
    n->remote_wakeup = (attrs & 0x20) != 0;

    /* Runtime power-management state (under <dev>/power/). */
    char pdir[PATH_MAX + 8];
    snprintf(pdir, sizeof pdir, "%s/power", d);
    sysfs_read_str(pdir, "control", n->power_control, sizeof n->power_control);
    sysfs_read_str(pdir, "wakeup", n->wakeup, sizeof n->wakeup);
    sysfs_read_str(pdir, "runtime_status", n->runtime_status, sizeof n->runtime_status);
    n->autosuspend_delay_ms = sysfs_read_int(pdir, "autosuspend_delay_ms", -1);

    sysfs_read_blob(d, "descriptors", &n->raw_descriptors, &n->raw_len);

    /* Refine node kind for hubs. */
    if (n->kind == NODE_DEVICE && (n->dev_class == 0x09 || n->maxchild > 0))
        n->kind = NODE_HUB;
}

/* ------------------------------------------------------ controller details */

/* Read a sysfs file that may contain "0x1234"; returns value or fallback. */
static unsigned long read_prefixed_hex(const char *dir, const char *attr,
                                       unsigned long fallback)
{
    char buf[32];
    if (!sysfs_read_str(dir, attr, buf, sizeof buf) || !buf[0])
        return fallback;
    return strtoul(buf, NULL, 16);
}

/* Given a root hub node, find/create its controller node. Controllers are
 * deduplicated by their PCI/parent directory path. */
static UsbNode *get_controller(NodeList *controllers, UsbNode *root_hub)
{
    char real[PATH_MAX];
    if (!realpath(root_hub->sysfs_path, real))
        return NULL;
    /* Parent directory is the host controller device. */
    char *slash = strrchr(real, '/');
    if (!slash)
        return NULL;
    *slash = '\0';
    const char *ctrl_dir = real;
    const char *ctrl_base = (slash = strrchr(real, '/')) ? slash + 1 : real;

    /* Already created? Match on the controller's sysfs path. */
    for (size_t i = 0; i < controllers->n; i++)
        if (strcmp(controllers->items[i]->sysfs_path, ctrl_dir) == 0)
            return controllers->items[i];

    UsbNode *c = node_new(NODE_CONTROLLER);
    if (!c)
        return NULL;
    snprintf(c->sysfs_path, sizeof c->sysfs_path, "%s", ctrl_dir);
    snprintf(c->devname, sizeof c->devname, "%.63s", ctrl_base);

    ControllerInfo *ci = calloc(1, sizeof *ci);
    if (ci) {
        snprintf(ci->pci_addr, sizeof ci->pci_addr, "%.31s", ctrl_base);
        ci->pci_vendor = (uint16_t)read_prefixed_hex(ctrl_dir, "vendor", 0);
        ci->pci_device = (uint16_t)read_prefixed_hex(ctrl_dir, "device", 0);
        ci->pci_class = (uint32_t)read_prefixed_hex(ctrl_dir, "class", 0);
        if (!sysfs_read_link_base(ctrl_dir, "driver", ci->driver, sizeof ci->driver))
            ci->driver[0] = '\0';
        char grp[64];
        ci->iommu_group = sysfs_read_link_base(ctrl_dir, "iommu_group", grp, sizeof grp)
                              ? atoi(grp) : -1;
        c->controller = ci;
    }

    list_push(controllers, c);
    return c;
}

/* ------------------------------------------------------------ interfaces */

/* Translate the device part of an interface name to a device node name.
 * "1-5:1.0"   -> device "1-5"
 * "1-0:1.0"   -> root hub "usb1" (port 0 means the root hub itself) */
static void iface_device_name(const char *iface, char *out, size_t out_sz)
{
    char devpart[64];
    snprintf(devpart, sizeof devpart, "%.63s", iface);
    char *colon = strchr(devpart, ':');
    if (colon)
        *colon = '\0';

    /* "<bus>-0" denotes the root hub. */
    char *dash = strchr(devpart, '-');
    if (dash && strcmp(dash + 1, "0") == 0) {
        *dash = '\0';
        snprintf(out, out_sz, "usb%.60s", devpart);
    } else {
        snprintf(out, out_sz, "%s", devpart);
    }
}

static void attach_interface(UsbNode *dev, const char *iface_path,
                             const char *iface_name)
{
    UsbInterface ui = {0};
    ui.number = sysfs_read_hex8(iface_path, "bInterfaceNumber", 0);
    ui.alt = (uint8_t)sysfs_read_int(iface_path, "bAlternateSetting", 0);
    ui.n_endpoints = sysfs_read_hex8(iface_path, "bNumEndpoints", 0);
    ui.cls = sysfs_read_hex8(iface_path, "bInterfaceClass", 0);
    ui.subcls = sysfs_read_hex8(iface_path, "bInterfaceSubClass", 0);
    ui.proto = sysfs_read_hex8(iface_path, "bInterfaceProtocol", 0);
    if (!sysfs_read_link_base(iface_path, "driver", ui.driver, sizeof ui.driver))
        ui.driver[0] = '\0';
    (void)iface_name;

    UsbInterface *grown = realloc(dev->interfaces,
                                  (dev->n_interfaces + 1) * sizeof *grown);
    if (!grown)
        return;
    dev->interfaces = grown;
    dev->interfaces[dev->n_interfaces++] = ui;
}

/* --------------------------------------------------------- parent linking */

/* Compute the devname of a device's parent. Returns false for root hubs
 * (which attach to a controller, not another device). */
static bool parent_devname(const char *name, char *out, size_t out_sz)
{
    if (strncmp(name, "usb", 3) == 0)
        return false; /* root hub */

    const char *dash = strchr(name, '-');
    if (!dash)
        return false;

    const char *dot = strrchr(name, '.');
    if (dot && dot > dash) {
        /* "1-5.2" -> "1-5" */
        size_t len = (size_t)(dot - name);
        if (len >= out_sz)
            len = out_sz - 1;
        memcpy(out, name, len);
        out[len] = '\0';
    } else {
        /* "1-5" -> "usb1" */
        size_t bus_len = (size_t)(dash - name);
        char bus[16];
        if (bus_len >= sizeof bus)
            bus_len = sizeof bus - 1;
        memcpy(bus, name, bus_len);
        bus[bus_len] = '\0';
        snprintf(out, out_sz, "usb%s", bus);
    }
    return true;
}

/* ----------------------------------------------------- child node sorting */

static int cmp_interfaces(const void *a, const void *b)
{
    const UsbInterface *x = a, *y = b;
    if (x->number != y->number)
        return (int)x->number - (int)y->number;
    return (int)x->alt - (int)y->alt;
}

static int cmp_children(const void *a, const void *b)
{
    const UsbNode *x = *(const UsbNode *const *)a;
    const UsbNode *y = *(const UsbNode *const *)b;
    if (x->port != y->port)
        return x->port - y->port;
    return strcmp(x->devname, y->devname);
}

static void sort_tree(UsbNode *n)
{
    if (n->n_children > 1)
        qsort(n->children, n->n_children, sizeof *n->children, cmp_children);
    for (size_t i = 0; i < n->n_children; i++)
        sort_tree(n->children[i]);
}

/* ------------------------------------------ libusb augmentation pass */

/* Fetch the raw BOS descriptor set via a GET_DESCRIPTOR control transfer.
 * Returns a malloc'd buffer (caller frees) or NULL. */
static uint8_t *fetch_bos_raw(libusb_device_handle *h, size_t *out_len)
{
    enum { BOS = 0x0F };
    uint8_t hdr[5];
    int r = libusb_control_transfer(
        h, LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR, (uint16_t)(BOS << 8), 0,
        hdr, sizeof hdr, 1000);
    if (r < 5)
        return NULL;
    uint16_t total = (uint16_t)(hdr[2] | (hdr[3] << 8));
    if (total < 5 || total > 4096)
        return NULL;
    uint8_t *buf = malloc(total);
    if (!buf)
        return NULL;
    r = libusb_control_transfer(
        h, LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR, (uint16_t)(BOS << 8), 0,
        buf, total, 1000);
    if (r < (int)total) {
        free(buf);
        return NULL;
    }
    *out_len = total;
    return buf;
}

/* Best-effort live probing: fill in any string descriptors sysfs lacked, and
 * fetch the BOS descriptor for USB >= 2.01 devices.  Opening a device usually
 * needs privilege, so every libusb failure here is silently ignored and we
 * simply fall back to whatever sysfs already gave us. */
static void augment_via_libusb(NodeList *devices)
{
    bool need = false;
    for (size_t i = 0; i < devices->n; i++) {
        UsbNode *n = devices->items[i];
        if (!n->vid && !n->pid)
            continue;
        if (!n->manufacturer[0] || !n->product[0] || n->bcd_usb >= 0x0201) {
            need = true;
            break;
        }
    }
    if (!need)
        return;

    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0)
        return;

    libusb_device **list = NULL;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device *dev = list[i];
        uint8_t bus = libusb_get_bus_number(dev);
        uint8_t addr = libusb_get_device_address(dev);

        UsbNode *node = NULL;
        for (size_t j = 0; j < devices->n; j++) {
            if (devices->items[j]->busnum == bus &&
                devices->items[j]->devnum == addr) {
                node = devices->items[j];
                break;
            }
        }
        if (!node)
            continue;

        bool want_strings = !node->manufacturer[0] || !node->product[0] ||
                            !node->serial[0];
        bool want_bos = node->bcd_usb >= 0x0201 && !node->bos_raw;
        if (!want_strings && !want_bos)
            continue;

        struct libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(dev, &dd) != 0)
            continue;
        libusb_device_handle *h = NULL;
        if (libusb_open(dev, &h) != 0)
            continue;

        if (want_strings) {
            unsigned char tmp[256];
            if (!node->manufacturer[0] && dd.iManufacturer &&
                libusb_get_string_descriptor_ascii(h, dd.iManufacturer, tmp, sizeof tmp) > 0)
                snprintf(node->manufacturer, sizeof node->manufacturer, "%s", tmp);
            if (!node->product[0] && dd.iProduct &&
                libusb_get_string_descriptor_ascii(h, dd.iProduct, tmp, sizeof tmp) > 0)
                snprintf(node->product, sizeof node->product, "%s", tmp);
            if (!node->serial[0] && dd.iSerialNumber &&
                libusb_get_string_descriptor_ascii(h, dd.iSerialNumber, tmp, sizeof tmp) > 0)
                snprintf(node->serial, sizeof node->serial, "%s", tmp);
        }
        if (want_bos)
            node->bos_raw = fetch_bos_raw(h, &node->bos_len);

        libusb_close(h);
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

/* ------------------------------------------------------------------ public */

UsbNode *usb_enumerate(void)
{
    DIR *dir = opendir(SYSFS_USB_DEVICES);
    if (!dir)
        return NULL;

    UsbNode *root = node_new(NODE_ROOT);
    if (!root) {
        closedir(dir);
        return NULL;
    }
    snprintf(root->devname, sizeof root->devname, "%s", "(host)");

    NodeList devices = {0};      /* root hubs + hubs + devices */
    NodeList controllers = {0};

    struct dirent *de;
    /* Pass 1: create device/root-hub nodes. */
    while ((de = readdir(dir))) {
        const char *name = de->d_name;
        if (name[0] == '.')
            continue;
        if (strchr(name, ':'))
            continue; /* interface, handled in pass 3 */

        UsbNodeKind kind = (strncmp(name, "usb", 3) == 0) ? NODE_ROOT_HUB
                                                          : NODE_DEVICE;
        UsbNode *n = node_new(kind);
        if (!n)
            continue;
        snprintf(n->devname, sizeof n->devname, "%.63s", name);
        snprintf(n->sysfs_path, sizeof n->sysfs_path, "%s/%s",
                 SYSFS_USB_DEVICES, name);
        read_device_attrs(n);
        list_push(&devices, n);
    }

    /* Pass 2: link root hubs to controllers, devices to parent devices. */
    for (size_t i = 0; i < devices.n; i++) {
        UsbNode *n = devices.items[i];
        if (n->kind == NODE_ROOT_HUB) {
            UsbNode *c = get_controller(&controllers, n);
            if (c)
                node_add_child(c, n);
            else
                node_add_child(root, n); /* fallback */
        } else {
            char pname[64];
            UsbNode *parent = NULL;
            if (parent_devname(n->devname, pname, sizeof pname))
                parent = list_find(&devices, pname);
            node_add_child(parent ? parent : root, n);
        }
    }

    /* Attach controllers under the synthetic root. */
    for (size_t i = 0; i < controllers.n; i++)
        node_add_child(root, controllers.items[i]);

    /* Pass 3: attach interfaces to their device nodes. */
    rewinddir(dir);
    while ((de = readdir(dir))) {
        const char *name = de->d_name;
        if (name[0] == '.' || !strchr(name, ':'))
            continue;
        char devname[64];
        iface_device_name(name, devname, sizeof devname);
        UsbNode *dev = list_find(&devices, devname);
        if (!dev)
            continue;
        char ipath[PATH_MAX];
        snprintf(ipath, sizeof ipath, "%s/%s", SYSFS_USB_DEVICES, name);
        attach_interface(dev, ipath, name);
    }
    closedir(dir);

    /* Present interfaces in bInterfaceNumber order regardless of readdir order. */
    for (size_t i = 0; i < devices.n; i++)
        if (devices.items[i]->n_interfaces > 1)
            qsort(devices.items[i]->interfaces, devices.items[i]->n_interfaces,
                  sizeof(UsbInterface), cmp_interfaces);

    augment_via_libusb(&devices);

    sort_tree(root);

    free(devices.items);
    free(controllers.items);
    return root;
}

void usb_tree_free(UsbNode *n)
{
    if (!n)
        return;
    for (size_t i = 0; i < n->n_children; i++)
        usb_tree_free(n->children[i]);
    free(n->children);
    free(n->interfaces);
    free(n->controller);
    free(n->raw_descriptors);
    free(n->bos_raw);
    free(n);
}

const UsbNode *usb_find_by_busdev(const UsbNode *root, int busnum, int devnum)
{
    if (!root)
        return NULL;
    if (root->kind != NODE_ROOT && root->kind != NODE_CONTROLLER &&
        root->busnum == busnum && root->devnum == devnum)
        return root;
    for (size_t i = 0; i < root->n_children; i++) {
        const UsbNode *r = usb_find_by_busdev(root->children[i], busnum, devnum);
        if (r)
            return r;
    }
    return NULL;
}

const UsbNode *usb_find_by_vidpid(const UsbNode *root, uint16_t vid, uint16_t pid)
{
    if (!root)
        return NULL;
    if (root->kind != NODE_ROOT && root->kind != NODE_CONTROLLER &&
        root->vid == vid && root->pid == pid)
        return root;
    for (size_t i = 0; i < root->n_children; i++) {
        const UsbNode *r = usb_find_by_vidpid(root->children[i], vid, pid);
        if (r)
            return r;
    }
    return NULL;
}
