/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_USB_USBMON_H
#define USBEXPLORER_USB_USBMON_H

#include <stdbool.h>
#include <stddef.h>

/*
 * usbmon integration.  Live packet capture comes from the debugfs text node
 * /sys/kernel/debug/usb/usbmon/<bus>u; a Wireshark-compatible pcap file can be
 * written from the binary /dev/usbmonN device (LINKTYPE_USB_LINUX).
 *
 * Both require the usbmon module to be loaded and (usually) root.  Every entry
 * point degrades gracefully with a clear message when usbmon is unavailable.
 */

/* True if debugfs usbmon is mounted/accessible. */
bool usbmon_available(void);

/* Set by SIGINT handlers in the CLI to stop a capture loop. */
extern volatile int usbmon_stop;

/* Live text capture of bus 'bus' to stdout until usbmon_stop is set.  If
 * 'dev' > 0, only lines for that device number are shown.  Returns 0, or an
 * exit status on error. */
int usbmon_capture_text(int bus, int dev, char *errbuf, size_t errsz);

/* Capture 'max_packets' (0 = until usbmon_stop) from the binary device for
 * bus 'bus' into a classic pcap file. Returns packets written, or -1. */
long usbmon_capture_pcap(int bus, const char *path, long max_packets,
                         char *errbuf, size_t errsz);

#endif /* USBEXPLORER_USB_USBMON_H */
