# usbexplorer XML report schema

The `--xml` mode (and the GUI "Export report" feature) emit a UTF-8 XML document
describing the USB device tree. The schema is intentionally simple and stable so
reports can be diffed between snapshots.

## Root

```xml
<usbexplorer version="0.1.0">
  ...top-level nodes...
</usbexplorer>
```

- `version` — the usbexplorer version that produced the report.

In full-tree mode the root's children are `<controller>` elements. In filtered
mode (`--device VID:PID`) the root contains a single `<devices>` element whose
children are the matching `<device>` elements (without their sub-trees).

## `<controller>`

A host controller (PCI/platform xHCI/EHCI/OHCI/UHCI). Children are `<device>`
elements (its root hubs, then the device tree beneath them).

| Attribute     | Meaning                                   |
|---------------|-------------------------------------------|
| `pciAddress`  | PCI address, e.g. `0000:62:00.3`          |
| `pciVendor`   | PCI vendor id, hex (`0x1022`)             |
| `pciDevice`   | PCI device id, hex                        |
| `pciClass`    | 24-bit PCI class/subclass/progif, hex     |
| `driver`      | kernel driver, e.g. `xhci_hcd`            |
| `iommuGroup`  | IOMMU group number, or `-1` if none       |

> PCI vendor/device *names* are not yet resolved (that needs `pci.ids`, a
> separate database from `usb.ids`); only the numeric ids are emitted.

## `<device>`

A root hub, hub, or device. Nested `<device>` children represent attached
downstream devices.

| Attribute      | Meaning                                            |
|----------------|----------------------------------------------------|
| `kind`         | `root_hub`, `hub`, or `device`                     |
| `devname`      | sysfs name (`usb1`, `1-5`, `1-5.2`)                |
| `busnum`/`devnum` | USB bus and device numbers                      |
| `port`         | port number on the parent hub (0 = root hub)       |
| `idVendor`/`idProduct` | USB VID/PID, hex                           |
| `vendorName`/`productName` | resolved names (string or `usb.ids`)   |
| `speed`        | `LS`/`FS`/`HS`/`SS`/`SS+`/`SS+20G`/`USB4`          |
| `bcdUSB`/`bcdDevice` | BCD versions, e.g. `2.01`                    |
| `bDeviceClass` | device class, hex                                  |
| `maxPowerMa`   | configured max current draw (mA)                   |
| `selfPowered`  | `true`/`false`                                     |
| `serial`       | serial number string (omitted if absent)           |
| `sysfsPath`    | absolute `/sys` path                               |

### `<interface>` (child of `<device>`)

| Attribute          | Meaning                          |
|--------------------|----------------------------------|
| `number`           | bInterfaceNumber                 |
| `alternateSetting` | bAlternateSetting                |
| `class`/`subclass`/`protocol` | interface class triple, hex |
| `driver`           | bound kernel driver (omitted if none) |

### `<descriptors>` (child of `<device>`)

Holds the decoded descriptor tree. Each descriptor is a `<descriptor>` element:

```xml
<descriptor type="Configuration Descriptor">
  <field name="wTotalLength" value="..."/>
  ...
  <descriptor type="Interface Descriptor"> ... </descriptor>
</descriptor>
```

- `type` — human-readable descriptor name.
- `<field>` — one decoded field, with `name` and decoded `value` strings.
- Nested `<descriptor>` elements model the natural hierarchy
  (configuration → interface → endpoint → companion, etc).
