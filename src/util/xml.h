/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_XML_H
#define USBEXPLORER_UTIL_XML_H

#include <stdio.h>

/*
 * Thin convenience wrapper around libxml2's xmlTextWriter, streaming an
 * indented XML document to a FILE*.  Attribute/element escaping is handled by
 * libxml2.  All functions are no-ops on a NULL writer so call sites stay terse.
 */

typedef struct XmlWriter XmlWriter;

XmlWriter *xml_new(FILE *out);          /* starts the document + <?xml ...?> */
int        xml_free(XmlWriter *w);      /* ends the document, flushes, frees; returns 0 on success */

void xml_start(XmlWriter *w, const char *element);
void xml_end(XmlWriter *w);             /* close the most recent element */

void xml_attr(XmlWriter *w, const char *name, const char *fmt, ...);
void xml_text(XmlWriter *w, const char *text);

#endif /* USBEXPLORER_UTIL_XML_H */
