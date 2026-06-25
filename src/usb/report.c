/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "usb/report.h"
#include "usb/bluetooth.h"
#include "usb/speedtest.h"
#include "config.h"
#if HAVE_HISTORY
#include "usb/history.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A tiny growable string buffer backed by open_memstream. */
typedef struct { FILE *f; char *buf; size_t len; } SB;
static int sb_open(SB *s) { s->buf = NULL; s->len = 0; s->f = open_memstream(&s->buf, &s->len); return s->f != NULL; }
static char *sb_take(SB *s) { fclose(s->f); return s->buf; }

/* ----------------------------------------------------------- latency */

static double interval_us(UsbSpeed sp, uint8_t bi)
{
    if (bi == 0)
        return 0;
    return (sp == SPD_LS || sp == SPD_FS) ? bi * 1000.0
                                          : 125.0 * (double)(1u << (bi - 1));
}

char *report_latency(const UsbNode *n)
{
    SB s;
    if (!sb_open(&s))
        return NULL;
    fprintf(s.f, "%s %s (%04x:%04x) — %s\n",
            n->manufacturer[0] ? n->manufacturer : "?",
            n->product[0] ? n->product : "?", n->vid, n->pid,
            usb_speed_str(n->speed));

    const uint8_t *p = n->raw_descriptors;
    size_t len = n->raw_len, pos = 0;
    int found = 0;
    while (p && pos + 2 <= len) {
        uint8_t blen = p[pos], btype = p[pos + 1];
        if (blen < 2 || pos + blen > len)
            break;
        if (btype == 0x05 && blen >= 7) {
            uint8_t addr = p[pos + 2], attr = p[pos + 3], bi = p[pos + 6];
            const char *xn = (attr & 3) == 3 ? "Interrupt"
                           : (attr & 3) == 1 ? "Isochronous" : NULL;
            if (xn) {
                double us = interval_us(n->speed, bi);
                fprintf(s.f, "  EP 0x%02x %-3s %-11s bInterval=%u -> %.0f us"
                             " (%.0f Hz max poll)\n",
                        addr, (addr & 0x80) ? "IN" : "OUT", xn, bi, us,
                        us > 0 ? 1e6 / us : 0.0);
                found++;
            }
        }
        pos += blen;
    }
    if (!found)
        fprintf(s.f, "  (no interrupt/isochronous endpoints)\n");
    return sb_take(&s);
}

/* ----------------------------------------------------------- dmesg */

static bool kmsg_matches(const char *msg, const char *devname, uint16_t vid, uint16_t pid)
{
    char needle[96];
    snprintf(needle, sizeof needle, "usb %s:", devname);
    if (strstr(msg, needle))
        return true;
    snprintf(needle, sizeof needle, " %s:", devname);
    if (strstr(msg, needle))
        return true;
    char v[24], pn[24];
    snprintf(v, sizeof v, "idVendor=%04x", vid);
    snprintf(pn, sizeof pn, "idProduct=%04x", pid);
    return strstr(msg, v) && strstr(msg, pn);
}

char *report_dmesg(const UsbNode *n)
{
    SB s;
    if (!sb_open(&s))
        return NULL;

    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(s.f, "cannot open /dev/kmsg: %s%s\n", strerror(errno),
                errno == EACCES ? " (kernel log usually needs root)" : "");
        return sb_take(&s);
    }
    fprintf(s.f, "# dmesg for %s (%04x:%04x)\n", n->devname, n->vid, n->pid);

    char rec[8192];
    int matches = 0;
    for (;;) {
        ssize_t len = read(fd, rec, sizeof rec - 1);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EPIPE)
                continue;
            break;
        }
        rec[len] = '\0';
        char *semi = strchr(rec, ';');
        if (!semi)
            continue;
        unsigned long long ts = 0;
        sscanf(rec, "%*u,%*u,%llu", &ts);
        *semi = '\0';
        char *msg = semi + 1;
        char *nl = strchr(msg, '\n');
        if (nl)
            *nl = '\0';
        if (kmsg_matches(msg, n->devname, n->vid, n->pid)) {
            fprintf(s.f, "[%5llu.%06llu] %s\n", ts / 1000000ULL, ts % 1000000ULL, msg);
            matches++;
        }
    }
    close(fd);
    if (matches == 0)
        fprintf(s.f, "# (no matching messages in the kernel ring buffer)\n");
    return sb_take(&s);
}

