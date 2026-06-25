/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/usbmon.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_int(int sig) { (void)sig; usbmon_stop = 1; }

static void install_handler(void)
{
    struct sigaction sa = { .sa_handler = on_int };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int cli_usbmon(const char *arg)
{
    if (!usbmon_available()) {
        fprintf(stderr, "usbexplorer: usbmon not available "
                        "(run: sudo modprobe usbmon; mount -t debugfs none /sys/kernel/debug)\n");
        return 1;
    }
    int bus = 0, dev = 0;
    sscanf(arg, "%d.%d", &bus, &dev);
    if (bus <= 0) {
        fprintf(stderr, "usbexplorer: --usbmon expects BUSNUM[.DEVNUM]\n");
        return 2;
    }
    install_handler();
    char err[256];
    int rc = usbmon_capture_text(bus, dev, err, sizeof err);
    if (rc)
        fprintf(stderr, "usbexplorer: %s\n", err);
    return rc;
}

int cli_usbmon_pcap(const char *arg)
{
    /* "BUS:FILE" */
    char spec[256];
    snprintf(spec, sizeof spec, "%s", arg);
    char *colon = strchr(spec, ':');
    if (!colon) {
        fprintf(stderr, "usbexplorer: --usbmon-pcap expects BUSNUM:FILE\n");
        return 2;
    }
    *colon = '\0';
    int bus = atoi(spec);
    const char *file = colon + 1;

    install_handler();
    char err[256];
    long n = usbmon_capture_pcap(bus, file, 0, err, sizeof err);
    if (n < 0) {
        fprintf(stderr, "usbexplorer: %s\n", err);
        return 1;
    }
    fprintf(stderr, "usbexplorer: wrote %ld packets to %s\n", n, file);
    return 0;
}
