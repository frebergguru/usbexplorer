/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tui/tui.h"

#include "util/usbids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Case-insensitive substring test (NULL haystack never matches). */
static bool ci_contains(const char *hay, const char *needle)
{
    return hay && *hay && strcasestr(hay, needle) != NULL;
}

bool tui_node_matches(const UsbNode *n, const char *query)
{
    if (!query || !*query)
        return false;
    if (n->kind == NODE_ROOT || n->kind == NODE_CONTROLLER)
        return false;

    if (ci_contains(n->devname, query) || ci_contains(n->manufacturer, query) ||
        ci_contains(n->product, query) || ci_contains(n->serial, query) ||
        ci_contains(n->sysfs_path, query))
        return true;

    if (ci_contains(usbids_vendor(n->vid), query) ||
        ci_contains(usbids_product(n->vid, n->pid), query) ||
        ci_contains(usbids_class(n->dev_class), query))
        return true;

    char vidpid[16];
    snprintf(vidpid, sizeof vidpid, "%04x:%04x", n->vid, n->pid);
    if (strcasestr(vidpid, query))
        return true;

    for (size_t i = 0; i < n->n_interfaces; i++)
        if (ci_contains(n->interfaces[i].driver, query))
            return true;

    return false;
}

/* Flatten every device node (ignoring expand state) in display order. */
static void collect(const UsbNode *n, const UsbNode **arr, size_t *cnt, size_t max)
{
    if (n->kind != NODE_ROOT && *cnt < max)
        arr[(*cnt)++] = n;
    for (size_t i = 0; i < n->n_children; i++)
        collect(n->children[i], arr, cnt, max);
}

bool tui_search_step(TuiState *s, int dir)
{
    if (!s->search[0] || s->n_rows == 0)
        return false;

    /* Gather all nodes in order. */
    size_t cap = 1024;
    const UsbNode **all = malloc(cap * sizeof *all);
    if (!all)
        return false;
    size_t total = 0;
    for (size_t i = 0; i < s->root->n_children; i++)
        collect(s->root->children[i], all, &total, cap);

    /* Find the currently selected node's index. */
    const UsbNode *cur = s->rows[s->sel].node;
    size_t start = 0;
    for (size_t i = 0; i < total; i++)
        if (all[i] == cur) {
            start = i;
            break;
        }

    /* Scan forward/backward with wrap-around. */
    bool found = false;
    for (size_t step = 1; step <= total; step++) {
        size_t idx = (dir >= 0)
                         ? (start + step) % total
                         : (start + total - (step % total)) % total;
        if (tui_node_matches(all[idx], s->search)) {
            const UsbNode *m = all[idx];
            tui_expand_ancestors(s, m);
            tui_tree_rebuild(s);
            size_t row = tui_row_of_node(s, m);
            if (row != (size_t)-1)
                s->sel = row;
            found = true;
            break;
        }
    }

    free(all);
    if (!found)
        snprintf(s->msg, sizeof s->msg, "No match for \"%s\"", s->search);
    return found;
}
