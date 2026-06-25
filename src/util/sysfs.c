/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/sysfs.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void join_path(char *dst, size_t dst_sz, const char *dir, const char *attr)
{
    snprintf(dst, dst_sz, "%s/%s", dir, attr);
}

/* Strip leading and trailing ASCII whitespace in place. */
static void trim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, len - start + 1);
}

bool sysfs_read_str(const char *dir, const char *attr, char *out, size_t out_sz)
{
    if (out_sz == 0)
        return false;
    out[0] = '\0';

    char path[PATH_MAX];
    join_path(path, sizeof path, dir, attr);

    FILE *f = fopen(path, "re");
    if (!f)
        return false;

    size_t n = fread(out, 1, out_sz - 1, f);
    out[n] = '\0';
    fclose(f);

    trim(out);
    return true;
}

uint16_t sysfs_read_hex16(const char *dir, const char *attr, uint16_t fallback)
{
    char buf[32];
    if (!sysfs_read_str(dir, attr, buf, sizeof buf) || buf[0] == '\0')
        return fallback;

    char *end = NULL;
    unsigned long v = strtoul(buf, &end, 16);
    if (end == buf || v > 0xFFFF)
        return fallback;
    return (uint16_t)v;
}

uint8_t sysfs_read_hex8(const char *dir, const char *attr, uint8_t fallback)
{
    char buf[32];
    if (!sysfs_read_str(dir, attr, buf, sizeof buf) || buf[0] == '\0')
        return fallback;

    char *end = NULL;
    unsigned long v = strtoul(buf, &end, 16);
    if (end == buf || v > 0xFF)
        return fallback;
    return (uint8_t)v;
}

int sysfs_read_int(const char *dir, const char *attr, int fallback)
{
    char buf[32];
    if (!sysfs_read_str(dir, attr, buf, sizeof buf) || buf[0] == '\0')
        return fallback;

    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf)
        return fallback;
    return (int)v;
}

bool sysfs_read_blob(const char *dir, const char *attr,
                     uint8_t **out_buf, size_t *out_len)
{
    *out_buf = NULL;
    *out_len = 0;

    char path[PATH_MAX];
    join_path(path, sizeof path, dir, attr);

    FILE *f = fopen(path, "re");
    if (!f)
        return false;

    /* sysfs binary blobs are small; grow a buffer as we read. */
    size_t cap = 4096, len = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return false;
    }

    for (;;) {
        if (len == cap) {
            size_t ncap = cap * 2;
            uint8_t *nbuf = realloc(buf, ncap);
            if (!nbuf) {
                free(buf);
                fclose(f);
                return false;
            }
            buf = nbuf;
            cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0)
            break;
    }
    fclose(f);

    if (len == 0) {
        free(buf);
        return false;
    }

    *out_buf = buf;
    *out_len = len;
    return true;
}

bool sysfs_read_link_base(const char *dir, const char *attr,
                          char *out, size_t out_sz)
{
    if (out_sz == 0)
        return false;
    out[0] = '\0';

    char path[PATH_MAX];
    join_path(path, sizeof path, dir, attr);

    char target[PATH_MAX];
    ssize_t n = readlink(path, target, sizeof target - 1);
    if (n < 0)
        return false;
    target[n] = '\0';

    const char *base = strrchr(target, '/');
    base = base ? base + 1 : target;
    snprintf(out, out_sz, "%s", base);
    return out[0] != '\0';
}

bool sysfs_exists(const char *dir, const char *attr)
{
    char path[PATH_MAX];
    join_path(path, sizeof path, dir, attr);
    return access(path, F_OK) == 0;
}
