/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/json.h"

#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 64

struct JsonWriter {
    FILE *out;
    bool  pretty;
    int   depth;
    /* For each open container: is it an array, and does it already hold an
     * item (so the next item needs a leading comma)? */
    bool  is_array[JSON_MAX_DEPTH];
    bool  has_item[JSON_MAX_DEPTH];
};

JsonWriter *json_new(FILE *out, bool pretty)
{
    JsonWriter *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->out = out;
    w->pretty = pretty;
    return w;
}

static void indent(JsonWriter *w)
{
    if (!w->pretty)
        return;
    fputc('\n', w->out);
    for (int i = 0; i < w->depth; i++)
        fputs("  ", w->out);
}

/* Emit the separator/indent and optional key before a value. */
static void prefix(JsonWriter *w, const char *key)
{
    if (w->depth > 0) {
        if (w->has_item[w->depth - 1])
            fputc(',', w->out);
        w->has_item[w->depth - 1] = true;
        indent(w);
    }
    if (key) {
        fputc('"', w->out);
        fputs(key, w->out); /* keys here are fixed identifiers: no escaping */
        fputs(w->pretty ? "\": " : "\":", w->out);
    }
}

static void write_escaped(JsonWriter *w, const char *s)
{
    fputc('"', w->out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", w->out); break;
        case '\\': fputs("\\\\", w->out); break;
        case '\b': fputs("\\b", w->out);  break;
        case '\f': fputs("\\f", w->out);  break;
        case '\n': fputs("\\n", w->out);  break;
        case '\r': fputs("\\r", w->out);  break;
        case '\t': fputs("\\t", w->out);  break;
        default:
            if (*p < 0x20)
                fprintf(w->out, "\\u%04x", *p);
            else
                fputc(*p, w->out);
        }
    }
    fputc('"', w->out);
}

static void push(JsonWriter *w, bool is_array)
{
    if (w->depth < JSON_MAX_DEPTH) {
        w->is_array[w->depth] = is_array;
        w->has_item[w->depth] = false;
    }
    w->depth++;
}

static void pop(JsonWriter *w, char closer)
{
    bool had_items = w->depth > 0 && w->has_item[w->depth - 1];
    w->depth--;
    if (w->pretty && had_items)
        indent(w);
    fputc(closer, w->out);
}

void json_begin_object(JsonWriter *w, const char *key)
{
    prefix(w, key);
    fputc('{', w->out);
    push(w, false);
}

void json_end_object(JsonWriter *w) { pop(w, '}'); }

void json_begin_array(JsonWriter *w, const char *key)
{
    prefix(w, key);
    fputc('[', w->out);
    push(w, true);
}

void json_end_array(JsonWriter *w) { pop(w, ']'); }

void json_str(JsonWriter *w, const char *key, const char *val)
{
    prefix(w, key);
    if (val)
        write_escaped(w, val);
    else
        fputs("null", w->out);
}

void json_int(JsonWriter *w, const char *key, long long val)
{
    prefix(w, key);
    fprintf(w->out, "%lld", val);
}

void json_bool(JsonWriter *w, const char *key, bool val)
{
    prefix(w, key);
    fputs(val ? "true" : "false", w->out);
}

void json_null(JsonWriter *w, const char *key)
{
    prefix(w, key);
    fputs("null", w->out);
}

void json_free(JsonWriter *w)
{
    if (!w)
        return;
    if (w->pretty)
        fputc('\n', w->out);
    free(w);
}
