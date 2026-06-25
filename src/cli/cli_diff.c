/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cli/cli.h"
#include "usb/descriptors.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- build a deterministic line dump of a device ---- */

typedef struct {
    char **lines;
    size_t n, cap;
} Lines;

static void push(Lines *L, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void push(Lines *L, const char *fmt, ...)
{
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->lines = realloc(L->lines, L->cap * sizeof *L->lines);
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    L->lines[L->n++] = strdup(buf);
}

static void flatten_desc(Lines *L, const DescNode *d, int depth)
{
    push(L, "%*s%s", depth * 2, "", d->type);
    for (size_t i = 0; i < d->n_fields; i++)
        push(L, "%*s  %s = %s", depth * 2, "", d->fields[i].name, d->fields[i].value);
    for (size_t i = 0; i < d->n_children; i++)
        flatten_desc(L, d->children[i], depth + 1);
}

static void build_dump(const UsbNode *n, Lines *L)
{
    push(L, "VID:PID = %04x:%04x", n->vid, n->pid);
    push(L, "Class = 0x%02x/0x%02x/0x%02x", n->dev_class, n->dev_subclass, n->dev_protocol);
    push(L, "Speed = %s", usb_speed_badge(n->speed));
    push(L, "USB = %x.%02x  Device = %x.%02x", n->bcd_usb >> 8, n->bcd_usb & 0xff,
         n->bcd_device >> 8, n->bcd_device & 0xff);
    push(L, "Configurations = %u   Interfaces = %d", n->num_configs, n->num_interfaces);
    for (size_t i = 0; i < n->n_interfaces; i++) {
        const UsbInterface *ui = &n->interfaces[i];
        push(L, "Interface %u.%u = 0x%02x/0x%02x/0x%02x (%s)", ui->number, ui->alt,
             ui->cls, ui->subcls, ui->proto, ui->driver[0] ? ui->driver : "-");
    }
    if (n->raw_descriptors && n->raw_len) {
        DescNode *d = usb_decode_descriptors(n->raw_descriptors, n->raw_len);
        if (d) {
            for (size_t i = 0; i < d->n_children; i++)
                flatten_desc(L, d->children[i], 0);
            desc_node_free(d);
        }
    }
}

static void free_lines(Lines *L)
{
    for (size_t i = 0; i < L->n; i++)
        free(L->lines[i]);
    free(L->lines);
}

/* ---- LCS-based unified diff over the two line arrays ---- */

static int diff_to(FILE *out, const Lines *a, const Lines *b)
{
    size_t na = a->n, nb = b->n;
    /* dp[(na+1) x (nb+1)] LCS lengths */
    int *dp = calloc((na + 1) * (nb + 1), sizeof *dp);
    if (!dp)
        return -1;
#define DP(i, j) dp[(i) * (nb + 1) + (j)]
    for (size_t i = na; i-- > 0;)
        for (size_t j = nb; j-- > 0;)
            DP(i, j) = (strcmp(a->lines[i], b->lines[j]) == 0)
                           ? DP(i + 1, j + 1) + 1
                           : (DP(i + 1, j) >= DP(i, j + 1) ? DP(i + 1, j) : DP(i, j + 1));

    int changes = 0;
    size_t i = 0, j = 0;
    while (i < na && j < nb) {
        if (strcmp(a->lines[i], b->lines[j]) == 0) {
            fprintf(out, "  %s\n", a->lines[i]);
            i++; j++;
        } else if (DP(i + 1, j) >= DP(i, j + 1)) {
            fprintf(out, "- %s\n", a->lines[i++]);
            changes++;
        } else {
            fprintf(out, "+ %s\n", b->lines[j++]);
            changes++;
        }
    }
    while (i < na) { fprintf(out, "- %s\n", a->lines[i++]); changes++; }
    while (j < nb) { fprintf(out, "+ %s\n", b->lines[j++]); changes++; }
#undef DP
    free(dp);
    return changes;
}

/* Build the diff of two nodes as a heap string (caller frees). */
char *cli_diff_string(const UsbNode *na, const UsbNode *nb)
{
    char *text = NULL;
    size_t tlen = 0;
    FILE *ms = open_memstream(&text, &tlen);
    if (!ms)
        return NULL;
    Lines a = {0}, b = {0};
    build_dump(na, &a);
    build_dump(nb, &b);
    int changes = diff_to(ms, &a, &b);
    if (changes == 0)
        fprintf(ms, "(devices are identical at the descriptor level)\n");
    free_lines(&a);
    free_lines(&b);
    fclose(ms);
    return text;
}

/* ---- resolve a "bus.dev" or "vid:pid" spec to a node ---- */

static const UsbNode *resolve(const UsbNode *root, const char *spec)
{
    if (strchr(spec, ':')) {
        unsigned int vid, pid;
        if (sscanf(spec, "%x:%x", &vid, &pid) == 2)
            return usb_find_by_vidpid(root, (uint16_t)vid, (uint16_t)pid);
    } else if (strchr(spec, '.')) {
        int bus, dev;
        if (sscanf(spec, "%d.%d", &bus, &dev) == 2)
            return usb_find_by_busdev(root, bus, dev);
    }
    return NULL;
}

int cli_diff(const UsbNode *root, const char *arg)
{
    char spec[128];
    snprintf(spec, sizeof spec, "%s", arg);
    char *comma = strchr(spec, ',');
    if (!comma) {
        fprintf(stderr, "usbexplorer: --diff expects A,B (each bus.dev or vid:pid)\n");
        return 2;
    }
    *comma = '\0';
    const char *sa = spec, *sb = comma + 1;

    const UsbNode *na = resolve(root, sa);
    const UsbNode *nb = resolve(root, sb);
    if (!na) { fprintf(stderr, "usbexplorer: device '%s' not found\n", sa); return 1; }
    if (!nb) { fprintf(stderr, "usbexplorer: device '%s' not found\n", sb); return 1; }

    printf("--- %s  (%04x:%04x %s)\n", sa, na->vid, na->pid,
           na->product[0] ? na->product : "");
    printf("+++ %s  (%04x:%04x %s)\n", sb, nb->vid, nb->pid,
           nb->product[0] ? nb->product : "");

    char *diff = cli_diff_string(na, nb);
    if (diff) {
        fputs(diff, stdout);
        free(diff);
    }
    return 0;
}
