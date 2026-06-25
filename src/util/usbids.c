/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/usbids.h"
#include "config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Parser/cache for the usb.ids database.  The file groups entries into
 * sections; we only care about the vendor/product section (lines that start
 * with a 4-hex-digit vendor id) and the device-class section (lines that
 * start with "C ").  Everything else (HID usage tables, language ids, ...) is
 * skipped.  Within a section, leading tabs indicate nesting depth.
 */

typedef struct {
    uint16_t id;
    char *name;
} Entry;            /* generic id->name (product, subclass) */

typedef struct {
    uint16_t id;
    char *name;
    Entry *products;
    size_t n_products, cap_products;
} Vendor;

typedef struct {
    uint8_t id;
    char *name;
    Entry *protocols;
    size_t n_protocols, cap_protocols;
} Subclass;

typedef struct {
    uint8_t id;
    char *name;
    Subclass *subs;
    size_t n_subs, cap_subs;
} Class;

static Vendor *g_vendors;
static size_t g_n_vendors, g_cap_vendors;
static Class *g_classes;
static size_t g_n_classes, g_cap_classes;
static bool g_loaded;

static const char *const FALLBACK_PATHS[] = {
    USBIDS_PATH,
    "/usr/share/usb.ids",
    "/usr/share/misc/usb.ids",
    "/var/lib/usbutils/usb.ids",
};

/* --- small dynamic-array helpers --- */
#define GROW(arr, n, cap)                                                    \
    do {                                                                     \
        if ((n) == (cap)) {                                                  \
            size_t _nc = (cap) ? (cap) * 2 : 16;                             \
            void *_p = realloc((arr), _nc * sizeof *(arr));                  \
            if (!_p)                                                         \
                return; /* out of memory: stop loading, keep what we have */ \
            (arr) = _p;                                                      \
            (cap) = _nc;                                                     \
        }                                                                    \
    } while (0)

/* Count leading tab characters and return a pointer past them. */
static const char *skip_tabs(const char *s, int *depth)
{
    int d = 0;
    while (*s == '\t') {
        d++;
        s++;
    }
    *depth = d;
    return s;
}

/* Parse a run of hex digits into 'val'; return pointer just past them, or NULL
 * if there were none. */
static const char *parse_hex(const char *s, unsigned long *val)
{
    char *end = NULL;
    *val = strtoul(s, &end, 16);
    if (end == s)
        return NULL;
    return end;
}

/* Given a line cursor positioned at the id, split into id and trimmed name.
 * The name is everything after the run of spaces following the id. */
static const char *id_and_name(const char *cursor, unsigned long *id)
{
    const char *after = parse_hex(cursor, id);
    if (!after)
        return NULL;
    while (*after == ' ' || *after == '\t')
        after++;
    return after; /* name (already newline-trimmed by caller) */
}

static void load_from(FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    Vendor *cur_vendor = NULL;
    Class *cur_class = NULL;
    Subclass *cur_sub = NULL;
    /* Which top-level section the current depth-0 entry opened. */
    enum { SEC_OTHER, SEC_VENDOR, SEC_CLASS } section = SEC_OTHER;

    while ((len = getline(&line, &cap, f)) != -1) {
        /* strip trailing newline */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;

        int depth;
        const char *cur = skip_tabs(line, &depth);

        if (depth == 0) {
            if (cur[0] == 'C' && cur[1] == ' ') {
                unsigned long id;
                const char *name = id_and_name(cur + 2, &id);
                if (!name)
                    continue;
                GROW(g_classes, g_n_classes, g_cap_classes);
                cur_class = &g_classes[g_n_classes++];
                cur_class->id = (uint8_t)id;
                cur_class->name = strdup(name);
                cur_class->subs = NULL;
                cur_class->n_subs = cur_class->cap_subs = 0;
                cur_sub = NULL;
                section = SEC_CLASS;
            } else if (isxdigit((unsigned char)cur[0])) {
                unsigned long id;
                const char *name = id_and_name(cur, &id);
                if (!name)
                    continue;
                GROW(g_vendors, g_n_vendors, g_cap_vendors);
                cur_vendor = &g_vendors[g_n_vendors++];
                cur_vendor->id = (uint16_t)id;
                cur_vendor->name = strdup(name);
                cur_vendor->products = NULL;
                cur_vendor->n_products = cur_vendor->cap_products = 0;
                section = SEC_VENDOR;
            } else {
                section = SEC_OTHER; /* some other table; ignore its body */
            }
        } else if (depth == 1) {
            if (section == SEC_VENDOR && cur_vendor) {
                unsigned long id;
                const char *name = id_and_name(cur, &id);
                if (!name)
                    continue;
                GROW(cur_vendor->products, cur_vendor->n_products,
                     cur_vendor->cap_products);
                Entry *e = &cur_vendor->products[cur_vendor->n_products++];
                e->id = (uint16_t)id;
                e->name = strdup(name);
            } else if (section == SEC_CLASS && cur_class) {
                unsigned long id;
                const char *name = id_and_name(cur, &id);
                if (!name)
                    continue;
                GROW(cur_class->subs, cur_class->n_subs, cur_class->cap_subs);
                cur_sub = &cur_class->subs[cur_class->n_subs++];
                cur_sub->id = (uint8_t)id;
                cur_sub->name = strdup(name);
                cur_sub->protocols = NULL;
                cur_sub->n_protocols = cur_sub->cap_protocols = 0;
            }
        } else if (depth == 2 && section == SEC_CLASS && cur_sub) {
            unsigned long id;
            const char *name = id_and_name(cur, &id);
            if (!name)
                continue;
            GROW(cur_sub->protocols, cur_sub->n_protocols,
                 cur_sub->cap_protocols);
            Entry *e = &cur_sub->protocols[cur_sub->n_protocols++];
            e->id = (uint8_t)id;
            e->name = strdup(name);
        }
        /* deeper nesting (e.g. vendor interface entries) is ignored */
    }

    free(line);
}

