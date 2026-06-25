/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_ENUMERATE_H
#define USBEXPLORER_USB_ENUMERATE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The USB device tree.  enumerate() builds a tree rooted at a synthetic node
 * whose children are the host controllers; each controller's children are its
 * root hubs; each root hub's children are the attached devices and hubs, and
 * so on recursively.  Every frontend (CLI/TUI/GUI) renders this same tree, and
 * later feature modules annotate nodes in place.
 */

typedef enum {
    NODE_ROOT,        /* synthetic top of the tree (not a real device)      */
    NODE_CONTROLLER,  /* host controller (PCI/platform xHCI/EHCI/...)        */
    NODE_ROOT_HUB,    /* Linux root hub (sysfs "usbN")                       */
    NODE_HUB,         /* an external hub (bDeviceClass == 0x09)              */
    NODE_DEVICE,      /* a regular device                                    */
} UsbNodeKind;

typedef enum {
    SPD_UNKNOWN = 0,
    SPD_LS,       /* low speed   1.5 Mbit/s */
    SPD_FS,       /* full speed   12 Mbit/s */
    SPD_HS,       /* high speed  480 Mbit/s */
    SPD_SS,       /* SuperSpeed    5 Gbit/s */
    SPD_SSP_10,   /* SuperSpeed+  10 Gbit/s */
    SPD_SSP_20,   /* SuperSpeed+  20 Gbit/s */
    SPD_USB4,     /* USB4 / Thunderbolt     */
} UsbSpeed;

/* One USB interface as reported by sysfs (driver binding is per-interface). */
typedef struct {
    uint8_t number;       /* bInterfaceNumber    */
    uint8_t alt;          /* bAlternateSetting   */
    uint8_t n_endpoints;  /* bNumEndpoints       */
    uint8_t cls;          /* bInterfaceClass     */
    uint8_t subcls;       /* bInterfaceSubClass  */
    uint8_t proto;        /* bInterfaceProtocol  */
    char driver[64];      /* bound kernel driver, or "" */
} UsbInterface;

/* Host-controller details (present only for NODE_CONTROLLER). */
typedef struct {
    char     pci_addr[32];   /* e.g. "0000:62:00.3"; "" if not on PCI */
    uint16_t pci_vendor;
    uint16_t pci_device;
    uint32_t pci_class;      /* 24-bit PCI class/subclass/progif      */
    char     driver[32];     /* e.g. "xhci_hcd"                       */
    int      iommu_group;    /* -1 if none                            */
} ControllerInfo;

typedef struct UsbNode {
    UsbNodeKind kind;

    char sysfs_path[PATH_MAX];   /* absolute /sys path                     */
    char devname[64];            /* sysfs name: "usb1", "1-5", "1-5.2"     */

    int busnum;
    int devnum;
    int port;                    /* port number on parent hub (1-based), 0 if n/a */

    uint16_t vid, pid;
    uint16_t bcd_device;         /* bcdDevice */
    uint16_t bcd_usb;            /* bcdUSB    */
    uint8_t  dev_class, dev_subclass, dev_protocol;
    uint8_t  max_packet0;        /* bMaxPacketSize0 */
    uint8_t  num_configs;        /* bNumConfigurations */
    uint8_t  cur_config;         /* bConfigurationValue */
    int      num_interfaces;     /* bNumInterfaces */
    int      maxchild;           /* hub downstream port count, 0 if not a hub */
    UsbSpeed speed;

    char manufacturer[256];
    char product[256];
    char serial[256];

    int  max_power_ma;           /* from bMaxPower */
    bool self_powered;           /* bmAttributes bit 6 */
    bool remote_wakeup;          /* bmAttributes bit 5 */

    /* Runtime power management (from the sysfs "power" dir). "" if absent. */
    char power_control[8];       /* "auto" (autosuspend) or "on"            */
    int  autosuspend_delay_ms;   /* -1 if unknown                           */
    char wakeup[12];             /* "enabled"/"disabled"/"" (not wake-capable) */
    char runtime_status[16];     /* "active"/"suspended"/...                */

    /* Raw descriptor blob (device descriptor + all config descriptors) as
     * exposed by the sysfs "descriptors" attribute; owned by this node. */
    uint8_t *raw_descriptors;
    size_t   raw_len;

    /* Raw BOS descriptor set, fetched live via libusb (USB >= 2.01 only);
     * NULL if unavailable (older device, or no permission to open). Owned. */
    uint8_t *bos_raw;
    size_t   bos_len;

    UsbInterface  *interfaces;
    size_t         n_interfaces;

    ControllerInfo *controller;  /* non-NULL only for NODE_CONTROLLER */

    struct UsbNode  *parent;
    struct UsbNode **children;
    size_t           n_children;
} UsbNode;

/* Build the full tree from sysfs (and libusb where helpful).  Returns a
 * synthetic NODE_ROOT, or NULL on catastrophic failure (e.g. /sys not
 * mounted).  Free with usb_tree_free(). */
UsbNode *usb_enumerate(void);

void usb_tree_free(UsbNode *root);

/* Depth-first search for the device node with the given bus/device numbers,
 * or NULL if none.  Skips the synthetic root and host controllers. */
const UsbNode *usb_find_by_busdev(const UsbNode *root, int busnum, int devnum);

/* Depth-first search for the first device node matching VID:PID, or NULL. */
const UsbNode *usb_find_by_vidpid(const UsbNode *root, uint16_t vid, uint16_t pid);

/* Human-readable speed label, e.g. "480 Mbit/s (High Speed)". */
const char *usb_speed_str(UsbSpeed s);
/* Compact speed badge, e.g. "HS", "SS+", "USB4". */
const char *usb_speed_badge(UsbSpeed s);

#endif /* USBEXPLORER_USB_ENUMERATE_H */
