/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/monitor.h"
#include "util/json.h"
#include "config.h"
#if HAVE_HISTORY
#include "usb/history.h"
#endif

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void iso_timestamp(const struct timespec *ts, char *out, size_t out_sz)
{
    struct tm tm;
    gmtime_r(&ts->tv_sec, &tm);
    size_t n = strftime(out, out_sz, "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(out + n, out_sz - n, ".%03ldZ", ts->tv_nsec / 1000000);
}

static void emit_event(const UsbEvent *e, const struct timespec *now,
                       const struct timespec *prev, bool have_prev)
{
    char ts[40];
    iso_timestamp(now, ts, sizeof ts);

    JsonWriter *w = json_new(stdout, false);
    if (!w)
        return;
    json_begin_object(w, NULL);
    json_str(w, "time", ts);
    if (have_prev) {
        long long dms = (now->tv_sec - prev->tv_sec) * 1000LL +
                        (now->tv_nsec - prev->tv_nsec) / 1000000LL;
        json_int(w, "delta_ms", dms);
    } else {
        json_null(w, "delta_ms");
    }
    json_str(w, "action", e->action);
    json_str(w, "devtype", e->devtype[0] ? e->devtype : NULL);
    json_str(w, "sysname", e->sysname);
    if (e->has_ids) {
        char buf[16];
        snprintf(buf, sizeof buf, "0x%04x", e->vid);
        json_str(w, "idVendor", buf);
        snprintf(buf, sizeof buf, "0x%04x", e->pid);
        json_str(w, "idProduct", buf);
    }
    json_str(w, "driver", e->driver[0] ? e->driver : NULL);
    json_str(w, "syspath", e->syspath);
    json_end_object(w);
    json_free(w);
    fputc('\n', stdout);
    fflush(stdout);
}

static bool event_passes(const UsbEvent *e, const DeviceFilter *f)
{
    if (!f || !f->active)
        return true;
    if (f->have_vid_pid) {
        if (!e->has_ids || e->vid != f->vid || e->pid != f->pid)
            return false;
    }
    return true;
}

void cli_watch(const DeviceFilter *f)
{
    UsbMonitor *mon = usb_monitor_new();
    if (!mon) {
        fprintf(stderr, "usbexplorer: failed to start udev monitor\n");
        return;
    }

    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

#if HAVE_HISTORY
    UsbHistory *hist = usb_history_open();
#endif

    struct timespec prev = {0};
    bool have_prev = false;

    struct pollfd pfd = { .fd = usb_monitor_fd(mon), .events = POLLIN };
    while (!g_stop) {
        int r = poll(&pfd, 1, 500);
        if (r < 0)
            break; /* interrupted by signal -> loop check g_stop */
        if (r == 0)
            continue;

        UsbEvent e;
        while (usb_monitor_next(mon, &e)) {
            if (!event_passes(&e, f))
                continue;
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            emit_event(&e, &now, &prev, have_prev);
            prev = now;
            have_prev = true;
#if HAVE_HISTORY
            if ((e.type == USB_EV_ADD || e.type == USB_EV_REMOVE) &&
                strcmp(e.devtype, "usb_device") == 0)
                usb_history_record(hist, e.action, e.vid, e.pid, "", "", e.sysname);
#endif
        }
    }

#if HAVE_HISTORY
    usb_history_close(hist);
#endif
    usb_monitor_free(mon);
}