/* ----------------------------------------------------------- speed test */

static double link_ceiling(UsbSpeed sp)
{
    switch (sp) {
    case SPD_LS: return 0.18; case SPD_FS: return 1.0; case SPD_HS: return 45.0;
    case SPD_SS: return 450.0; case SPD_SSP_10: return 1000.0;
    case SPD_SSP_20: return 2000.0; default: return 0.0;
    }
}

char *report_speedtest(const UsbNode *n, long max_bytes)
{
    SB s;
    if (!sb_open(&s))
        return NULL;
    fprintf(s.f, "Device:     %s %s (%04x:%04x)\n",
            n->manufacturer[0] ? n->manufacturer : "?",
            n->product[0] ? n->product : "?", n->vid, n->pid);
    fprintf(s.f, "Link speed: %s\n", usb_speed_str(n->speed));

    SpeedResult r;
    if (!speedtest_run(n, max_bytes, &r)) {
        fprintf(s.f, "Speed test failed: %s\n", r.error);
        return sb_take(&s);
    }
    fprintf(s.f, "Block dev:  %s\n", r.devnode);
    fprintf(s.f, "Read:       %.1f MB in %.2f s  =>  %.1f MB/s\n",
            r.bytes / 1e6, r.seconds, r.mb_per_s);
    double ceil_mbps = link_ceiling(n->speed);
    if (ceil_mbps > 0) {
        double pct = 100.0 * r.mb_per_s / ceil_mbps;
        fprintf(s.f, "Link usage: %.0f%% of ~%.0f MB/s usable on this link\n",
                pct, ceil_mbps);
        if (pct < 50.0)
            fprintf(s.f, "Note:       below the link's capability — the drive, "
                         "not USB, is the bottleneck.\n");
    }
    return sb_take(&s);
}

/* ----------------------------------------------------------- bluetooth */

char *report_bluetooth(void)
{
    SB s;
    if (!sb_open(&s))
        return NULL;
    BtList *l = bt_enumerate();
    if (l->error[0]) {
        fprintf(s.f, "%s\n", l->error);
        bt_list_free(l);
        return sb_take(&s);
    }
    fprintf(s.f, "Adapters (%zu):\n", l->n_adapters);
    for (size_t i = 0; i < l->n_adapters; i++)
        fprintf(s.f, "  %s  %-20s  %s\n", l->adapters[i].address,
                l->adapters[i].name, l->adapters[i].powered ? "powered" : "off");
    fprintf(s.f, "Devices (%zu):\n", l->n_devices);
    for (size_t i = 0; i < l->n_devices; i++) {
        BtDevice *d = &l->devices[i];
        fprintf(s.f, "  %s  %-24s class 0x%06x  %s%s%s\n", d->address, d->name,
                d->class, d->paired ? "paired " : "",
                d->connected ? "connected " : "", d->trusted ? "trusted" : "");
    }
    bt_list_free(l);
    return sb_take(&s);
}

/* ----------------------------------------------------------- history */

char *report_history(int csv)
{
#if HAVE_HISTORY
    SB s;
    if (!sb_open(&s))
        return NULL;
    UsbHistory *h = usb_history_open();
    if (!h) {
        fprintf(s.f, "cannot open the history database\n");
        return sb_take(&s);
    }
    usb_history_dump(h, 0, csv, s.f);
    usb_history_close(h);
    char *out = sb_take(&s);
    if (out && !out[0]) {
        free(out);
        return strdup("(no recorded events yet)\n");
    }
    return out;
#else
    (void)csv;
    return strdup("built without history support\n");
#endif
}
