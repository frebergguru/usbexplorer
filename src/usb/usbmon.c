/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/usbmon.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define USBMON_DIR "/sys/kernel/debug/usb/usbmon"

volatile int usbmon_stop;

bool usbmon_available(void)
{
    return access(USBMON_DIR, R_OK | X_OK) == 0;
}

int usbmon_capture_text(int bus, int dev, char *errbuf, size_t errsz)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%du", USBMON_DIR, bus);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(errbuf, errsz, "cannot open %s: %s%s", path, strerror(errno),
                 errno == EACCES ? " (needs root; modprobe usbmon)"
                 : errno == ENOENT ? " (modprobe usbmon and mount debugfs)" : "");
        return 1;
    }

    /* The debugfs node yields one line per event:
     *   <urb-tag> <ts-us> <S|C|E> <type><dir>:<bus>:<dev>:<ep> ... */
    char buf[4096];
    char devtag[16] = "";
    if (dev > 0)
        snprintf(devtag, sizeof devtag, ":%d:%03d:", bus, dev);

    FILE *f = fdopen(fd, "r");
    if (!f) {
        close(fd);
        snprintf(errbuf, errsz, "fdopen failed");
        return 1;
    }
    while (!usbmon_stop && fgets(buf, sizeof buf, f)) {
        if (dev > 0 && !strstr(buf, devtag))
            continue;
        fputs(buf, stdout);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}

/* ---- classic pcap writer (LINKTYPE_USB_LINUX = 189) ---- */

/* The 48-byte usbmon binary header (kernel "Binary API", struct usbmon_packet).
 * Written verbatim as the pcap payload header, matching LINKTYPE_USB_LINUX. */
struct usbmon_packet {
    uint64_t id;
    uint8_t  type;
    uint8_t  xfer_type;
    uint8_t  epnum;
    uint8_t  devnum;
    uint16_t busnum;
    int8_t   flag_setup;
    int8_t   flag_data;
    int64_t  ts_sec;
    int32_t  ts_usec;
    int32_t  status;
    uint32_t length;       /* urb length            */
    uint32_t len_cap;      /* bytes captured below  */
    uint8_t  setup[8];
};

/* MON_IOCX_GET from <linux/usbdevice_fs.h>-adjacent usbmon ioctls. */
#define MON_IOC_MAGIC 0x92
struct mon_get_arg {
    struct usbmon_packet *hdr;
    void   *data;
    size_t  alloc;
};
#define MON_IOCX_GET _IOW(MON_IOC_MAGIC, 6, struct mon_get_arg)

static int write_u32(FILE *f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1 ? 0 : -1; }
static int write_u16(FILE *f, uint16_t v) { return fwrite(&v, 2, 1, f) == 1 ? 0 : -1; }

static int write_pcap_global(FILE *f)
{
    /* magic, ver 2.4, no tz/sigfigs, snaplen, LINKTYPE_USB_LINUX (189) */
    return write_u32(f, 0xa1b2c3d4) || write_u16(f, 2) || write_u16(f, 4) ||
           write_u32(f, 0) || write_u32(f, 0) || write_u32(f, 262144) ||
           write_u32(f, 189);
}

long usbmon_capture_pcap(int bus, const char *path, long max_packets,
                         char *errbuf, size_t errsz)
{
    char node[64];
    snprintf(node, sizeof node, "/dev/usbmon%d", bus);
    int fd = open(node, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(errbuf, errsz, "cannot open %s: %s%s", node, strerror(errno),
                 errno == EACCES ? " (needs root)"
                 : errno == ENOENT ? " (modprobe usbmon)" : "");
        return -1;
    }

    FILE *out = fopen(path, "wbe");
    if (!out) {
        snprintf(errbuf, errsz, "cannot write %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    if (write_pcap_global(out) != 0) {
        snprintf(errbuf, errsz, "short write to %s", path);
        fclose(out);
        close(fd);
        return -1;
    }

    long n = 0;
    uint8_t data[65536];
    struct usbmon_packet hdr;
    while (!usbmon_stop && (max_packets == 0 || n < max_packets)) {
        struct mon_get_arg arg = { .hdr = &hdr, .data = data, .alloc = sizeof data };
        if (ioctl(fd, MON_IOCX_GET, &arg) < 0) {
            if (errno == EINTR)
                continue;
            break; /* no more / error -> stop */
        }
        uint32_t caplen = 48 + hdr.len_cap;
        uint32_t origlen = 48 + hdr.length;
        write_u32(out, (uint32_t)hdr.ts_sec);
        write_u32(out, (uint32_t)hdr.ts_usec);
        write_u32(out, caplen);
        write_u32(out, origlen);
        fwrite(&hdr, sizeof hdr, 1, out);          /* 48-byte header */
        if (hdr.len_cap)
            fwrite(data, 1, hdr.len_cap, out);     /* payload */
        n++;
    }
    fclose(out);
    close(fd);
    return n;
}
