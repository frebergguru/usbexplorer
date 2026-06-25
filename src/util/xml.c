/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/xml.h"

#include <libxml/xmlwriter.h>
#include <stdarg.h>
#include <stdlib.h>

struct XmlWriter {
    xmlTextWriterPtr w;
};

XmlWriter *xml_new(FILE *out)
{
    XmlWriter *xw = calloc(1, sizeof *xw);
    if (!xw)
        return NULL;

    /* Take ownership of 'out' via an output buffer (closed by libxml2). */
    xmlOutputBufferPtr buf = xmlOutputBufferCreateFile(out, NULL);
    if (!buf) {
        free(xw);
        return NULL;
    }
    xw->w = xmlNewTextWriter(buf);
    if (!xw->w) {
        xmlOutputBufferClose(buf);
        free(xw);
        return NULL;
    }
    xmlTextWriterSetIndent(xw->w, 1);
    xmlTextWriterSetIndentString(xw->w, BAD_CAST "  ");
    xmlTextWriterStartDocument(xw->w, NULL, "UTF-8", NULL);
    return xw;
}

int xml_free(XmlWriter *w)
{
    if (!w)
        return -1;
    int rc = xmlTextWriterEndDocument(w->w);
    xmlFreeTextWriter(w->w);
    free(w);
    return rc < 0 ? -1 : 0;
}

void xml_start(XmlWriter *w, const char *element)
{
    if (w)
        xmlTextWriterStartElement(w->w, BAD_CAST element);
}

void xml_end(XmlWriter *w)
{
    if (w)
        xmlTextWriterEndElement(w->w);
}

void xml_attr(XmlWriter *w, const char *name, const char *fmt, ...)
{
    if (!w)
        return;
    char val[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(val, sizeof val, fmt, ap);
    va_end(ap);
    xmlTextWriterWriteAttribute(w->w, BAD_CAST name, BAD_CAST val);
}

void xml_text(XmlWriter *w, const char *text)
{
    if (w && text)
        xmlTextWriterWriteString(w->w, BAD_CAST text);
}