static void ensure_loaded(void)
{
    if (g_loaded)
        return;
    g_loaded = true; /* even on failure: don't retry every lookup */

    for (size_t i = 0; i < sizeof FALLBACK_PATHS / sizeof FALLBACK_PATHS[0]; i++) {
        FILE *f = fopen(FALLBACK_PATHS[i], "re");
        if (f) {
            load_from(f);
            fclose(f);
            return;
        }
    }
    /* No database found: lookups will simply return NULL. */
}

static const Vendor *find_vendor(uint16_t vid)
{
    ensure_loaded();
    for (size_t i = 0; i < g_n_vendors; i++)
        if (g_vendors[i].id == vid)
            return &g_vendors[i];
    return NULL;
}

static const Class *find_class(uint8_t cls)
{
    ensure_loaded();
    for (size_t i = 0; i < g_n_classes; i++)
        if (g_classes[i].id == cls)
            return &g_classes[i];
    return NULL;
}

const char *usbids_vendor(uint16_t vid)
{
    const Vendor *v = find_vendor(vid);
    return v ? v->name : NULL;
}

const char *usbids_product(uint16_t vid, uint16_t pid)
{
    const Vendor *v = find_vendor(vid);
    if (!v)
        return NULL;
    for (size_t i = 0; i < v->n_products; i++)
        if (v->products[i].id == pid)
            return v->products[i].name;
    return NULL;
}

const char *usbids_class(uint8_t cls)
{
    const Class *c = find_class(cls);
    return c ? c->name : NULL;
}

const char *usbids_subclass(uint8_t cls, uint8_t sub)
{
    const Class *c = find_class(cls);
    if (!c)
        return NULL;
    for (size_t i = 0; i < c->n_subs; i++)
        if (c->subs[i].id == sub)
            return c->subs[i].name;
    return NULL;
}

const char *usbids_protocol(uint8_t cls, uint8_t sub, uint8_t proto)
{
    const Class *c = find_class(cls);
    if (!c)
        return NULL;
    for (size_t i = 0; i < c->n_subs; i++) {
        if (c->subs[i].id != sub)
            continue;
        const Subclass *s = &c->subs[i];
        for (size_t j = 0; j < s->n_protocols; j++)
            if (s->protocols[j].id == proto)
                return s->protocols[j].name;
    }
    return NULL;
}

void usbids_free(void)
{
    for (size_t i = 0; i < g_n_vendors; i++) {
        for (size_t j = 0; j < g_vendors[i].n_products; j++)
            free(g_vendors[i].products[j].name);
        free(g_vendors[i].products);
        free(g_vendors[i].name);
    }
    free(g_vendors);
    g_vendors = NULL;
    g_n_vendors = g_cap_vendors = 0;

    for (size_t i = 0; i < g_n_classes; i++) {
        for (size_t j = 0; j < g_classes[i].n_subs; j++) {
            for (size_t k = 0; k < g_classes[i].subs[j].n_protocols; k++)
                free(g_classes[i].subs[j].protocols[k].name);
            free(g_classes[i].subs[j].protocols);
            free(g_classes[i].subs[j].name);
        }
        free(g_classes[i].subs);
        free(g_classes[i].name);
    }
    free(g_classes);
    g_classes = NULL;
    g_n_classes = g_cap_classes = 0;

    g_loaded = false;
}
