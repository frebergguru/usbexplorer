/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_JSON_H
#define USBEXPLORER_UTIL_JSON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Minimal streaming JSON writer.  No intermediate document model: values are
 * written directly to a FILE* in document order.  Commas, indentation and
 * string escaping are handled automatically.  Pass key = NULL when writing an
 * element of an array (or the single top-level value).
 */

typedef struct JsonWriter JsonWriter;

JsonWriter *json_new(FILE *out, bool pretty);
void        json_free(JsonWriter *w);   /* finishes the last line; frees */

void json_begin_object(JsonWriter *w, const char *key);
void json_end_object(JsonWriter *w);
void json_begin_array(JsonWriter *w, const char *key);
void json_end_array(JsonWriter *w);

void json_str(JsonWriter *w, const char *key, const char *val);
void json_int(JsonWriter *w, const char *key, long long val);
void json_bool(JsonWriter *w, const char *key, bool val);
void json_null(JsonWriter *w, const char *key);

#endif /* USBEXPLORER_UTIL_JSON_H */
