/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/pciids.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * pci.ids uses the same layout as usb.ids, but we only need the
 * vendor/device portion: a vendor line at column 0 ("vvvv  Name"), then
 * tab-indented device lines ("dddd  Name").  Deeper nesting (subsystems) and
 * the class section ("C ...") are skipped.
 */

typedef struct {
    uint16_t id;
    char *name;
} Device;

typedef struct {
    uint16_t id;
    char *name;
    Device *devices;
    size_t n_devices, cap_devices;
} Vendor;

static Vendor *g_vendors;
static size_t g_n, g_cap;
static bool g_loaded;

static const char *const PATHS[] = {
    "/usr/share/hwdata/pci.ids",
    "/usr/share/pci.ids",
    "/usr/share/misc/pci.ids",
};

static const char *parse_entry(const char *s, unsigned long *id)
{
    char *end = NULL;
    *id = strtoul(s, &end, 16);
    if (end == s)
        return NULL;
    while (*end == ' ' || *end == '\t')
        end++;
    return end;
}

static void load_from(FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    Vendor *cur = NULL;
    bool in_vendor_section = false;

    while ((len = getline(&line, &cap, f)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;

        if (line[0] != '\t') {
            /* Top-level: a vendor (4 hex) or the start of another section. */
            if (!isxdigit((unsigned char)line[0])) {
                in_vendor_section = false; /* e.g. "C 00  ..." class section */
                cur = NULL;
                continue;
            }
            unsigned long id;
            const char *name = parse_entry(line, &id);
            if (!name)
                continue;
            if (g_n == g_cap) {
                size_t nc = g_cap ? g_cap * 2 : 256;
                Vendor *p = realloc(g_vendors, nc * sizeof *p);
                if (!p)
                    break;
                g_vendors = p;
                g_cap = nc;
            }
            cur = &g_vendors[g_n++];
            cur->id = (uint16_t)id;
            cur->name = strdup(name);
            cur->devices = NULL;
            cur->n_devices = cur->cap_devices = 0;
            in_vendor_section = true;
        } else if (line[0] == '\t' && line[1] != '\t' && in_vendor_section && cur) {
            unsigned long id;
            const char *name = parse_entry(line + 1, &id);
            if (!name)
                continue;
            if (cur->n_devices == cur->cap_devices) {
                size_t nc = cur->cap_devices ? cur->cap_devices * 2 : 8;
                Device *p = realloc(cur->devices, nc * sizeof *p);
                if (!p)
                    continue;
                cur->devices = p;
                cur->cap_devices = nc;
            }
            Device *d = &cur->devices[cur->n_devices++];
            d->id = (uint16_t)id;
            d->name = strdup(name);
        }
        /* two-tab subsystem lines are ignored */
    }
    free(line);
}

static void ensure_loaded(void)
{
    if (g_loaded)
        return;
    g_loaded = true;
    for (size_t i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) {
        FILE *f = fopen(PATHS[i], "re");
        if (f) {
            load_from(f);
            fclose(f);
            return;
        }
    }
}

static const Vendor *find_vendor(uint16_t vid)
{
    ensure_loaded();
    for (size_t i = 0; i < g_n; i++)
        if (g_vendors[i].id == vid)
            return &g_vendors[i];
    return NULL;
}

const char *pciids_vendor(uint16_t vid)
{
    const Vendor *v = find_vendor(vid);
    return v ? v->name : NULL;
}

const char *pciids_device(uint16_t vid, uint16_t did)
{
    const Vendor *v = find_vendor(vid);
    if (!v)
        return NULL;
    for (size_t i = 0; i < v->n_devices; i++)
        if (v->devices[i].id == did)
            return v->devices[i].name;
    return NULL;
}

void pciids_free(void)
{
    for (size_t i = 0; i < g_n; i++) {
        for (size_t j = 0; j < g_vendors[i].n_devices; j++)
            free(g_vendors[i].devices[j].name);
        free(g_vendors[i].devices);
        free(g_vendors[i].name);
    }
    free(g_vendors);
    g_vendors = NULL;
    g_n = g_cap = 0;
    g_loaded = false;
}
